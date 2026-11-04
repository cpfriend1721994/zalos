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
#include <QSettings>
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

QStringList candidateRoots() {
    QStringList roots;
    const QString env = qEnvironmentVariable("ZALOS_ZCALL_DIR");
    if (!env.isEmpty()) roots << env;
    const QString override = QSettings().value("calls/zcallDir").toString();
    if (!override.isEmpty()) roots << override;
    roots << CallBridge::bundledEngineRoot();
#ifdef ZALOS_SOURCE_DIR
    roots << QStringLiteral(ZALOS_SOURCE_DIR) + "/third_party/zcall-native";
#endif
    return roots;
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

void CallBridge::refreshAvailability() {
    QString reason;
    m_enginePath = findEngine();
    QString overridePath;
    const QByteArray overrideKey = findTransportKey(&overridePath);
    if (overrideKey.size() == 16) {
        m_key = overrideKey;
        m_mainJsPath = overridePath;
    } else if (!m_keyFile) {
        QByteArray key(16, '\0');
        QRandomGenerator::system()->generate(reinterpret_cast<quint32*>(key.data()),
                                             reinterpret_cast<quint32*>(key.data() + key.size()));
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        m_keyFile = std::make_unique<QTemporaryFile>((dir.isEmpty() ? QDir::tempPath() : dir)
                                                     + "/zalos-callv2-XXXXXX.js");
        if (writeKeyFile(m_keyFile.get(), key)) {
            m_key = key;
            m_mainJsPath = m_keyFile->fileName();
        }
    }
    if (findNode().isEmpty()) reason = "node_missing";
    else if (m_enginePath.isEmpty()) reason = "engine_missing";
    else if (m_key.size() != 16) reason = "key_missing";
    else if (QStandardPaths::findExecutable("python3").isEmpty()) reason = "python_missing";
    if (reason != m_unavailableReason) {
        m_unavailableReason = reason;
        emit availabilityChanged();
    }
}

QString CallBridge::engineIn(const QString& folder) {
    if (folder.isEmpty()) return {};
    for (const QString& rel : {QStringLiteral("native-engine/zcall-native.js"), QStringLiteral("zcall-native.js"),
                               QStringLiteral("zcall-native/native-engine/zcall-native.js")}) {
        const QString path = QDir::cleanPath(folder + "/" + rel);
        if (QFile::exists(path)) return path;
    }
    return {};
}

QString CallBridge::bundledEngineRoot() {
    return QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../share/zalos/zcall-native");
}

QString CallBridge::findEngine() {
    for (const QString& root : candidateRoots()) {
        const QString path = engineIn(root);
        if (!path.isEmpty()) return path;
    }
    return {};
}

bool CallBridge::setEngineDirectory(const QString& urlOrPath) {
    if (urlOrPath.isEmpty()) {
        QSettings().remove("calls/zcallDir");
        refreshAvailability();
        emit availabilityChanged();
        return true;
    }
    const QUrl url(urlOrPath);
    const QString folder = url.isLocalFile() ? url.toLocalFile() : urlOrPath;
    const QString engine = engineIn(folder);
    if (engine.isEmpty()) return false;
    QSettings().setValue("calls/zcallDir", QFileInfo(engine).dir().absolutePath().section('/', 0, -2));
    refreshAvailability();
    emit availabilityChanged();
    return true;
}

bool CallBridge::engineIsBundled() const {
    return !m_enginePath.isEmpty() && engineDirectory() == bundledEngineRoot();
}

QString CallBridge::engineDirectory() const {
    return m_enginePath.isEmpty() ? QString() : QFileInfo(m_enginePath).dir().absolutePath().section('/', 0, -2);
}

QByteArray CallBridge::findTransportKey(QString* mainJsPath) {
    const QString path = qEnvironmentVariable("ZALO_MAIN_JS");
    if (path.isEmpty()) return {};
    static const QRegularExpression re(QStringLiteral(R"re(callV2TransportAesKey:"([A-Za-z0-9+/=]+)")re"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QRegularExpressionMatch m = re.match(QString::fromUtf8(file.readAll()));
    if (!m.hasMatch()) return {};
    const QByteArray key = QByteArray::fromBase64(m.captured(1).toLatin1());
    if (key.size() != 16) return {};
    if (mainJsPath) *mainJsPath = QDir::cleanPath(path);
    return key;
}

bool CallBridge::writeKeyFile(QTemporaryFile* file, const QByteArray& key) {
    if (!file || key.size() != 16) return false;
    if (!file->isOpen() && !file->open()) return false;
    file->setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file->resize(0);
    file->seek(0);
    const QByteArray line = "callV2TransportAesKey:\"" + key.toBase64() + "\"\n";
    const bool ok = file->write(line) == line.size() && file->flush();
    return ok;
}

QString CallBridge::findNode() {
    for (const char* name : {"node", "nodejs"}) {
        const QString path = QStandardPaths::findExecutable(QString::fromLatin1(name));
        if (!path.isEmpty()) return path;
    }
    const QString home = QDir::homePath();
    QStringList candidates{home + "/.bun/bin/node", home + "/.volta/bin/node",
                           home + "/.local/share/fnm/aliases/default/bin/node", home + "/.local/bin/node",
                           "/usr/local/bin/node"};
    QStringList nvm = QDir(home + "/.nvm/versions/node").entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    std::sort(nvm.begin(), nvm.end(), [](const QString& a, const QString& b) {
        return QVersionNumber::fromString(a.mid(1)) > QVersionNumber::fromString(b.mid(1));
    });
    for (const QString& v : nvm) candidates << home + "/.nvm/versions/node/" + v + "/bin/node";
    for (const QString& path : candidates) {
        if (QFileInfo(path).isExecutable()) return path;
    }
    return {};
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
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("ZALO_MAIN_JS", m_mainJsPath);
    env.insert("ZCALL_LOG_DIR", logDir);
    env.insert("ZCALL_UI_PORT", QString::number(m_uiServer->serverPort()));
    env.insert("ZCALL_UI_TOKEN", m_uiToken);
    env.insert("ZCALL_GROUP", "0");

    m_process = new QProcess(this);
    m_process->setProcessEnvironment(env);
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
    m_process->start(findNode(), {m_enginePath, "--token", m_token,
                                  "--recv", QString::number(m_recvServer->serverPort()),
                                  "--send", QString::number(m_sendServer->serverPort()),
                                  "--media", "zrtc", "--send-udp"});
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
    if (act.startsWith("group_")) return;
    const qint64 ts = content.value("data").toObject().value("ts").toVariant().toLongLong();
    if (ts > 0 && QDateTime::currentMSecsSinceEpoch() - ts > 60000) return;

    const bool incoming = act == "request";
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
    m_cameraOffAnnounced = false;
    m_isVideoMode = false;
    if (m_cameraSender) m_cameraSender->stop();
    m_cameraOn = false;
    m_uiSkipBytes = 0;
    emit stateChanged();
    emit cameraStateChanged();
}

void CallBridge::accept() {
    const bool cam = m_isVideoMode && cameraAvailable();
    m_cameraOn = cam;
    m_cameraOffAnnounced = false;
    sendUi({{"action", "accept"}, {"camStartOff", !cam}});
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
