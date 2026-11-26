#include "call_bridge.h"
#include "platform/multimedia_support.h"

#include "h264_codec.h"
#include "peer_video.h"
#include "core/media/media_utils.h"
#include "network/zalo_crypto.h"
#include "network/zalo_session.h"

#include <QAudioDevice>
#include <QCameraDevice>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMediaDevices>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>
#include <QVersionNumber>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QUuid>
#include <QVideoFrame>
#include <QVideoSink>

#include <algorithm>

namespace Zalos::Core::Call {

namespace {

constexpr int kMaxHexPerFrame = 4000;

QString randomToken() {
    return "zcall-" + QUuid::createUuid().toString(QUuid::Id128);
}

}

CallBridge::CallBridge(Network::ZaloSession* session, QObject* parent)
    : QObject(parent), m_session(session) {
    QSettings settings(QStringLiteral("Zalos"), QStringLiteral("ZalosApp"));
    m_selectedMicId = settings.value(QStringLiteral("call/micDevice")).toString();
    m_selectedSpeakerId = settings.value(QStringLiteral("call/speakerDevice")).toString();
    m_selectedCameraId = settings.value(QStringLiteral("call/cameraDevice")).toString();

    m_idleShutdownTimer = new QTimer(this);
    m_idleShutdownTimer->setSingleShot(true);
    m_idleShutdownTimer->setInterval(1500);
    connect(m_idleShutdownTimer, &QTimer::timeout, this, [this] {
        if (phase().isEmpty() || phase() == QLatin1String("ended")) shutdown();
    });

    m_hangupWatchdog = new QTimer(this);
    m_hangupWatchdog->setSingleShot(true);
    m_hangupWatchdog->setInterval(1500);
    connect(m_hangupWatchdog, &QTimer::timeout, this, [this] {
        const QString p = phase();
        if (p.isEmpty() || p == QLatin1String("ended")) return;
        const QString savedName = peerName();
        const QString savedAvatar = peerAvatar();
        const bool savedGroup = isGroup();
        const QString savedPeer = m_activePeer;
        const bool savedOutgoing = m_activeOutgoing;
        const bool savedVideo = m_isVideoMode;
        const bool savedBubble = m_bubbleEmitted;
        if (m_process && m_engineReady && m_sendSocket && m_sendSocket->state() == QAbstractSocket::ConnectedState) {
            sendToEngine({{"type", "request"}, {"command", "endCall"}, {"data", QJsonObject()}});
            m_sendSocket->flush();
        }
        shutdown();
        m_ui = QJsonObject{{"phase", "ended"}, {"name", savedName}, {"avatar", savedAvatar},
                           {"group", savedGroup}, {"statusCode", "ended"}};
        emit stateChanged();
        emit cameraStateChanged();
        if (!savedPeer.isEmpty() && !savedGroup && !savedBubble) {
            m_bubbleEmitted = true;
            emit callFinished(savedPeer, savedOutgoing, 0, savedVideo);
        }
    });
    refreshAvailability();
}

CallBridge::~CallBridge() {
    shutdown();
    if (m_videoThread) {
        m_videoThread->quit();
        m_videoThread->wait();
        delete m_videoDecoder;
    }
}

bool CallBridge::videoSupported() { return OpenH264::instance().available(); }

void CallBridge::startVideoPipeline() {
    if (m_videoThread || !videoSupported()) return;
    m_videoThread = new QThread(this);
    m_videoThread->setObjectName(QStringLiteral("zalos-video"));
    m_videoDecoder = new PeerVideoDecoder;
    m_videoDecoder->setPendingCounter(&m_pendingVideoFrames);
    m_videoDecoder->moveToThread(m_videoThread);
    connect(this, &CallBridge::videoFrameReceived, m_videoDecoder, &PeerVideoDecoder::decode, Qt::QueuedConnection);
    connect(this, &CallBridge::peerVideoResetRequested, m_videoDecoder, &PeerVideoDecoder::reset, Qt::QueuedConnection);
    connect(m_videoDecoder, &PeerVideoDecoder::frameDecoded, this, &CallBridge::onPeerFrame, Qt::QueuedConnection);
    connect(m_videoDecoder, &PeerVideoDecoder::keyFrameNeeded, this, &CallBridge::requestKeyFrame, Qt::QueuedConnection);
    m_peerVideoIdle = new QTimer(this);
    m_peerVideoIdle->setSingleShot(true);
    m_peerVideoIdle->setInterval(2000);
    connect(m_peerVideoIdle, &QTimer::timeout, this, [this] {
        m_peerVideoActive = false;
        emit peerVideoChanged();
    });
    m_videoThread->start();
}

void CallBridge::setPeerVideoSink(QVideoSink* sink) { m_peerSink = sink; }

void CallBridge::onPeerFrame(const QVideoFrame& frame, int src) {
    if (src != 0 || phase().isEmpty() || phase() == "ended") return;
    if (m_peerSink) m_peerSink->setVideoFrame(frame);
    m_peerVideoIdle->start();
    if (!m_peerVideoActive) {
        m_peerVideoActive = true;
        emit peerVideoChanged();
    }
}

void CallBridge::requestKeyFrame(const QString& why, int src) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastKeyRequestMs < 1000) return;
    m_lastKeyRequestMs = now;
    QJsonObject action{{"action", "needkey"}, {"why", why}};
    if (src != 0) action["src"] = src;
    sendUi(action);
}

QString CallBridge::findEngine() {
    const QString app = QCoreApplication::applicationDirPath();
    QStringList candidates{app + QStringLiteral("/../libexec/zalos/zalos-call")};
#ifdef ZALOS_CALL_BUILD_PATH
    candidates << QStringLiteral(ZALOS_CALL_BUILD_PATH);
#endif
    for (const QString& candidate : candidates) {
        if (QFileInfo(candidate).isExecutable()) return QDir::cleanPath(candidate);
    }
    return {};
}

QString CallBridge::audioEngineFor(const QString& callEnginePath) {
    if (callEnginePath.isEmpty()) return {};
    const QString candidate = QFileInfo(callEnginePath).absolutePath() + QStringLiteral("/zalos-call-audio");
    return QFileInfo(candidate).isExecutable() ? candidate : QString();
}

void CallBridge::refreshAvailability() {
    QString reason;
    m_enginePath = findEngine();
    if (!m_keyFile) {
        QByteArray key(16, '\0');
        QRandomGenerator::system()->generate(reinterpret_cast<quint32*>(key.data()),
                                             reinterpret_cast<quint32*>(key.data() + key.size()));
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        m_keyFile = std::make_unique<QTemporaryFile>((dir.isEmpty() ? QDir::tempPath() : dir)
                                                     + "/zalos-callv2-XXXXXX.key");
        if (writeKeyFile(m_keyFile.get(), key)) {
            m_key = key;
            m_keyFilePath = m_keyFile->fileName();
        }
    }
    if (m_enginePath.isEmpty() || audioEngineFor(m_enginePath).isEmpty()) reason = "engine_missing";
    else if (m_key.size() != 16) reason = "key_missing";
    if (reason != m_unavailableReason) {
        m_unavailableReason = reason;
        emit availabilityChanged();
    }
}

bool CallBridge::writeKeyFile(QTemporaryFile* file, const QByteArray& key) {
    if (!file || key.size() != 16) return false;
    if (!file->isOpen() && !file->open()) return false;
    file->setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file->resize(0);
    file->seek(0);
    const QByteArray line = key.toBase64() + "\n";
    return file->write(line) == line.size() && file->flush();
}

QByteArray CallBridge::encodeFrame(const QJsonObject& frame, const QByteArray& key) {
    const QByteArray hex = Network::ZaloCrypto::aesCbcEncrypt(key, QJsonDocument(frame).toJson(QJsonDocument::Compact)).toHex();
    if (hex.size() <= kMaxHexPerFrame) return hex + '$';
    QByteArray out;
    const int total = int((hex.size() + kMaxHexPerFrame - 1) / kMaxHexPerFrame);
    static std::atomic<quint64> nextChunkId{0};
    const QByteArray id = QByteArray::number(++nextChunkId);
    for (int i = 0; i < total; ++i) {
        out += hex.mid(qint64(i) * kMaxHexPerFrame, kMaxHexPerFrame) + '#' + id + '#' + QByteArray::number(total) + '#'
               + QByteArray::number(i) + "#$";
    }
    return out;
}

QList<QJsonObject> CallBridge::decodeFrames(QByteArray& buffer, const QByteArray& key, QHash<QString, QStringList>* chunks) {
    QList<QJsonObject> frames;
    int end;
    while ((end = buffer.indexOf('$')) >= 0) {
        QByteArray token = buffer.left(end);
        buffer.remove(0, end + 1);
        if (token.isEmpty()) continue;

        QByteArray hex = token;
        if (token.contains('#')) {
            const QList<QByteArray> f = token.split('#');
            if (f.size() < 4 || !chunks) continue;
            const QString id = QString::fromLatin1(f.at(1));
            const int total = f.at(2).toInt();
            const int index = f.at(3).toInt();
            if (total <= 0 || total > 4096 || index < 0 || index >= total) continue;
            if (!chunks->contains(id) && chunks->size() >= 64) {
                const auto keys = chunks->keys();
                chunks->remove(keys.first());
            }
            QStringList& parts = (*chunks)[id];
            if (parts.size() < total) parts.resize(total);
            parts[index] = QString::fromLatin1(f.at(0));
            if (parts.contains(QString())) continue;
            hex = parts.join(QString()).toLatin1();
            chunks->remove(id);
        }
        bool ok = false;
        const QByteArray plain = Network::ZaloCrypto::aesCbcDecrypt(key, QByteArray::fromHex(hex), &ok);
        const QJsonObject obj = QJsonDocument::fromJson(plain).object();
        if (ok && !obj.isEmpty()) frames.append(obj);
    }
    return frames;
}

void CallBridge::setLocalUser(const QString& uid, const QString& name, const QString& avatar) {
    m_local = QJsonObject{{"id", uid}, {"name", name}, {"avatar", avatar}};
    if (m_engineReady) sendToEngine({{"type", "update"}, {"command", "updateLocal"}, {"data", QJsonObject{{"local", m_local}}}});
}

void CallBridge::setProfileResolver(std::function<QPair<QString, QString>(const QString&)> resolver) {
    m_resolver = std::move(resolver);
}

bool CallBridge::ensureEngine() {
    if (m_idleShutdownTimer) m_idleShutdownTimer->stop();
    if (m_process) return true;
    refreshAvailability();
    if (!isAvailable()) return false;

    m_token = randomToken();
    m_uiToken = randomToken();
    m_engineReady = false;
    m_recvBuffer.clear();
    m_uiBuffer.clear();
    m_uiSkipBytes = 0;
    m_chunks.clear();

    m_recvServer = new QTcpServer(this);
    m_sendServer = new QTcpServer(this);
    m_uiServer = new QTcpServer(this);
    if (!m_recvServer->listen(QHostAddress::LocalHost) || !m_sendServer->listen(QHostAddress::LocalHost)
        || !m_uiServer->listen(QHostAddress::LocalHost)) {
        shutdown();
        return false;
    }

    const quint64 gen = ++m_engineGen;
    const QPointer<CallBridge> self(this);
    auto acceptOn = [this, self, gen](QTcpServer* server, int role) {
        connect(server, &QTcpServer::newConnection, this, [this, self, gen, server, role]() {
            if (!self || gen != m_engineGen) return;
            while (QTcpSocket* sock = server->nextPendingConnection()) {
                auto* pending = new QByteArray;
                connect(sock, &QTcpSocket::readyRead, this, [this, self, gen, sock, pending, role]() {
                    if (!self || gen != m_engineGen) return;
                    if (!sock->property("authed").toBool()) {
                        pending->append(sock->readAll());
                        if (pending->size() > 4096) {
                            sock->abort();
                            return;
                        }
                        const int nl = pending->indexOf('\n');
                        if (nl < 0) return;
                        const QByteArray tok = pending->left(nl);
                        const QByteArray rest = pending->mid(nl + 1);
                        const QString expected = role == 2 ? m_uiToken : m_token;
                        if (tok != expected.toLatin1()) {
                            sock->disconnectFromHost();
                            return;
                        }
                        sock->setProperty("authed", true);
                        if (role == 0) { m_recvSocket = sock; m_recvBuffer = rest; }
                        else if (role == 1) { m_sendSocket = sock; flushPending(); }
                        else {
                            m_uiSocket = sock;
                            m_uiBuffer = rest;
                            if (!m_selectedMicId.isEmpty())
                                sendUi({{"action", "device"}, {"kind", "mic"}, {"id", m_selectedMicId}});
                            if (!m_selectedSpeakerId.isEmpty())
                                sendUi({{"action", "device"}, {"kind", "speaker"}, {"id", m_selectedSpeakerId}});
                        }
                    } else if (role == 0) {
                        m_recvBuffer += sock->readAll();
                    } else if (role == 2) {
                        m_uiBuffer += sock->readAll();
                    } else {
                        sock->readAll();
                    }
                    if (role == 0) {
                        for (const QJsonObject& f : decodeFrames(m_recvBuffer, m_key, &m_chunks)) {
                            if (!self || gen != m_engineGen) return;
                            onEngineFrame(f);
                        }
                    } else if (role == 2) {
                        drainUiBuffer();
                    }
                });
                connect(sock, &QTcpSocket::disconnected, sock, [sock, pending]() {
                    delete pending;
                    sock->deleteLater();
                });
            }
        });
    };
    acceptOn(m_recvServer, 0);
    acceptOn(m_sendServer, 1);
    acceptOn(m_uiServer, 2);

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    QProcess* proc = m_process;
    connect(m_process, &QProcess::finished, this, [this, self, gen, proc]() {
        if (!self || gen != m_engineGen || proc != m_process) return;
        const bool wasInCall = !m_activePeer.isEmpty();
        const bool showing = !phase().isEmpty() && phase() != QLatin1String("ended");
        const QString peer = m_activePeer;
        const QJsonValue callId = m_activeCallId;
        const int callType = m_activeCallType;
        const bool outgoing = m_activeOutgoing;
        const bool wasVideo = m_isVideoMode;
        const bool connected = m_activeConnected;
        const bool isGroup = m_isGroupMode;
        const bool endSent = m_endSignalSent;
        const bool bubbleEmitted = m_bubbleEmitted;
        shutdown();
        if (!peer.isEmpty() && !isGroup && !endSent && !callId.isUndefined() && !callId.isNull() && m_session) {
            const QString imei = m_session->imei();
            const QString path = connected ? QStringLiteral("/api/voicecall/endcall")
                                           : QStringLiteral("/api/voicecall/cancel");
            const QJsonObject params = connected
                ? QJsonObject{{"uidTo", peer}, {"callId", callId}, {"status", 3}, {"imei", imei}}
                : QJsonObject{{"callerId", peer}, {"callId", callId}, {"callType", callType}, {"status", 0}, {"imei", imei}};
            qWarning() << "CallBridge: call engine exited before sending" << path << "for peer" << peer;
            m_session->apiGet("voice_call", path, params, [path](bool ok, const QJsonValue&, const QString& err, int code) {
                if (!ok) qWarning() << "CallBridge fallback" << path << "failed:" << code << err;
            });
        }
        if (wasInCall || showing) {
            m_ui = QJsonObject{{"phase", "ended"}, {"name", peerName()}, {"avatar", peerAvatar()}, {"group", isGroup},
                               {"statusCode", "disconnected"}};
            emit stateChanged();
        }
        if (!peer.isEmpty() && !isGroup && !bubbleEmitted) {
            m_bubbleEmitted = true;
            emit callFinished(peer, outgoing, 0, wasVideo);
        }
    });
    connect(m_process, &QProcess::errorOccurred, this, [this, self, gen, proc](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || !self || gen != m_engineGen || proc != m_process) return;
        shutdown();
        m_ui = QJsonObject{{"phase", "ended"}, {"name", peerName()}, {"avatar", peerAvatar()}, {"statusCode", "disconnected"}};
        emit stateChanged();
    });
    const QStringList engineArgs{"--token", m_token,
                                 "--recv", QString::number(m_recvServer->serverPort()),
                                 "--send", QString::number(m_sendServer->serverPort()),
                                 "--media", "zrtc", "--send-udp", "--group",
                                 "--key-file", m_keyFilePath,
                                 "--ui-port", QString::number(m_uiServer->serverPort()),
                                 "--ui-token", m_uiToken};
    m_process->start(m_enginePath, engineArgs);
    return true;
}

void CallBridge::shutdown() {
    ++m_engineGen;
    if (m_idleShutdownTimer) m_idleShutdownTimer->stop();
    if (m_hangupWatchdog) m_hangupWatchdog->stop();
    if (m_peerVideoIdle) m_peerVideoIdle->stop();
    if (m_process) {
        QProcess* p = m_process;
        m_process = nullptr;
        p->disconnect(this);
        if (p->state() != QProcess::NotRunning) {
            p->terminate();
            if (!p->waitForFinished(1500)) {
                p->kill();
                p->waitForFinished(500);
            }
        }
        p->deleteLater();
    }
    for (QPointer<QTcpSocket>* sock : {&m_recvSocket, &m_sendSocket, &m_uiSocket}) {
        if (*sock) {
            (*sock)->disconnect(this);
            (*sock)->abort();
            (*sock)->deleteLater();
            *sock = nullptr;
        }
    }
    for (QTcpServer** s : {&m_recvServer, &m_sendServer, &m_uiServer}) {
        if (*s) {
            (*s)->close();
            (*s)->deleteLater();
            *s = nullptr;
        }
    }
    m_engineReady = false;
    m_pending.clear();
    m_recvBuffer.clear();
    m_uiBuffer.clear();
    m_chunks.clear();
    m_activePeer.clear();
    m_activeCallId = QJsonValue();
    m_activeCallType = 0;
    m_activeConnected = false;
    m_endSignalSent = false;
    m_bubbleEmitted = false;
    m_isVideoMode = false;
    m_isGroupMode = false;
    if (m_cameraSender) m_cameraSender->stop();
    m_cameraOn = false;
    m_prevCameraOn = false;
    m_screenShareOn = false;
    m_cameraOffAnnounced = false;
    m_uiSkipBytes = 0;
    if (m_peerSink) m_peerSink->setVideoFrame(QVideoFrame());
    m_pendingVideoFrames.store(0, std::memory_order_relaxed);
    if (m_peerVideoActive) {
        m_peerVideoActive = false;
        emit peerVideoChanged();
    }
    emit peerVideoResetRequested();
}

void CallBridge::sendToEngine(const QJsonObject& frame) {
    if (!m_sendSocket || !m_engineReady) {
        m_pending.append(frame);
        return;
    }
    m_sendSocket->write(encodeFrame(frame, m_key));
}

void CallBridge::flushPending() {
    if (!m_sendSocket || !m_engineReady) return;
    const QList<QJsonObject> pending = m_pending;
    m_pending.clear();
    for (const QJsonObject& f : pending) m_sendSocket->write(encodeFrame(f, m_key));
}

void CallBridge::onEngineFrame(const QJsonObject& frame) {
    const QString type = frame.value("type").toString();
    const QString command = frame.value("command").toVariant().toString();
    const QJsonObject data = frame.value("data").toObject();

    if (type == "update") {
        if (command == "native-ready") {
            m_engineReady = true;
            m_pending.prepend(QJsonObject{{"type", "update"}, {"command", "init"},
                                          {"data", QJsonObject{{"local", m_local}}}});
            flushPending();
        } else if (command == "callState" && data.value("state").toString() == "free") {
            m_activePeer.clear();
            const QString p = phase();
            if (!p.isEmpty() && p != QLatin1String("ended")) {
                if (m_hangupWatchdog) m_hangupWatchdog->stop();
                m_ui = QJsonObject{{"phase", "ended"}, {"name", peerName()}, {"avatar", peerAvatar()},
                                   {"group", isGroup()}, {"statusCode", "ended"}};
                m_isVideoMode = false;
                if (m_cameraSender) m_cameraSender->stop();
                m_cameraOn = false;
                m_prevCameraOn = false;
                m_screenShareOn = false;
                emit stateChanged();
                emit cameraStateChanged();
                if (m_process && m_idleShutdownTimer) m_idleShutdownTimer->start();
            }
        } else if (command == "bubble") {
            m_bubbleEmitted = true;
            const QString peer = data.value("partnerId").toVariant().toString();
            emit callFinished(peer.isEmpty() ? m_activePeer : peer, data.value("role").toBool(),
                              data.value("duration").toInt(), m_isVideoMode);
        }
        return;
    }
    if (type == "sendSignal") {
        onSendSignal(frame.value("command").toInt(), data);
        return;
    }
    if (type == "request") {
        if (command == "getAliasName") {
            const QString uid = data.value("noisedId").toVariant().toString();
            const auto profile = m_resolver ? m_resolver(uid) : QPair<QString, QString>();
            QJsonObject reply{{"noisedId", uid}, {"aliasName", profile.first}, {"avatar", profile.second}};
            if (data.contains("requestId")) reply["requestId"] = data.value("requestId");
            sendToEngine({{"type", "response"}, {"command", "getAliasName"}, {"data", reply}});
        } else if (command == "killMe") {
            shutdown();
        }
    }
}

void CallBridge::onSendSignal(int command, const QJsonObject& d) {
    if (!m_session) return;
    const QString imei = m_session->imei();
    QString path;
    QJsonObject params;
    auto withExtend = [&](QJsonObject p) {
        if (d.contains("extendData") && !d.value("extendData").toString().isEmpty()) {
            p["extendData"] = d.value("extendData");
            p["subCommand"] = 3;
        }
        return p;
    };

    switch (command) {
        case 401:
            path = "/api/voicecall/requestcall";
            params = {{"calleeId", d.value("calleeId")}, {"callId", d.value("callId")},
                      {"codec", d.value("codec").isNull() ? QJsonValue("[]") : d.value("codec")},
                      {"typeRequest", d.value("type")}, {"imei", imei}};
            m_activePeer = d.value("calleeId").toVariant().toString();
            m_activeCallId = d.value("callId");
            m_activeCallType = d.value("type").toInt() == 3 ? 1 : 0;
            m_activeOutgoing = true;
            m_endSignalSent = false;
            m_bubbleEmitted = false;
            break;
        case 416:
            path = "/api/voicecall/request";
            params = withExtend({{"calleeId", d.value("calleeId")}, {"rtcpAddress", d.value("rtcpAddress")},
                                 {"rtpAddress", d.value("rtpAddress")}, {"codec", d.value("codec")},
                                 {"session", d.value("session")}, {"callId", d.value("callId")}, {"imei", imei}});
            m_activeCallId = d.value("callId");
            break;
        case 407:
            path = "/api/voicecall/ringring";
            params = {{"callerId", d.value("callerId")}, {"callId", d.value("callId")}, {"status", 0}, {"imei", imei}};
            m_activePeer = d.value("callerId").toVariant().toString();
            m_activeCallId = d.value("callId");
            m_activeOutgoing = false;
            m_endSignalSent = false;
            m_bubbleEmitted = false;
            break;
        case 408:
            path = "/api/voicecall/answerack";
            params = {{"calleeId", d.value("calleeId")}, {"callId", d.value("callId")}, {"status", 0}, {"imei", imei}};
            break;
        case 402:
            path = "/api/voicecall/answer";
            params = withExtend({{"callerId", d.value("callerId")}, {"callId", d.value("callId")},
                                 {"status", d.value("status")}, {"codec", d.value("codec").toString()},
                                 {"rtcpAddress", d.value("rtcpAddress")}, {"rtpAddress", d.value("rtpAddress")},
                                 {"session_id", d.value("session")}});
            m_activeCallId = d.value("callId");
            if (d.value("status").toInt() != 0) m_endSignalSent = true;
            break;
        case 405:
            path = "/api/voicecall/cancel";
            params = {{"callerId", d.value("toId")}, {"callId", d.value("callId")}, {"callType", d.value("callType")},
                      {"status", 0}, {"imei", imei}};
            m_endSignalSent = true;
            break;
        case 406:
            path = "/api/voicecall/logendcall";
            params = {{"call_id", d.value("callId")}, {"partner_id", d.value("partnerId")}, {"status", d.value("status")},
                      {"logdata", d.value("data")}, {"duration", d.value("duration")}, {"role", d.value("role")},
                      {"imei", imei}};
            break;
        case 409:
            path = "/api/voicecall/endcall";
            params = {{"uidTo", d.value("toId")}, {"callId", d.value("callId")}, {"status", 3}, {"imei", imei}};
            m_endSignalSent = true;
            break;
        case 12006:
            path = "/api/voicecall/conf";
            params = {{"calleeId", d.value("calleeId")}, {"callId", d.value("callId")}, {"isOnCam", d.value("isOnCam")},
                      {"isOnMic", d.value("isOnMic")}, {"data", d.value("data")}};
            break;
        case 12064:
            path = "/api/voicecall/requestsharescreen";
            params = {{"uidTo", d.value("uidTo")}, {"callId", d.value("callId")}, {"status", d.value("status")}};
            break;
        case 12433:
            path = "/api/voicecall/group/requestcall";
            params = {{"groupId", d.value("groupId")}, {"callId", d.value("callId")},
                      {"typeRequest", d.value("typeRequest")}, {"data", d.value("data")},
                      {"partners", d.value("partners")}};
            break;
        case 12434:
            path = "/api/voicecall/group/request";
            params = {{"calleeId", d.value("calleeId")}, {"callId", d.value("callId")},
                      {"callType", d.value("callType")}, {"data", d.value("data")},
                      {"session", d.value("session")}, {"partners", d.value("partners")},
                      {"groupId", d.value("groupId")}};
            break;
        case 12435:
            path = "/api/voicecall/group/adduser";
            params = {{"calleeId", d.value("calleeId")}, {"callId", d.value("callId")},
                      {"callType", d.value("callType")}, {"hostCall", d.value("hostCall")},
                      {"data", d.value("data")}, {"session", d.value("session")},
                      {"partners", d.value("partners")}, {"groupId", d.value("groupId")}};
            break;
        case 12436:
            path = "/api/voicecall/group/answer";
            params = {{"calleeId", d.value("calleeId")}, {"callId", d.value("callId")},
                      {"callType", d.value("callType")}, {"hostCall", d.value("hostCall")},
                      {"data", d.value("data")}, {"partners", d.value("partners")},
                      {"groupId", d.value("groupId")}, {"status", d.value("status")},
                      {"session", d.value("session")}};
            break;
        case 12437:
        case 12438: {
            auto putPresent = [&](const char* key) {
                const QJsonValue v = d.value(QLatin1String(key));
                if (!v.isUndefined()) params.insert(QLatin1String(key), v);
            };
            path = command == 12437 ? "/api/voicecall/group/cancel" : "/api/voicecall/group/endcall";
            m_endSignalSent = true;
            for (const char* key : {"calleeId", "callId", "callType", "hostCall", "data", "partners", "groupId"}) {
                putPresent(key);
            }
            break;
        }
        case 12439:
            path = "/api/voicecall/group/ringring";
            params = {{"calleeId", d.value("calleeId")}, {"callId", d.value("callId")},
                      {"callType", d.value("callType")}, {"hostCall", d.value("hostCall")},
                      {"data", d.value("data")}, {"partners", d.value("partners")},
                      {"session", d.value("session")}, {"status", d.value("status")}};
            break;
        case 12445:
            path = "/api/voicecall/group/state";
            params = {{"callId", d.value("callId")}, {"hostCall", d.value("hostCall")},
                      {"callType", d.value("callType")}};
            break;
        case 12446:
            path = "/api/voicecall/group/finish";
            params = {{"callId", d.value("callId")}, {"callType", d.value("callType")},
                      {"data", d.value("data")}, {"duration", d.value("duration")},
                      {"hostCall", d.value("hostCall")}, {"partners", d.value("partners")},
                      {"status", d.value("status")}, {"joinTime", d.value("joinTime")}};
            break;
        case 12447:
            path = "/api/voicecall/group/notify";
            params = {{"callId", d.value("callId")}, {"hostCall", d.value("hostCall")},
                      {"callType", d.value("callType")}, {"status", d.value("status")},
                      {"data", d.value("data")}};
            break;
        case 12097:
            path = "/api/voicecall/group/ping";
            params = {{"callId", d.value("callId")}, {"hostCall", d.value("hostCall")},
                      {"userId", d.value("userId")}, {"callstate", d.value("callState").toVariant().toInt(nullptr)},
                      {"audiostate", d.value("audioState").toVariant().toInt(nullptr)},
                      {"videostate", d.value("videoState").toVariant().toInt(nullptr)},
                      {"params", d.value("params")}};
            break;
        case 12098:
            path = "/api/voicecall/group/broadcast";
            params = {{"callId", d.value("callId")}, {"hostCall", d.value("hostCall")},
                      {"userId", d.value("userId")}, {"callstate", d.value("callState").toVariant().toInt(nullptr)},
                      {"audiostate", d.value("audioState").toVariant().toInt(nullptr)},
                      {"videostate", d.value("videoState").toVariant().toInt(nullptr)},
                      {"params", d.value("params")}};
            break;
        case 12044:
            path = "/api/voicecall/group/sharescreen";
            params = {{"userId", d.value("userId")}, {"callId", d.value("callId")},
                      {"hostCall", d.value("hostCall")}, {"status", d.value("status")},
                      {"audiostate", d.value("audioState").toVariant().toInt(nullptr)},
                      {"videostate", d.value("videoState").toVariant().toInt(nullptr)},
                      {"data", d.value("data")}};
            break;
        default:
            return;
    }

    const QJsonValue callId = d.value("callId");
    const QPointer<CallBridge> self(this);
    const quint64 gen = m_engineGen;
    m_session->apiGet("voice_call", path, params,
        [this, self, gen, command, callId, path](bool ok, const QJsonValue& data, const QString& error, int code) {
            if (!ok && (command == 405 || command == 406 || command == 409 || command == 12437 || command == 12438)) {
                qWarning() << "CallBridge signal" << command << path << "failed:" << code << error;
            }
            if (!self || gen != m_engineGen) return;
            if (ok) {
                sendToEngine({{"type", "recvSignal"}, {"command", command}, {"data", data}});
            } else if (command == 401 || command == 12433) {
                sendToEngine({{"type", "recvSignal"}, {"command", command},
                              {"data", QJsonObject{{"status", code != 0 ? code : -1}, {"msg", error}, {"callId", callId}}}});
            }
        });
}

void CallBridge::startVoiceCall(const QString& peerId, const QString& peerName, const QString& peerAvatar) {
    if (!m_activePeer.isEmpty() || !ensureEngine()) return;
    m_activePeer = peerId;
    m_activeCallId = QJsonValue();
    m_activeCallType = 0;
    m_activeOutgoing = true;
    m_activeConnected = false;
    m_endSignalSent = false;
    m_bubbleEmitted = false;
    m_isVideoMode = false;
    m_isGroupMode = false;
    m_ui = QJsonObject{{"phase", "outgoing"}, {"name", peerName}, {"avatar", peerAvatar}, {"video", false}};
    emit stateChanged();
    sendToEngine({{"type", "request"}, {"command", "makeCall"},
                  {"data", QJsonObject{{"partner", QJsonArray{QJsonObject{{"id", peerId}, {"name", peerName},
                                                                         {"avatar", peerAvatar}}}},
                                       {"type", 1}}}});
}

void CallBridge::startVideoCall(const QString& peerId, const QString& peerName, const QString& peerAvatar) {
    if (!m_activePeer.isEmpty() || !ensureEngine()) return;
    startVideoPipeline();
    m_activePeer = peerId;
    m_activeCallId = QJsonValue();
    m_activeCallType = 1;
    m_activeOutgoing = true;
    m_activeConnected = false;
    m_endSignalSent = false;
    m_bubbleEmitted = false;
    m_isVideoMode = true;
    m_isGroupMode = false;
    m_cameraError.clear();
    m_cameraOn = cameraAvailable();
    m_ui = QJsonObject{{"phase", "outgoing"}, {"name", peerName}, {"avatar", peerAvatar}, {"video", true}};
    emit stateChanged();
    emit cameraStateChanged();
    sendToEngine({{"type", "request"}, {"command", "makeCall"},
                  {"data", QJsonObject{{"partner", QJsonArray{QJsonObject{{"id", peerId}, {"name", peerName},
                                                                         {"avatar", peerAvatar}}}},
                                       {"type", 3}}}});
}

void CallBridge::startGroupCall(const QString& groupId, const QString& groupName, const QString& groupAvatar,
                               const QVariantList& members, bool isVideo) {
    if (!m_activePeer.isEmpty() || !ensureEngine()) return;
    if (isVideo) startVideoPipeline();
    m_activePeer = groupId;
    m_activeCallId = QJsonValue();
    m_activeCallType = 0;
    m_activeOutgoing = true;
    m_activeConnected = false;
    m_endSignalSent = false;
    m_bubbleEmitted = false;
    m_isVideoMode = isVideo;
    m_isGroupMode = true;
    m_cameraError.clear();
    m_cameraOn = isVideo && cameraAvailable();

    QJsonArray partners;
    for (const QVariant& item : members) {
        const QVariantMap m = item.toMap();
        const QString uid = m.value("id").toString();
        if (uid.isEmpty() || m.value("isMe").toBool()) continue;
        partners.append(QJsonObject{
            {"id", uid},
            {"name", m.value("name").toString()},
            {"avatar", m.value("avatar").toString()}
        });
    }

    m_ui = QJsonObject{
        {"phase", "outgoing"},
        {"name", groupName.isEmpty() ? QStringLiteral("Cuộc gọi nhóm") : groupName},
        {"avatar", groupAvatar},
        {"video", isVideo},
        {"group", true}
    };
    emit stateChanged();
    if (isVideo) emit cameraStateChanged();

    sendToEngine({
        {"type", "request"},
        {"command", "makeCall"},
        {"data", QJsonObject{
            {"type", isVideo ? 6 : 5},
            {"partner", partners},
            {"groupInfo", QJsonObject{
                {"id", groupId},
                {"name", groupName},
                {"avatar", groupAvatar},
                {"maxUsers", 8}
            }}
        }}
    });
}

bool CallBridge::cameraAvailable() const {
    return CameraSender::encoderAvailable() && CameraSender::cameraAvailable();
}

bool CallBridge::screenShareAvailable() const {
    return CameraSender::encoderAvailable() && CameraSender::screenAvailable();
}

CameraSender* CallBridge::cameraSender() {
    if (!m_cameraSender) {
        m_cameraSender = new CameraSender(this);
        m_cameraSender->setPreviewSink(m_localSink);
        m_cameraSender->setPreferredCameraId(m_selectedCameraId);
        connect(m_cameraSender, &CameraSender::frameEncoded, this, [this](bool key, const QByteArray& au, int w, int h) {
            if (phase() != QLatin1String("connected") || (!m_cameraOn && !m_screenShareOn)) return;
            if (!m_uiSocket || m_uiSocket->state() != QAbstractSocket::ConnectedState) return;
            if (au.isEmpty() || au.size() > 4 * 1024 * 1024) return;
            const QJsonObject head{{"action", "videoFrame"}, {"key", key}, {"screen", m_screenShareOn},
                                   {"w", w}, {"h", h}, {"bin", au.size()}};
            QByteArray pkt = QJsonDocument(head).toJson(QJsonDocument::Compact);
            pkt.append('\n');
            pkt.append(au);
            m_uiSocket->write(pkt);
        });
        connect(m_cameraSender, &CameraSender::failed, this, [this](const QString& reason) {
            m_cameraError = reason;
            if (m_cameraOn) applyCamera(false);
            if (m_screenShareOn) applyScreenShare(false);
            emit cameraStateChanged();
        });
    }
    return m_cameraSender;
}

void CallBridge::setLocalVideoSink(QVideoSink* sink) {
    m_localSink = sink;
    if (m_cameraSender) m_cameraSender->setPreviewSink(sink);
}

void CallBridge::applyCamera(bool on) {
    if (on) {
        if (m_screenShareOn) {
            m_screenShareOn = false;
            m_prevCameraOn = false;
            sendUi({{"action", "screen"}, {"on", false}});
        }
        startVideoPipeline();
        QString error;
        cameraSender()->setEncodingEnabled(phase() == QLatin1String("connected"));
        if (!cameraSender()->start(&error)) {
            m_cameraError = error;
            on = false;
        } else {
            m_cameraError.clear();
            m_cameraSender->requestKeyFrame();
        }
    } else if (m_cameraSender && !m_screenShareOn) {
        m_cameraSender->stop();
    }
    m_cameraOn = on;
    emit cameraStateChanged();
    sendUi({{"action", "camera"}, {"on", on}});
}

void CallBridge::setCamera(bool on) {
    applyCamera(on);
}

void CallBridge::applyScreenShare(bool on) {
    if (on) {
        if (phase() != QLatin1String("connected") || isGroup()) return;
        const bool wasCameraOn = m_cameraOn;
        if (!m_screenShareOn) m_prevCameraOn = wasCameraOn;
        m_cameraOn = false;
        startVideoPipeline();
        QString error;
        cameraSender()->setEncodingEnabled(true);
        if (!cameraSender()->startScreen(&error)) {
            m_cameraError = error;
            m_screenShareOn = false;
            emit cameraStateChanged();
            if (m_prevCameraOn && isVideo() && cameraAvailable()) {
                m_prevCameraOn = false;
                applyCamera(true);
            }
            return;
        }
        m_cameraError.clear();
        m_cameraSender->requestKeyFrame();
        m_screenShareOn = true;
        emit cameraStateChanged();
        sendUi({{"action", "screen"}, {"on", true}});
        sendUi({{"action", "camera"}, {"on", true}});
        return;
    }

    if (!m_screenShareOn) return;
    if (m_cameraSender) m_cameraSender->stop();
    m_screenShareOn = false;
    emit cameraStateChanged();
    sendUi({{"action", "screen"}, {"on", false}});
    const bool restoreCam = m_prevCameraOn && isVideo() && cameraAvailable() && phase() == QLatin1String("connected");
    m_prevCameraOn = false;
    if (restoreCam) {
        applyCamera(true);
    } else {
        sendUi({{"action", "camera"}, {"on", false}});
    }
}

void CallBridge::setScreenShare(bool on) {
    applyScreenShare(on);
}

void CallBridge::handleVoipControl(const QJsonObject& rawContent) {
    QJsonObject content = rawContent;
    if (content.value("data").isString()) {
        content["data"] = QJsonDocument::fromJson(content.value("data").toString().toUtf8()).object();
    }
    const QString act = content.value("act").toString();
    qint64 ts = content.value("data").toObject().value("ts").toVariant().toLongLong();
    if (ts > 0 && ts < 100000000000LL) ts *= 1000;

    const bool incoming = act == "request" || act == "group_request";
    if (incoming && ts > 0 && QDateTime::currentMSecsSinceEpoch() - ts > 60000) return;
    if (incoming) {
        content["inCallStatus"] = m_activePeer.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue("zalo");
    }
    if (!m_process && !incoming) return;
    if (!ensureEngine()) return;
    sendToEngine({{"type", "control"}, {"data", content}});
}

void CallBridge::drainUiBuffer() {
    for (;;) {
        if (m_uiSkipBytes > 0) {
            if (m_uiBuffer.size() < m_uiSkipBytes) return;
            const QByteArray payload = m_uiBuffer.left(m_uiSkipBytes);
            m_uiBuffer.remove(0, m_uiSkipBytes);
            m_uiSkipBytes = 0;
            if (m_lastVideoSrc == 0) {
                if (!m_lastVideoKey && m_pendingVideoFrames.load(std::memory_order_relaxed) >= 4) {
                    continue;
                }
                startVideoPipeline();
                if (m_videoDecoder) m_pendingVideoFrames.fetch_add(1, std::memory_order_relaxed);
                emit videoFrameReceived(m_lastVideoKey, m_lastVideoCodec, payload, m_lastVideoSrc);
            }
        }
        const int nl = m_uiBuffer.indexOf('\n');
        if (nl < 0) return;
        const QByteArray line = m_uiBuffer.left(nl);
        m_uiBuffer.remove(0, nl + 1);
        if (line.trimmed().isEmpty()) continue;
        const QJsonObject head = QJsonDocument::fromJson(line).object();
        if (head.contains("bin")) {
            const qint64 bin = head.value("bin").toVariant().toLongLong();
            if (bin <= 0 || bin > 4 * 1024 * 1024) {
                m_uiBuffer.clear();
                m_uiSkipBytes = 0;
                return;
            }
            m_uiSkipBytes = bin;
            m_lastVideoKey = head.value("key").toBool();
            m_lastVideoCodec = head.value("codec").toString();
            m_lastVideoSrc = head.value("src").toInt();
            continue;
        }
        onUiLine(line);
    }
}

void CallBridge::sendUi(const QJsonObject& action) {
    if (!m_uiSocket || m_uiSocket->state() != QAbstractSocket::ConnectedState) return;
    m_uiSocket->write(QJsonDocument(action).toJson(QJsonDocument::Compact) + '\n');
    m_uiSocket->flush();
}

void CallBridge::onUiLine(const QByteArray& line) {
    const QJsonObject msg = QJsonDocument::fromJson(line).object();
    const QString type = msg.value("type").toString();
    if (type == "state") {
        const QString previous = phase();
        m_ui = msg;
        if (phase() == QLatin1String("connected")) m_activeConnected = true;
        if (m_cameraSender) {
            const bool connected = phase() == QLatin1String("connected");
            m_cameraSender->setEncodingEnabled(connected);
            if (connected && previous != QLatin1String("connected") && (m_cameraOn || m_screenShareOn)) {
                m_cameraSender->requestKeyFrame();
            }
        }
        if (msg.contains("video")) {
            m_isVideoMode = msg.value("video").toBool();
            if (m_isVideoMode && !m_cameraOffAnnounced && msg.value("phase").toString() != QLatin1String("incoming")) {
                m_cameraOffAnnounced = true;
                applyCamera(m_cameraOn);
            }
        }
        const QJsonObject enc = msg.value("cameraEncode").toObject();
        if (!enc.isEmpty() && m_cameraSender) {
            m_cameraSender->setProfile(QSize(enc.value("width").toInt(), enc.value("height").toInt()),
                                       enc.value("fps").toInt(15), enc.value("bitrate").toInt(300000));
        }
        const QJsonObject scrEnc = msg.value("screenEncode").toObject();
        if (!scrEnc.isEmpty() && m_cameraSender) {
            m_cameraSender->setScreenProfile(1280, scrEnc.value("maxShort").toInt(720),
                                             scrEnc.value("fps").toInt(10),
                                             scrEnc.value("bitrate").toInt(1500000));
        }
        if (msg.value("phase").toString() == "incoming" && previous != "incoming") {
            emit incomingCall(msg.value("name").toString());
        }
        bool micChanged = false;
        if (msg.contains("micVolume") && msg.value("micVolume").isDouble()) {
            const int vol = std::clamp(msg.value("micVolume").toInt(100), 0, 100);
            if (m_micVolume != vol) {
                m_micVolume = vol;
                micChanged = true;
            }
        }
        if (msg.contains("micAutoGain") && msg.value("micAutoGain").isBool()) {
            const bool ag = msg.value("micAutoGain").toBool(true);
            if (m_micAutoGain != ag) {
                m_micAutoGain = ag;
                micChanged = true;
            }
        }
        if (micChanged) emit micStateChanged();
        emit stateChanged();
    } else if (type == "micLevel") {
        const int lvl = std::clamp(msg.value("level").toInt(60), 0, 127);
        if (m_micLevel != lvl) {
            m_micLevel = lvl;
            emit micStateChanged();
        }
    } else if (type == "keyframe") {
        if (m_cameraSender) m_cameraSender->requestKeyFrame();
    } else if (type == "close") {
        if (m_hangupWatchdog) m_hangupWatchdog->stop();
        m_activePeer.clear();
        m_ui = QJsonObject{{"phase", "ended"}, {"name", peerName()}, {"avatar", peerAvatar()}, {"group", isGroup()},
                           {"text", msg.value("text")}, {"statusCode", msg.value("statusCode")},
                           {"statusParams", msg.value("statusParams")}};
        m_isVideoMode = false;
        if (m_cameraSender) m_cameraSender->stop();
        m_cameraOn = false;
        m_prevCameraOn = false;
        m_screenShareOn = false;
        m_micLevel = 60;
        m_micVolume = 100;
        m_micAutoGain = true;
        emit micStateChanged();
        emit stateChanged();
        emit cameraStateChanged();
        if (m_process && m_idleShutdownTimer) m_idleShutdownTimer->start();
    }
}

void CallBridge::resetUi() {
    if (m_hangupWatchdog) m_hangupWatchdog->stop();
    emit peerVideoResetRequested();
    if (m_peerSink) m_peerSink->setVideoFrame(QVideoFrame());
    m_pendingVideoFrames.store(0, std::memory_order_relaxed);
    if (m_peerVideoIdle) m_peerVideoIdle->stop();
    if (m_peerVideoActive) {
        m_peerVideoActive = false;
        emit peerVideoChanged();
    }
    m_ui = {};
    m_activePeer.clear();
    m_activeCallId = QJsonValue();
    m_activeCallType = 0;
    m_activeConnected = false;
    m_endSignalSent = false;
    m_bubbleEmitted = false;
    m_cameraOffAnnounced = false;
    m_isVideoMode = false;
    if (m_cameraSender) m_cameraSender->stop();
    m_cameraOn = false;
    m_prevCameraOn = false;
    m_screenShareOn = false;
    m_micLevel = 60;
    m_micVolume = 100;
    m_micAutoGain = true;
    m_uiSkipBytes = 0;
    emit micStateChanged();
    emit stateChanged();
    emit cameraStateChanged();
}

void CallBridge::refreshDevicesAsync() {
    if (!Platform::MultimediaSupport::available()) return;
    if (m_devicesScanning.exchange(true)) return;
    const QPointer<CallBridge> self(this);
    QMetaObject::invokeMethod(this, [self]() {
        if (!self) return;
        QVariantList ins;
        for (const QAudioDevice& dev : QMediaDevices::audioInputs()) {
            const QString id = QString::fromUtf8(dev.id());
            if (id.isEmpty() || id.endsWith(QLatin1String(".monitor")) || id.startsWith(QLatin1String("zcall_ec_"))
                || id.startsWith(QLatin1String("raop_sink."))) {
                continue;
            }
            const QString name = dev.description().isEmpty() ? id : dev.description();
            ins.append(QVariantMap{{"id", id}, {"name", name}});
        }
        QVariantList outs;
        for (const QAudioDevice& dev : QMediaDevices::audioOutputs()) {
            const QString id = QString::fromUtf8(dev.id());
            if (id.isEmpty() || id.endsWith(QLatin1String(".monitor")) || id.startsWith(QLatin1String("zcall_ec_"))
                || id.startsWith(QLatin1String("raop_sink."))) {
                continue;
            }
            const QString name = dev.description().isEmpty() ? id : dev.description();
            outs.append(QVariantMap{{"id", id}, {"name", name}});
        }
        QVariantList nonIr;
        QVariantList allCams;
        for (const QCameraDevice& dev : QMediaDevices::videoInputs()) {
            const QString id = QString::fromUtf8(dev.id());
            const QString name = dev.description().isEmpty() ? id : dev.description();
            const QVariantMap item{{"id", id}, {"name", name}};
            allCams.append(item);
            if (!Media::MediaUtils::isIrCamera(name)) nonIr.append(item);
        }
        const QVariantList cams = nonIr.isEmpty() ? allCams : nonIr;

        const bool changed = !self->m_devicesCached
                             || self->m_cachedAudioInputs != ins
                             || self->m_cachedAudioOutputs != outs
                             || self->m_cachedCameras != cams;
        self->m_cachedAudioInputs = ins;
        self->m_cachedAudioOutputs = outs;
        self->m_cachedCameras = cams;
        self->m_devicesCached = true;
        self->m_devicesScanning.store(false);
        if (changed) emit self->devicesChanged();
    }, Qt::QueuedConnection);
}

QVariantList CallBridge::audioInputDevices() const {
    if (!Platform::MultimediaSupport::available()) return {};
    if (!m_devicesCached) const_cast<CallBridge*>(this)->refreshDevicesAsync();
    return m_cachedAudioInputs;
}

QVariantList CallBridge::audioOutputDevices() const {
    if (!Platform::MultimediaSupport::available()) return {};
    if (!m_devicesCached) const_cast<CallBridge*>(this)->refreshDevicesAsync();
    return m_cachedAudioOutputs;
}

QVariantList CallBridge::cameraDevices() const {
    if (!Platform::MultimediaSupport::available()) return {};
    if (!m_devicesCached) const_cast<CallBridge*>(this)->refreshDevicesAsync();
    return m_cachedCameras;
}

void CallBridge::selectAudioDevice(const QString& kind, const QString& id) {
    if (kind != QLatin1String("mic") && kind != QLatin1String("speaker")) return;
    QSettings settings(QStringLiteral("Zalos"), QStringLiteral("ZalosApp"));
    if (kind == QLatin1String("mic")) {
        m_selectedMicId = id;
        settings.setValue(QStringLiteral("call/micDevice"), id);
    } else {
        m_selectedSpeakerId = id;
        settings.setValue(QStringLiteral("call/speakerDevice"), id);
    }
    emit devicesChanged();
    sendUi({{"action", "device"}, {"kind", kind}, {"id", id}});
}

void CallBridge::selectCameraDevice(const QString& id) {
    m_selectedCameraId = id;
    QSettings settings(QStringLiteral("Zalos"), QStringLiteral("ZalosApp"));
    settings.setValue(QStringLiteral("call/cameraDevice"), id);
    emit devicesChanged();
    if (m_cameraSender) m_cameraSender->setPreferredCameraId(id);
}

void CallBridge::setMicVolume(int pct) {
    const int clamped = std::clamp(pct, 0, 100);
    bool changed = false;
    if (m_micVolume != clamped) {
        m_micVolume = clamped;
        changed = true;
    }
    if (m_micAutoGain) {
        m_micAutoGain = false;
        changed = true;
    }
    if (changed) emit micStateChanged();
    sendUi({{"action", "micVolume"}, {"value", clamped}});
}

void CallBridge::setMicAutoGain(bool on) {
    if (m_micAutoGain != on) {
        m_micAutoGain = on;
        emit micStateChanged();
    }
    sendUi({{"action", "micAutoGain"}, {"on", on}});
}

void CallBridge::accept(bool camStartOff) {
    const bool cam = !camStartOff && m_isVideoMode && cameraAvailable();
    if (m_isVideoMode) startVideoPipeline();
    m_cameraOn = cam;
    m_cameraOffAnnounced = false;
    sendUi({{"action", "accept"}, {"camStartOff", camStartOff || !cam}});
    emit cameraStateChanged();
}
void CallBridge::reject() { sendUi({{"action", "reject"}}); }

void CallBridge::hangup() {
    if (phase() == QLatin1String("ended")) {
        if (m_hangupWatchdog) m_hangupWatchdog->stop();
        resetUi();
        return;
    }
    if (m_uiSocket && m_uiSocket->state() == QAbstractSocket::ConnectedState) {
        sendUi({{"action", "hangup"}});
        if (m_hangupWatchdog) m_hangupWatchdog->start();
        return;
    }
    if (m_process && m_engineReady && m_sendSocket && m_sendSocket->state() == QAbstractSocket::ConnectedState) {
        sendToEngine({{"type", "request"}, {"command", "endCall"}, {"data", QJsonObject()}});
        m_sendSocket->flush();
        m_ui = QJsonObject{{"phase", "ended"}, {"name", peerName()}, {"avatar", peerAvatar()}, {"group", isGroup()},
                           {"statusCode", "ended"}};
        emit stateChanged();
        if (m_idleShutdownTimer) m_idleShutdownTimer->start();
        return;
    }
    if (m_process) shutdown();
    resetUi();
}

void CallBridge::setMuted(bool muted) {
    sendUi({{"action", "mute"}, {"on", muted}});
    m_ui["muted"] = muted;
    emit stateChanged();
}

void CallBridge::setSpeakerOff(bool off) {
    sendUi({{"action", "speaker"}, {"on", off}});
    m_ui["speakerOff"] = off;
    emit stateChanged();
}

}
