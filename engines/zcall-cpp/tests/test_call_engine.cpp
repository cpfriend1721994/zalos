// SPDX-License-Identifier: BSD-3-Clause
#include "wire/bytes.h"
#include "wire/ipc_crypto.h"
#include "wire/zrtp_packet.h"

#include <QFile>
#include <QUdpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

using namespace zcall;

namespace {
const char* kToken = "TKN-test";
const char* kKeyB64 = "AAECAwQFBgcICQoLDA0ODw==";

struct EngineRun {
    QTcpServer recv;
    QTcpServer send;
    QProcess proc;
    QTcpSocket* out{nullptr};
    QByteArray key;
    QList<QJsonObject> frames;
    std::unique_ptr<FrameParser> parser;

    bool hasUpdate(const QString& command) const {
        for (const QJsonObject& f : frames)
            if (f.value("type").toString() == QLatin1String("update") && f.value("command").toString() == command) return true;
        return false;
    }
    bool hasSignal(int cmd) const {
        for (const QJsonObject& f : frames)
            if (f.value("type").toString() == QLatin1String("sendSignal") && f.value("command").toInt() == cmd) return true;
        return false;
    }
    void write(const QJsonObject& frame) {
        out->write(ipcEncrypt(QJsonDocument(frame).toJson(QJsonDocument::Compact), key) + '$');
        out->flush();
    }
};
}

class TestCallEngine : public QObject {
    Q_OBJECT

private:
    bool sawSignal(int cmd) const {
        for (const QJsonObject& f : m_frames)
            if (f.value("type").toString() == QLatin1String("sendSignal") && f.value("command").toInt() == cmd) return true;
        return false;
    }
    bool sawUpdate(const QString& command) const {
        for (const QJsonObject& f : m_frames)
            if (f.value("type").toString() == QLatin1String("update") && f.value("command").toString() == command) return true;
        return false;
    }
    void sendToEngine(const QJsonObject& frame) {
        m_engineSend->write(ipcEncrypt(QJsonDocument(frame).toJson(QJsonDocument::Compact), m_key) + '$');
        m_engineSend->flush();
    }

    QTemporaryDir m_dir;
    QTcpServer m_recvSrv;
    QTcpServer m_sendSrv;
    QProcess m_proc;
    QTcpSocket* m_engineSend{nullptr};
    QByteArray m_key;
    QList<QJsonObject> m_frames;
    QList<QByteArray> m_tokens;
    std::unique_ptr<FrameParser> m_parser;

private Q_SLOTS:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        m_key = QByteArray::fromBase64(QByteArray(kKeyB64));
        QFile keyFile(m_dir.filePath(QStringLiteral("transport.key")));
        QVERIFY(keyFile.open(QIODevice::WriteOnly));
        keyFile.write(kKeyB64);
        keyFile.close();

        m_parser = std::make_unique<FrameParser>(m_key, true, [this](const IpcFrame& f) {
            if (f.isToken) m_tokens.append(f.token);
            if (!f.isToken && !f.isError && f.json.isObject()) m_frames.append(f.json.toObject());
        });
        QVERIFY(m_recvSrv.listen(QHostAddress::LocalHost, 0));
        QVERIFY(m_sendSrv.listen(QHostAddress::LocalHost, 0));
        connect(&m_recvSrv, &QTcpServer::newConnection, this, [this] {
            QTcpSocket* s = m_recvSrv.nextPendingConnection();
            connect(s, &QTcpSocket::readyRead, this, [this, s] { m_parser->push(s->readAll()); });
        });
        connect(&m_sendSrv, &QTcpServer::newConnection, this, [this] {
            m_engineSend = m_sendSrv.nextPendingConnection();
            m_engineSend->setParent(this);
        });

        m_proc.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("ZCALL_TOKEN"), QLatin1String(kToken));
        m_proc.setProcessEnvironment(env);
        m_proc.start(QStringLiteral(ZCALL_ENGINE_BIN),
                     {QStringLiteral("--key-file"), m_dir.filePath(QStringLiteral("transport.key")),
                      QStringLiteral("--recv"), QString::number(m_recvSrv.serverPort()),
                      QStringLiteral("--send"), QString::number(m_sendSrv.serverPort()),
                      QStringLiteral("--media"), QStringLiteral("stub"),
                      QStringLiteral("--no-window")});
        QVERIFY(m_proc.waitForStarted(5000));
    }

    bool frameSince(int from, int cmd, const QString& type) const {
        for (int i = from; i < m_frames.size(); ++i)
            if (m_frames.at(i).value("type").toString() == type && m_frames.at(i).value("command").toInt() == cmd) return true;
        return false;
    }

    void tokenFromEnvironmentAuthenticates() {
        QTRY_VERIFY_WITH_TIMEOUT(!m_tokens.isEmpty(), 5000);
        QCOMPARE(m_tokens.first(), QByteArray(kToken));
        QVERIFY(!m_proc.arguments().contains(QLatin1String(kToken)));
    }

    void staleSignalReplyIgnored() {
        QTRY_VERIFY_WITH_TIMEOUT(m_engineSend != nullptr, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(sawUpdate(QStringLiteral("native-ready")), 10000);
        const int base = m_frames.size();

        sendToEngine(QJsonObject{{"type", "request"}, {"command", "makeCall"},
                                 {"data", QJsonObject{{"type", 1}, {"partner", QJsonArray{QJsonObject{{"id", "123456"}, {"name", "Người thử"}}}}}}});
        QTRY_VERIFY_WITH_TIMEOUT(frameSince(base, 401, QStringLiteral("sendSignal")), 5000);
        double callId = 0;
        for (int i = base; i < m_frames.size(); ++i) {
            if (m_frames.at(i).value("command").toInt() == 401 && m_frames.at(i).value("type").toString() == "sendSignal")
                callId = m_frames.at(i).value("data").toObject().value("callId").toDouble();
        }
        QVERIFY(callId >= 1000000000);

        const QJsonObject servers{{"servers", QJsonArray{QJsonObject{{"rtpaddr", "10.0.0.1:4200"}, {"rtcpaddr", "10.0.0.1:4201"}}}}};
        auto reply401 = [&servers](double id) {
            QJsonObject data = servers;
            data.insert("status", 0);
            data.insert("sessId", "S" + QString(40, QLatin1Char('x')));
            data.insert("fromId", "111111111");
            data.insert("toId", "222222222");
            data.insert("id", id);
            return QJsonObject{{"type", "recvSignal"}, {"command", 401}, {"data", data}};
        };
        sendToEngine(reply401(callId + 1));
        QTest::qWait(800);
        QVERIFY(!frameSince(base, 416, QStringLiteral("sendSignal")));

        sendToEngine(reply401(callId));
        QTRY_VERIFY_WITH_TIMEOUT(frameSince(base, 416, QStringLiteral("sendSignal")), 5000);
        sendToEngine(QJsonObject{{"type", "request"}, {"command", "endCall"}});
        QTRY_VERIFY_WITH_TIMEOUT(frameSince(base, 409, QStringLiteral("sendSignal"))
                                 || frameSince(base, 405, QStringLiteral("sendSignal")), 5000);
    }

    void callSetupContract() {
        QTRY_VERIFY_WITH_TIMEOUT(m_engineSend != nullptr, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(sawUpdate(QStringLiteral("native-ready")), 10000);

        sendToEngine(QJsonObject{{"type", "request"}, {"command", "makeCall"},
                                 {"data", QJsonObject{{"type", 1}, {"partner", QJsonArray{QJsonObject{{"id", "123456"}, {"name", "Người thử"}}}}}}});
        QTRY_VERIFY_WITH_TIMEOUT(sawSignal(401), 5000);

        QJsonObject callFrame;
        for (const QJsonObject& f : m_frames)
            if (f.value("command").toInt() == 401 && f.value("type").toString() == "sendSignal") callFrame = f;
        const double callId = callFrame.value("data").toObject().value("callId").toDouble();
        QVERIFY(callId >= 1000000000);

        sendToEngine(QJsonObject{{"type", "recvSignal"}, {"command", 401},
                                 {"data", QJsonObject{{"status", 0}, {"sessId", "S" + QString(40, QLatin1Char('x'))},
                                                      {"fromId", "111111111"}, {"toId", "222222222"},
                                                      {"servers", QJsonArray{QJsonObject{{"rtpaddr", "10.0.0.1:4200"}, {"rtcpaddr", "10.0.0.1:4201"}}}}}}});
        QTRY_VERIFY_WITH_TIMEOUT(sawSignal(416), 5000);
    }

    void relayInitHandshake() {
        QUdpSocket relay;
        QVERIFY(relay.bind(QHostAddress::LocalHost, 0));
        QByteArray lastInit;
        int initCount = 0;
        connect(&relay, &QUdpSocket::readyRead, this, [&] {
            while (relay.hasPendingDatagrams()) {
                QByteArray buf(int(relay.pendingDatagramSize()), Qt::Uninitialized);
                QHostAddress addr;
                quint16 port = 0;
                relay.readDatagram(buf.data(), buf.size(), &addr, &port);
                if (buf.size() < kControlHeaderLen || u8at(buf, 0) != MsgType::Control || u8at(buf, 1) != kMagicByte) continue;
                if (rd16le(buf, 18) != Cmd::InitCallCaller && rd16le(buf, 18) != Cmd::InitCallCallee) continue;
                lastInit = buf;
                ++initCount;
                ControlHeader h;
                h.msgType = MsgType::ServerReply;
                h.magic = kMagicByte;
                h.cmd = rd16le(buf, 18);
                h.subCmd = u8at(buf, 20);
                QByteArray reply = buildControlHeader(h) + le32(0) + le32(rd32le(buf, kControlHeaderLen + 0)) +
                                   le32(0xabcdef01u) + u16String(QByteArray("203.0.113.9|5"));
                relay.writeDatagram(reply, addr, port);
            }
        });

        EngineRun run;
        QVERIFY(run.recv.listen(QHostAddress::LocalHost, 0));
        QVERIFY(run.send.listen(QHostAddress::LocalHost, 0));
        run.key = m_key;
        run.parser = std::make_unique<FrameParser>(run.key, true, [&run](const IpcFrame& f) {
            if (!f.isToken && !f.isError && f.json.isObject()) run.frames.append(f.json.toObject());
        });
        connect(&run.recv, &QTcpServer::newConnection, this, [&run] {
            QTcpSocket* s = run.recv.nextPendingConnection();
            connect(s, &QTcpSocket::readyRead, s, [&run, s] { run.parser->push(s->readAll()); });
        });
        connect(&run.send, &QTcpServer::newConnection, this, [&run] { run.out = run.send.nextPendingConnection(); });

        run.proc.start(QStringLiteral(ZCALL_ENGINE_BIN),
                       {QStringLiteral("--token"), QLatin1String(kToken),
                        QStringLiteral("--key-file"), m_dir.filePath(QStringLiteral("transport.key")),
                        QStringLiteral("--recv"), QString::number(run.recv.serverPort()),
                        QStringLiteral("--send"), QString::number(run.send.serverPort()),
                        QStringLiteral("--media"), QStringLiteral("zrtc"), QStringLiteral("--send-udp"),
                        QStringLiteral("--no-window")});
        QVERIFY(run.proc.waitForStarted(5000));
        QTRY_VERIFY_WITH_TIMEOUT(run.out != nullptr, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(run.hasUpdate(QStringLiteral("native-ready")), 10000);

        run.write(QJsonObject{{"type", "request"}, {"command", "makeCall"},
                              {"data", QJsonObject{{"type", 1}, {"partner", QJsonArray{QJsonObject{{"id", "123456"}, {"name", "Người thử"}}}}}}});
        QTRY_VERIFY_WITH_TIMEOUT(run.hasSignal(401), 5000);

        run.write(QJsonObject{{"type", "recvSignal"}, {"command", 401},
                              {"data", QJsonObject{{"status", 0}, {"sessId", "S" + QString(40, QLatin1Char('x'))},
                                                   {"fromId", "111111111"}, {"toId", "222222222"},
                                                   {"servers", QJsonArray{QJsonObject{{"rtpaddr", QStringLiteral("127.0.0.1:%1").arg(relay.localPort())},
                                                                                       {"rtcpaddr", QStringLiteral("127.0.0.1:%1").arg(relay.localPort())}}}}}}});
        QTRY_VERIFY_WITH_TIMEOUT(run.hasSignal(416), 8000);
        QVERIFY(initCount >= 1);
        QCOMPARE(int(u8at(lastInit, 0)), int(MsgType::Control));
        QCOMPARE(int(u8at(lastInit, 1)), kMagicByte);
        QCOMPARE(int(rd16le(lastInit, 18)), Cmd::InitCallCaller);
        QVERIFY(run.proc.state() != QProcess::NotRunning);

        run.proc.terminate();
        if (!run.proc.waitForFinished(3000)) run.proc.kill();
    }

    void ringPingWhileRingingStopsOnAnswer() {
        QUdpSocket relay;
        QVERIFY(relay.bind(QHostAddress::LocalHost, 0));
        int pingCount = 0;
        int endCallCount = 0;
        connect(&relay, &QUdpSocket::readyRead, this, [&] {
            while (relay.hasPendingDatagrams()) {
                QByteArray buf(int(relay.pendingDatagramSize()), Qt::Uninitialized);
                QHostAddress addr;
                quint16 port = 0;
                relay.readDatagram(buf.data(), buf.size(), &addr, &port);
                if (buf.size() < kControlHeaderLen || u8at(buf, 0) != MsgType::Control) continue;
                const int cmd = rd16le(buf, 18);
                ControlHeader h;
                h.msgType = MsgType::ServerReply;
                h.magic = u8at(buf, 1);
                h.cmd = cmd;
                h.subCmd = u8at(buf, 20);
                if (cmd == Cmd::InitCallCaller || cmd == Cmd::InitCallCallee) {
                    const QByteArray reply = buildControlHeader(h) + le32(0) + le32(rd32le(buf, kControlHeaderLen + 0))
                                             + le32(0x11223344u) + u16String(QByteArray("203.0.113.9|5"));
                    relay.writeDatagram(reply, addr, port);
                } else if (cmd == Cmd::Ping) {
                    ++pingCount;
                    relay.writeDatagram(buildControlHeader(h), addr, port);
                } else if (cmd == Cmd::EndCall) {
                    ++endCallCount;
                }
            }
        });

        EngineRun run;
        QVERIFY(run.recv.listen(QHostAddress::LocalHost, 0));
        QVERIFY(run.send.listen(QHostAddress::LocalHost, 0));
        run.key = m_key;
        run.parser = std::make_unique<FrameParser>(run.key, true, [&run](const IpcFrame& f) {
            if (!f.isToken && !f.isError && f.json.isObject()) run.frames.append(f.json.toObject());
        });
        connect(&run.recv, &QTcpServer::newConnection, this, [&run] {
            QTcpSocket* s = run.recv.nextPendingConnection();
            connect(s, &QTcpSocket::readyRead, s, [&run, s] { run.parser->push(s->readAll()); });
        });
        connect(&run.send, &QTcpServer::newConnection, this, [&run] { run.out = run.send.nextPendingConnection(); });

        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("ZCALL_PING_MS"), QStringLiteral("40"));
        run.proc.setProcessEnvironment(env);
        run.proc.start(QStringLiteral(ZCALL_ENGINE_BIN),
                       {QStringLiteral("--token"), QLatin1String(kToken),
                        QStringLiteral("--key-file"), m_dir.filePath(QStringLiteral("transport.key")),
                        QStringLiteral("--recv"), QString::number(run.recv.serverPort()),
                        QStringLiteral("--send"), QString::number(run.send.serverPort()),
                        QStringLiteral("--media"), QStringLiteral("zrtc"), QStringLiteral("--send-udp"),
                        QStringLiteral("--no-window")});
        QVERIFY(run.proc.waitForStarted(5000));
        QTRY_VERIFY_WITH_TIMEOUT(run.out != nullptr, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(run.hasUpdate(QStringLiteral("native-ready")), 10000);

        run.write(QJsonObject{{"type", "request"}, {"command", "makeCall"},
                              {"data", QJsonObject{{"type", 1}, {"partner", QJsonArray{QJsonObject{{"id", "123456"}, {"name", "Người thử"}}}}}}});
        QTRY_VERIFY_WITH_TIMEOUT(run.hasSignal(401), 5000);

        double callId = 0;
        for (const QJsonObject& f : run.frames) {
            if (f.value("type").toString() == "sendSignal" && f.value("command").toInt() == 401) {
                callId = f.value("data").toObject().value("callId").toDouble();
            }
        }
        const QString relayAddr = QStringLiteral("127.0.0.1:%1").arg(relay.localPort());
        run.write(QJsonObject{{"type", "recvSignal"}, {"command", 401},
                              {"data", QJsonObject{{"status", 0}, {"sessId", "S" + QString(40, QLatin1Char('r'))},
                                                   {"fromId", "111111111"}, {"toId", "222222222"},
                                                   {"servers", QJsonArray{QJsonObject{{"rtpaddr", relayAddr},
                                                                                       {"rtcpaddr", relayAddr}}}}}}});
        QTRY_VERIFY_WITH_TIMEOUT(run.hasSignal(416), 8000);
        QTRY_VERIFY_WITH_TIMEOUT(pingCount >= 2, 3000);

        const int pingsBeforeAnswer = pingCount;
        run.write(QJsonObject{{"type", "control"},
                              {"data", QJsonObject{{"act", "answer"},
                                                   {"data", QJsonObject{{"callId", callId}, {"status", 0},
                                                                        {"params", QJsonObject{{"status", 0}, {"rtpSerIp", relayAddr}}}}}}}});
        QTRY_VERIFY_WITH_TIMEOUT(run.hasSignal(408), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(pingCount > pingsBeforeAnswer, 3000);

        run.write(QJsonObject{{"type", "control"},
                              {"data", QJsonObject{{"act", "end_call"}, {"data", QJsonObject{{"callId", callId}}}}}});
        QTRY_VERIFY_WITH_TIMEOUT(endCallCount >= 1, 5000);
        const int pingsAtEnd = pingCount;
        QTest::qWait(150);
        QCOMPARE(pingCount, pingsAtEnd);

        run.proc.terminate();
        if (!run.proc.waitForFinished(3000)) run.proc.kill();
    }

    void uiHangupAfterAcceptAndOutgoing() {
        QTcpServer recvSrv;
        QTcpServer sendSrv;
        QTcpServer uiSrv;
        QVERIFY(recvSrv.listen(QHostAddress::LocalHost, 0));
        QVERIFY(sendSrv.listen(QHostAddress::LocalHost, 0));
        QVERIFY(uiSrv.listen(QHostAddress::LocalHost, 0));

        QList<QJsonObject> frames;
        QList<QJsonObject> uiMsgs;
        QTcpSocket* engineSend = nullptr;
        QTcpSocket* uiSock = nullptr;
        QByteArray uiBuf;
        bool uiAuthed = false;
        FrameParser parser(m_key, true, [&frames](const IpcFrame& f) {
            if (!f.isToken && !f.isError && f.json.isObject()) frames.append(f.json.toObject());
        });

        connect(&recvSrv, &QTcpServer::newConnection, this, [&] {
            QTcpSocket* s = recvSrv.nextPendingConnection();
            connect(s, &QTcpSocket::readyRead, s, [&parser, s] { parser.push(s->readAll()); });
        });
        connect(&sendSrv, &QTcpServer::newConnection, this, [&] { engineSend = sendSrv.nextPendingConnection(); });
        connect(&uiSrv, &QTcpServer::newConnection, this, [&] {
            uiSock = uiSrv.nextPendingConnection();
            connect(uiSock, &QTcpSocket::readyRead, uiSock, [&] {
                uiBuf += uiSock->readAll();
                for (;;) {
                    const int nl = uiBuf.indexOf('\n');
                    if (nl < 0) break;
                    const QByteArray line = uiBuf.left(nl);
                    uiBuf.remove(0, nl + 1);
                    if (!uiAuthed) {
                        if (line == "TKN-ui") uiAuthed = true;
                        continue;
                    }
                    const QJsonDocument d = QJsonDocument::fromJson(line);
                    if (d.isObject()) uiMsgs.append(d.object());
                }
            });
        });

        QProcess proc;
        proc.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        proc.start(QStringLiteral(ZCALL_ENGINE_BIN),
                   {QStringLiteral("--token"), QLatin1String(kToken),
                    QStringLiteral("--key-file"), m_dir.filePath(QStringLiteral("transport.key")),
                    QStringLiteral("--recv"), QString::number(recvSrv.serverPort()),
                    QStringLiteral("--send"), QString::number(sendSrv.serverPort()),
                    QStringLiteral("--ui-port"), QString::number(uiSrv.serverPort()),
                    QStringLiteral("--ui-token"), QStringLiteral("TKN-ui"),
                    QStringLiteral("--media"), QStringLiteral("stub"),
                    QStringLiteral("--group")});
        QVERIFY(proc.waitForStarted(5000));
        QTRY_VERIFY_WITH_TIMEOUT(engineSend != nullptr && uiSock != nullptr && uiAuthed, 10000);

        auto sendFrame = [&](const QJsonObject& f) {
            engineSend->write(ipcEncrypt(QJsonDocument(f).toJson(QJsonDocument::Compact), m_key) + '$');
            engineSend->flush();
        };
        auto sendUi = [&](const QJsonObject& a) {
            uiSock->write(QJsonDocument(a).toJson(QJsonDocument::Compact) + '\n');
            uiSock->flush();
        };
        auto hasSignal = [&](int cmd) {
            for (const QJsonObject& f : frames)
                if (f.value("type").toString() == "sendSignal" && f.value("command").toInt() == cmd) return true;
            return false;
        };
        auto findSignal = [&](int cmd) -> QJsonObject {
            for (const QJsonObject& f : frames)
                if (f.value("type").toString() == "sendSignal" && f.value("command").toInt() == cmd) return f;
            return {};
        };
        auto hasUpdate = [&](const QString& cmd, const QString& stateVal = QString()) {
            for (const QJsonObject& f : frames) {
                if (f.value("type").toString() == "update" && f.value("command").toString() == cmd) {
                    if (stateVal.isEmpty() || f.value("data").toObject().value("state").toString() == stateVal) return true;
                }
            }
            return false;
        };
        auto hasUiPhase = [&](const QString& phase) {
            for (const QJsonObject& m : uiMsgs)
                if (m.value("type").toString() == "state" && m.value("phase").toString() == phase) return true;
            return false;
        };
        auto hasUiClose = [&]() {
            for (const QJsonObject& m : uiMsgs)
                if (m.value("type").toString() == "close") return true;
            return false;
        };

        QTRY_VERIFY_WITH_TIMEOUT(hasUpdate(QStringLiteral("native-ready")), 10000);

        frames.clear();
        uiMsgs.clear();
        sendFrame(QJsonObject{
            {"type", "control"},
            {"data", QJsonObject{
                {"act", "request"},
                {"data", QJsonObject{
                    {"callId", 1234567890},
                    {"uidN", "999001"},
                    {"uidFrom", 999001},
                    {"uidTo", 111111},
                    {"session", "S" + QString(40, QLatin1Char('a'))},
                    {"rtpAddress", "10.0.0.1:4200"},
                    {"rtcpAddress", "10.0.0.1:4201"},
                    {"params", QJsonObject{{"Dname", "Caller One"}, {"extendData", QJsonObject{{"callType", 1}}}}}
                }}
            }}
        });
        auto hasUiMicAutoGain = [&](bool expected) {
            for (const QJsonObject& m : uiMsgs) {
                if (m.value("type").toString() == "state" && m.contains("micAutoGain")
                    && m.value("micAutoGain").toBool() == expected) {
                    return true;
                }
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(hasUiPhase(QStringLiteral("incoming")) && hasUiMicAutoGain(true), 5000);
        uiMsgs.clear();
        sendUi(QJsonObject{{"action", "micAutoGain"}, {"on", false}});
        sendUi(QJsonObject{{"action", "accept"}, {"camStartOff", false}});
        QTRY_VERIFY_WITH_TIMEOUT(hasSignal(402) && hasUiPhase(QStringLiteral("connected")) && hasUiMicAutoGain(false), 5000);

        frames.clear();
        uiMsgs.clear();
        sendUi(QJsonObject{{"action", "hangup"}});
        QTRY_VERIFY_WITH_TIMEOUT(hasSignal(409), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(hasSignal(406) && hasUpdate(QStringLiteral("callState"), QStringLiteral("free")) && hasUiClose(), 5000);
        const QJsonObject sig409 = findSignal(409).value("data").toObject();
        QCOMPARE(sig409.value("toId").toString(), QStringLiteral("999001"));
        QCOMPARE(sig409.value("callId").toDouble(), 1234567890.0);

        frames.clear();
        uiMsgs.clear();
        sendFrame(QJsonObject{
            {"type", "request"},
            {"command", "makeCall"},
            {"data", QJsonObject{{"type", 1}, {"partner", QJsonArray{QJsonObject{{"id", "999002"}, {"name", "Callee Two"}}}}}}
        });
        QTRY_VERIFY_WITH_TIMEOUT(hasSignal(401), 5000);
        sendFrame(QJsonObject{
            {"type", "recvSignal"},
            {"command", 401},
            {"data", QJsonObject{
                {"status", 0},
                {"sessId", "S" + QString(40, QLatin1Char('b'))},
                {"fromId", "111111"},
                {"toId", "999002"},
                {"servers", QJsonArray{QJsonObject{{"rtpaddr", "10.0.0.1:4200"}, {"rtcpaddr", "10.0.0.1:4201"}}}}
            }}
        });
        QTRY_VERIFY_WITH_TIMEOUT(hasSignal(416), 5000);
        uiMsgs.clear();
        sendUi(QJsonObject{{"action", "hangup"}});
        QTRY_VERIFY_WITH_TIMEOUT(hasSignal(405) && hasSignal(406) && hasUiClose(), 5000);

        frames.clear();
        uiMsgs.clear();
        sendFrame(QJsonObject{
            {"type", "request"},
            {"command", "makeCall"},
            {"data", QJsonObject{{"type", 1}, {"partner", QJsonArray{QJsonObject{{"id", "999003"}, {"name", "Callee Three"}}}}}}
        });
        QTRY_VERIFY_WITH_TIMEOUT(hasSignal(401), 5000);
        const double outCallId = findSignal(401).value("data").toObject().value("callId").toDouble();
        sendFrame(QJsonObject{
            {"type", "recvSignal"},
            {"command", 401},
            {"data", QJsonObject{
                {"status", 0},
                {"sessId", "S" + QString(40, QLatin1Char('c'))},
                {"fromId", "111111"},
                {"toId", "999003"},
                {"servers", QJsonArray{QJsonObject{{"rtpaddr", "10.0.0.1:4200"}, {"rtcpaddr", "10.0.0.1:4201"}}}}
            }}
        });
        QTRY_VERIFY_WITH_TIMEOUT(hasSignal(416), 5000);
        sendFrame(QJsonObject{
            {"type", "control"},
            {"data", QJsonObject{
                {"act", "answer"},
                {"data", QJsonObject{
                    {"callId", outCallId},
                    {"status", 0},
                    {"params", QJsonObject{{"status", 0}, {"rtpSerIp", "10.0.0.1:4200"}}}
                }}
            }}
        });
        QTRY_VERIFY_WITH_TIMEOUT(hasSignal(408) && hasUiPhase(QStringLiteral("connected")), 5000);
        frames.clear();
        uiMsgs.clear();
        sendUi(QJsonObject{{"action", "hangup"}});
        QTRY_VERIFY_WITH_TIMEOUT(hasSignal(409) && hasSignal(406) && hasUiClose(), 5000);
        QCOMPARE(findSignal(409).value("data").toObject().value("toId").toString(), QStringLiteral("999003"));

        frames.clear();
        uiMsgs.clear();
        sendFrame(QJsonObject{
            {"type", "control"},
            {"data", QJsonObject{
                {"act", "group_request"},
                {"data", QJsonObject{
                    {"id", 1456789012},
                    {"hostCall", 777001},
                    {"groupId", 888001},
                    {"receiverId", 111111},
                    {"callType", 1},
                    {"groupName", "Group Test"},
                    {"callSetting", QJsonObject{
                        {"session", "S" + QString(40, QLatin1Char('g'))},
                        {"servers", QJsonArray{QJsonObject{{"rtpaddr", "10.0.0.1:4200"}, {"rtcpaddr", "10.0.0.1:4201"}}}}
                    }}
                }}
            }}
        });
        QTRY_VERIFY_WITH_TIMEOUT(hasUiPhase(QStringLiteral("incoming")), 5000);
        sendUi(QJsonObject{{"action", "accept"}, {"camStartOff", false}});
        QTRY_VERIFY_WITH_TIMEOUT(hasSignal(12436) && hasUiPhase(QStringLiteral("connecting")), 5000);
        sendFrame(QJsonObject{
            {"type", "control"},
            {"data", QJsonObject{
                {"act", "group_broadcast"},
                {"data", QJsonObject{{"callId", 1456789012}, {"userId", 777001}, {"callState", 3}, {"audioState", 0}, {"videoState", 1}}}
            }}
        });
        QTRY_VERIFY_WITH_TIMEOUT(hasUiPhase(QStringLiteral("connected")), 5000);
        frames.clear();
        uiMsgs.clear();
        sendUi(QJsonObject{{"action", "hangup"}});
        QTRY_VERIFY_WITH_TIMEOUT(hasSignal(12438) && hasSignal(12446) && hasUiClose(), 5000);

        // Idle UI hangup when no active call still closes UI and resets callState to free
        frames.clear();
        uiMsgs.clear();
        sendUi(QJsonObject{{"action", "hangup"}});
        QTRY_VERIFY_WITH_TIMEOUT(hasUiClose() && hasUpdate(QStringLiteral("callState"), QStringLiteral("free")), 5000);

        // Incoming request ended by end_call with null callId closes UI
        frames.clear();
        uiMsgs.clear();
        sendFrame(QJsonObject{
            {"type", "control"},
            {"data", QJsonObject{
                {"act", "request"},
                {"data", QJsonObject{
                    {"callId", 1357924680},
                    {"uidN", "999004"},
                    {"uidFrom", "999004"},
                    {"uidTo", "111111"},
                    {"session", "S" + QString(40, QLatin1Char('d'))},
                    {"rtpAddress", "10.0.0.1:4200"},
                    {"rtcpAddress", "10.0.0.1:4201"},
                    {"params", QJsonObject{
                        {"Dname", "Caller Four"},
                        {"extendData", QJsonObject{{"callType", 0}}}
                    }}
                }}
            }}
        });
        QTRY_VERIFY_WITH_TIMEOUT(hasUiPhase(QStringLiteral("incoming")), 5000);
        uiMsgs.clear();
        sendFrame(QJsonObject{
            {"type", "control"},
            {"data", QJsonObject{
                {"act", "end_call"},
                {"data", QJsonObject{{"callId", QJsonValue::Null}}}
            }}
        });
        QTRY_VERIFY_WITH_TIMEOUT(hasUiClose() && hasUpdate(QStringLiteral("callState"), QStringLiteral("free")), 5000);

        // Connected call ended by peer via "endcall" variant closes UI
        frames.clear();
        uiMsgs.clear();
        sendFrame(QJsonObject{
            {"type", "request"},
            {"command", "makeCall"},
            {"data", QJsonObject{{"type", 1}, {"partner", QJsonArray{QJsonObject{{"id", "999005"}, {"name", "Callee Five"}}}}}}
        });
        QTRY_VERIFY_WITH_TIMEOUT(hasSignal(401), 5000);
        sendFrame(QJsonObject{
            {"type", "recvSignal"},
            {"command", 401},
            {"data", QJsonObject{
                {"status", 0},
                {"sessId", "S" + QString(40, QLatin1Char('e'))},
                {"fromId", "111111"},
                {"toId", "999005"},
                {"servers", QJsonArray{QJsonObject{{"rtpaddr", "10.0.0.1:4200"}, {"rtcpaddr", "10.0.0.1:4201"}}}}
            }}
        });
        QTRY_VERIFY_WITH_TIMEOUT(hasSignal(416), 5000);
        uiMsgs.clear();
        sendFrame(QJsonObject{
            {"type", "control"},
            {"data", QJsonObject{
                {"act", "endcall"},
                {"data", QJsonObject{{"callId", QJsonValue::Null}}}
            }}
        });
        QTRY_VERIFY_WITH_TIMEOUT(hasUiClose() && hasUpdate(QStringLiteral("callState"), QStringLiteral("free")), 5000);

        proc.terminate();
        if (!proc.waitForFinished(3000)) proc.kill();
    }

    void zrtcHangupAfterInitDoesNotCrash() {
        QUdpSocket relay;
        QVERIFY(relay.bind(QHostAddress::LocalHost, 0));
        connect(&relay, &QUdpSocket::readyRead, this, [&] {
            while (relay.hasPendingDatagrams()) {
                QByteArray buf(int(relay.pendingDatagramSize()), Qt::Uninitialized);
                QHostAddress addr;
                quint16 port = 0;
                relay.readDatagram(buf.data(), buf.size(), &addr, &port);
                if (buf.size() < kControlHeaderLen || u8at(buf, 0) != MsgType::Control) continue;
                const int cmd = rd16le(buf, 18);
                if (cmd == Cmd::InitCallCaller || cmd == Cmd::InitCallCallee) {
                    ControlHeader h;
                    h.msgType = MsgType::ServerReply;
                    h.magic = u8at(buf, 1);
                    h.cmd = cmd;
                    h.subCmd = u8at(buf, 20);
                    QByteArray reply = buildControlHeader(h) + le32(0) + le32(rd32le(buf, kControlHeaderLen + 0)) +
                                       le32(0x44556677u) + u16String(QByteArray("203.0.113.9|5"));
                    relay.writeDatagram(reply, addr, port);
                }
            }
        });

        EngineRun run;
        QTcpServer uiSrv;
        QVERIFY(run.recv.listen(QHostAddress::LocalHost, 0));
        QVERIFY(run.send.listen(QHostAddress::LocalHost, 0));
        QVERIFY(uiSrv.listen(QHostAddress::LocalHost, 0));
        run.key = m_key;
        run.parser = std::make_unique<FrameParser>(run.key, true, [&run](const IpcFrame& f) {
            if (!f.isToken && !f.isError && f.json.isObject()) run.frames.append(f.json.toObject());
        });
        connect(&run.recv, &QTcpServer::newConnection, this, [&run] {
            QTcpSocket* s = run.recv.nextPendingConnection();
            connect(s, &QTcpSocket::readyRead, s, [&run, s] { run.parser->push(s->readAll()); });
        });
        connect(&run.send, &QTcpServer::newConnection, this, [&run] { run.out = run.send.nextPendingConnection(); });

        QTcpSocket* uiSock = nullptr;
        QByteArray uiBuf;
        QList<QJsonObject> uiMsgs;
        bool uiAuthed = false;
        connect(&uiSrv, &QTcpServer::newConnection, this, [&] {
            uiSock = uiSrv.nextPendingConnection();
            uiSock->setParent(&uiSrv);
            connect(uiSock, &QTcpSocket::readyRead, this, [&] {
                uiBuf += uiSock->readAll();
                if (!uiAuthed) {
                    const int nl = uiBuf.indexOf('\n');
                    if (nl < 0) return;
                    uiBuf.remove(0, nl + 1);
                    uiAuthed = true;
                }
                for (int nl = uiBuf.indexOf('\n'); nl >= 0; nl = uiBuf.indexOf('\n')) {
                    const QByteArray line = uiBuf.left(nl).trimmed();
                    uiBuf.remove(0, nl + 1);
                    if (!line.isEmpty()) uiMsgs.append(QJsonDocument::fromJson(line).object());
                }
            });
        });

        run.proc.start(QStringLiteral(ZCALL_ENGINE_BIN),
                       {QStringLiteral("--token"), QLatin1String(kToken),
                        QStringLiteral("--key-file"), m_dir.filePath(QStringLiteral("transport.key")),
                        QStringLiteral("--recv"), QString::number(run.recv.serverPort()),
                        QStringLiteral("--send"), QString::number(run.send.serverPort()),
                        QStringLiteral("--ui-port"), QString::number(uiSrv.serverPort()),
                        QStringLiteral("--ui-token"), QStringLiteral("UI-zrtc"),
                        QStringLiteral("--media"), QStringLiteral("zrtc"),
                        QStringLiteral("--send-udp"),
                        QStringLiteral("--group")});
        QVERIFY(run.proc.waitForStarted(5000));
        QTRY_VERIFY_WITH_TIMEOUT(run.out != nullptr && uiSock != nullptr && uiAuthed, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(run.hasUpdate(QStringLiteral("native-ready")), 10000);

        run.write(QJsonObject{
            {"type", "request"},
            {"command", "makeCall"},
            {"data", QJsonObject{{"type", 1}, {"partner", QJsonArray{QJsonObject{{"id", "999777"}, {"name", "Peer ZRTC"}}}}}}
        });
        QTRY_VERIFY_WITH_TIMEOUT(run.hasSignal(401), 5000);
        double callId = 0;
        for (const QJsonObject& f : run.frames)
            if (f.value("type") == QLatin1String("sendSignal") && f.value("command").toInt() == 401)
                callId = f.value("data").toObject().value("callId").toDouble();
        QVERIFY(callId > 0);

        const QString relayEp = QStringLiteral("127.0.0.1:%1").arg(relay.localPort());
        run.write(QJsonObject{
            {"type", "recvSignal"},
            {"command", 401},
            {"data", QJsonObject{
                {"status", 0},
                {"sessId", "S" + QString(40, QLatin1Char('z'))},
                {"fromId", "111111"},
                {"toId", "999777"},
                {"servers", QJsonArray{QJsonObject{{"rtpaddr", relayEp}, {"rtcpaddr", relayEp}}}}
            }}
        });
        QTRY_VERIFY_WITH_TIMEOUT(run.hasSignal(416), 8000);
        QTest::qWait(150);

        run.write(QJsonObject{
            {"type", "control"},
            {"data", QJsonObject{
                {"act", "answer"},
                {"data", QJsonObject{
                    {"callId", callId},
                    {"status", 0},
                    {"params", QJsonObject{{"status", 0}, {"rtpSerIp", relayEp}}}
                }}
            }}
        });
        QTRY_VERIFY_WITH_TIMEOUT(run.hasSignal(408), 5000);
        auto hasUiType = [&](const QString& type, const QString& phase = QString()) {
            for (const QJsonObject& m : uiMsgs)
                if (m.value("type").toString() == type && (phase.isEmpty() || m.value("phase").toString() == phase))
                    return true;
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(hasUiType(QStringLiteral("state"), QStringLiteral("connected")), 5000);

        run.frames.clear();
        uiMsgs.clear();
        uiSock->write(QJsonDocument(QJsonObject{{"action", "hangup"}}).toJson(QJsonDocument::Compact) + '\n');
        uiSock->flush();

        QTRY_VERIFY_WITH_TIMEOUT(run.hasSignal(409) && run.hasSignal(406) && run.hasUpdate(QStringLiteral("bubble"))
                                     && run.hasUpdate(QStringLiteral("callState")) && hasUiType(QStringLiteral("close")),
                                 5000);
        QCOMPARE(run.proc.state(), QProcess::Running);

        run.proc.terminate();
        if (!run.proc.waitForFinished(3000)) run.proc.kill();
    }

    void sigtermExitsCleanlyWithoutDeadlock() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray key = QByteArray::fromBase64(QByteArray(kKeyB64));
        QFile keyFile(dir.filePath(QStringLiteral("transport.key")));
        QVERIFY(keyFile.open(QIODevice::WriteOnly));
        keyFile.write(kKeyB64);
        keyFile.close();

        EngineRun run;
        run.key = key;
        run.parser = std::make_unique<FrameParser>(key, true, [&run](const IpcFrame& f) {
            if (!f.isToken && !f.isError && f.json.isObject()) run.frames.append(f.json.toObject());
        });
        QVERIFY(run.recv.listen(QHostAddress::LocalHost, 0));
        QVERIFY(run.send.listen(QHostAddress::LocalHost, 0));
        QObject::connect(&run.recv, &QTcpServer::newConnection, &run.recv, [&run] {
            QTcpSocket* s = run.recv.nextPendingConnection();
            QObject::connect(s, &QTcpSocket::readyRead, s, [&run, s] { run.parser->push(s->readAll()); });
        });
        QObject::connect(&run.send, &QTcpServer::newConnection, &run.send, [&run] {
            run.out = run.send.nextPendingConnection();
            run.out->setParent(&run.send);
        });

        run.proc.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        run.proc.start(QStringLiteral(ZCALL_ENGINE_BIN),
                       {QStringLiteral("--token"), QLatin1String(kToken),
                        QStringLiteral("--key-file"), dir.filePath(QStringLiteral("transport.key")),
                        QStringLiteral("--recv"), QString::number(run.recv.serverPort()),
                        QStringLiteral("--send"), QString::number(run.send.serverPort()),
                        QStringLiteral("--media"), QStringLiteral("stub"),
                        QStringLiteral("--no-window")});
        QVERIFY(run.proc.waitForStarted(5000));
        QTRY_VERIFY_WITH_TIMEOUT(run.out != nullptr && run.hasUpdate(QStringLiteral("native-ready")), 10000);

        run.proc.terminate();
        QVERIFY(run.proc.waitForFinished(1200));
        QCOMPARE(run.proc.exitStatus(), QProcess::NormalExit);
        QCOMPARE(run.proc.exitCode(), 0);
    }

    void cleanupTestCase() {
        if (m_proc.state() != QProcess::NotRunning) {
            m_proc.terminate();
            if (!m_proc.waitForFinished(3000)) m_proc.kill();
        }
    }
};

QTEST_GUILESS_MAIN(TestCallEngine)
#include "test_call_engine.moc"
