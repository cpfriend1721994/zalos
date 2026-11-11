#include "call_bridge.h"

#include "h264_codec.h"
#include "peer_video.h"
#include "network/zalo_crypto.h"
#include "network/zalo_session.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QUrl>
#include <QVersionNumber>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QVideoFrame>
#include <QVideoSink>

namespace Zalos::Core::Call {

namespace {

constexpr int kMaxHexPerFrame = 4000;

QString randomToken() {
    return "zcall-" + QUuid::createUuid().toString(QUuid::Id128);
}

}

CallBridge::CallBridge(Network::ZaloSession* session, QObject* parent)
    : QObject(parent), m_session(session) {
    refreshAvailability();
    startVideoPipeline();
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
    const QByteArray id = QByteArray::number(QDateTime::currentMSecsSinceEpoch());
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
            QStringList& parts = (*chunks)[id];
            if (parts.size() < total) parts.resize(total);
            if (index < 0 || index >= total) continue;
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

    auto acceptOn = [this](QTcpServer* server, int role) {
        connect(server, &QTcpServer::newConnection, this, [this, server, role]() {
            while (QTcpSocket* sock = server->nextPendingConnection()) {
                auto* pending = new QByteArray;
                connect(sock, &QTcpSocket::readyRead, this, [this, sock, pending, role]() {
                    if (!sock->property("authed").toBool()) {
                        pending->append(sock->readAll());
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
                        else { m_uiSocket = sock; m_uiBuffer = rest; }
                    } else if (role == 0) {
                        m_recvBuffer += sock->readAll();
                    } else if (role == 2) {
                        m_uiBuffer += sock->readAll();
                    } else {
                        sock->readAll();
                    }
                    if (role == 0) {
                        for (const QJsonObject& f : decodeFrames(m_recvBuffer, m_key, &m_chunks)) onEngineFrame(f);
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

    const QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/logs";
    QDir().mkpath(logDir);

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    connect(m_process, &QProcess::finished, this, [this]() {
        const bool wasInCall = !m_activePeer.isEmpty();
        shutdown();
        if (wasInCall) {
            m_ui = QJsonObject{{"phase", "ended"}};
            emit stateChanged();
        }
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        shutdown();
        m_ui = QJsonObject{{"phase", "ended"}, {"name", peerName()}, {"avatar", peerAvatar()}};
        emit stateChanged();
    });
    const QStringList engineArgs{"--token", m_token,
                                 "--recv", QString::number(m_recvServer->serverPort()),
                                 "--send", QString::number(m_sendServer->serverPort()),
                                 "--media", "zrtc", "--send-udp", "--group",
                                 "--key-file", m_keyFilePath,
                                 "--log-dir", logDir,
                                 "--ui-port", QString::number(m_uiServer->serverPort()),
                                 "--ui-token", m_uiToken};
    m_process->start(m_enginePath, engineArgs);
    return true;
}

void CallBridge::shutdown() {
    if (m_process) {
        QProcess* p = m_process;
        m_process = nullptr;
        p->disconnect(this);
        if (p->state() != QProcess::NotRunning) {
            p->terminate();
            if (!p->waitForFinished(1500)) p->kill();
        }
        p->deleteLater();
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
    m_activePeer.clear();
    m_isVideoMode = false;
    m_isGroupMode = false;
    if (m_cameraSender) m_cameraSender->stop();
    m_cameraOn = false;
    m_cameraOffAnnounced = false;
    m_uiSkipBytes = 0;
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
        } else if (command == "bubble") {
            emit callFinished(data.value("partnerId").toVariant().toString(), data.value("role").toBool(),
                              data.value("duration").toInt());
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
            m_activeOutgoing = true;
            break;
        case 416:
            path = "/api/voicecall/request";
            params = withExtend({{"calleeId", d.value("calleeId")}, {"rtcpAddress", d.value("rtcpAddress")},
                                 {"rtpAddress", d.value("rtpAddress")}, {"codec", d.value("codec")},
                                 {"session", d.value("session")}, {"callId", d.value("callId")}, {"imei", imei}});
            break;
        case 407:
            path = "/api/voicecall/ringring";
            params = {{"callerId", d.value("callerId")}, {"callId", d.value("callId")}, {"status", 0}, {"imei", imei}};
            m_activePeer = d.value("callerId").toVariant().toString();
            m_activeOutgoing = false;
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
            break;
        case 405:
            path = "/api/voicecall/cancel";
            params = {{"callerId", d.value("toId")}, {"callId", d.value("callId")}, {"callType", d.value("callType")},
                      {"status", 0}, {"imei", imei}};
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
            break;
        case 12006:
            path = "/api/voicecall/conf";
            params = {{"calleeId", d.value("calleeId")}, {"callId", d.value("callId")}, {"isOnCam", d.value("isOnCam")},
                      {"isOnMic", d.value("isOnMic")}, {"data", d.value("data")}};
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
            path = "/api/voicecall/group/cancel";
            params = {{"calleeId", d.value("calleeId")}, {"callId", d.value("callId")},
                      {"callType", d.value("callType")}, {"hostCall", d.value("hostCall")},
                      {"data", d.value("data")}, {"partners", d.value("partners")},
                      {"groupId", d.value("groupId")}};
            break;
        case 12438:
            path = "/api/voicecall/group/endcall";
            params = {{"calleeId", d.value("calleeId")}, {"callId", d.value("callId")},
                      {"callType", d.value("callType")}, {"hostCall", d.value("hostCall")},
                      {"data", d.value("data")}, {"partners", d.value("partners")},
                      {"groupId", d.value("groupId")}};
            break;
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
    m_session->apiGet("voice_call", path, params,
        [this, command, callId](bool ok, const QJsonValue& data, const QString& error, int code) {
            if (ok) {
                sendToEngine({{"type", "recvSignal"}, {"command", command}, {"data", data}});
            } else if (command == 401) {
                sendToEngine({{"type", "recvSignal"}, {"command", command},
                              {"data", QJsonObject{{"status", code != 0 ? code : -1}, {"msg", error}, {"callId", callId}}}});
            }
        });
}

void CallBridge::startVoiceCall(const QString& peerId, const QString& peerName, const QString& peerAvatar) {
    if (!m_activePeer.isEmpty() || !ensureEngine()) return;
    m_activePeer = peerId;
    m_activeOutgoing = true;
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
    m_activePeer = peerId;
    m_activeOutgoing = true;
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
    m_activePeer = groupId;
    m_activeOutgoing = true;
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

CameraSender* CallBridge::cameraSender() {
    if (!m_cameraSender) {
        m_cameraSender = new CameraSender(this);
        m_cameraSender->setPreviewSink(m_localSink);
        connect(m_cameraSender, &CameraSender::frameEncoded, this, [this](bool key, const QByteArray& au, int w, int h) {
            if (phase() != QLatin1String("connected") || !m_cameraOn) return;
            sendUi({{"action", "videoFrame"}, {"key", key}, {"data", QString::fromLatin1(au.toBase64())},
                    {"screen", false}, {"w", w}, {"h", h}});
        });
        connect(m_cameraSender, &CameraSender::failed, this, [this](const QString& reason) {
            m_cameraError = reason;
            if (m_cameraOn) applyCamera(false);
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
        QString error;
        if (!cameraSender()->start(&error)) {
            m_cameraError = error;
            on = false;
        } else {
            m_cameraError.clear();
            m_cameraSender->requestKeyFrame();
        }
    } else if (m_cameraSender) {
        m_cameraSender->stop();
    }
    m_cameraOn = on;
    emit cameraStateChanged();
    sendUi({{"action", "camera"}, {"on", on}});
}

void CallBridge::setCamera(bool on) {
    applyCamera(on);
}

void CallBridge::handleVoipControl(const QJsonObject& rawContent) {
    QJsonObject content = rawContent;
    if (content.value("data").isString()) {
        content["data"] = QJsonDocument::fromJson(content.value("data").toString().toUtf8()).object();
    }
    const QString act = content.value("act").toString();
    const qint64 ts = content.value("data").toObject().value("ts").toVariant().toLongLong();
    if (ts > 0 && QDateTime::currentMSecsSinceEpoch() - ts > 60000) return;

    const bool incoming = act == "request" || act == "group_request";
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
            emit videoFrameReceived(m_lastVideoKey, m_lastVideoCodec, payload, m_lastVideoSrc);
        }
        const int nl = m_uiBuffer.indexOf('\n');
        if (nl < 0) return;
        const QByteArray line = m_uiBuffer.left(nl);
        m_uiBuffer.remove(0, nl + 1);
        if (line.trimmed().isEmpty()) continue;
        const QJsonObject head = QJsonDocument::fromJson(line).object();
        if (head.contains("bin")) {
            m_uiSkipBytes = head.value("bin").toVariant().toLongLong();
            m_lastVideoKey = head.value("key").toBool();
            m_lastVideoCodec = head.value("codec").toString();
            m_lastVideoSrc = head.value("src").toInt();
            continue;
        }
        onUiLine(line);
    }
}

void CallBridge::sendUi(const QJsonObject& action) {
    if (m_uiSocket) m_uiSocket->write(QJsonDocument(action).toJson(QJsonDocument::Compact) + '\n');
}

void CallBridge::onUiLine(const QByteArray& line) {
    const QJsonObject msg = QJsonDocument::fromJson(line).object();
    const QString type = msg.value("type").toString();
    if (type == "state") {
        const QString previous = phase();
        m_ui = msg;
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
        if (msg.value("phase").toString() == "incoming" && previous != "incoming") {
            emit incomingCall(msg.value("name").toString());
        }
        emit stateChanged();
    } else if (type == "keyframe") {
        if (m_cameraSender) m_cameraSender->requestKeyFrame();
    } else if (type == "close") {
        m_ui = QJsonObject{{"phase", "ended"}, {"name", peerName()}, {"avatar", peerAvatar()},
                           {"text", msg.value("text")}};
        m_isVideoMode = false;
        emit stateChanged();
    }
}

void CallBridge::resetUi() {
    emit peerVideoResetRequested();
    if (m_peerSink) m_peerSink->setVideoFrame(QVideoFrame());
    if (m_peerVideoIdle) m_peerVideoIdle->stop();
    if (m_peerVideoActive) {
        m_peerVideoActive = false;
        emit peerVideoChanged();
    }
    m_ui = {};
    m_activePeer.clear();
    m_cameraOffAnnounced = false;
    m_isVideoMode = false;
    if (m_cameraSender) m_cameraSender->stop();
    m_cameraOn = false;
    m_uiSkipBytes = 0;
    emit stateChanged();
    emit cameraStateChanged();
}

void CallBridge::accept(bool camStartOff) {
    const bool cam = !camStartOff && m_isVideoMode && cameraAvailable();
    m_cameraOn = cam;
    m_cameraOffAnnounced = false;
    sendUi({{"action", "accept"}, {"camStartOff", camStartOff || !cam}});
    emit cameraStateChanged();
}
void CallBridge::reject() { sendUi({{"action", "reject"}}); }

void CallBridge::hangup() {
    if (phase() == "ended" || !m_uiSocket) {
        resetUi();
        return;
    }
    sendUi({{"action", "hangup"}});
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
