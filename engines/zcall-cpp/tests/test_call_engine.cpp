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
        m_proc.start(QStringLiteral(ZCALL_ENGINE_BIN),
                     {QStringLiteral("--token"), QLatin1String(kToken),
                      QStringLiteral("--key-file"), m_dir.filePath(QStringLiteral("transport.key")),
                      QStringLiteral("--recv"), QString::number(m_recvSrv.serverPort()),
                      QStringLiteral("--send"), QString::number(m_sendSrv.serverPort()),
                      QStringLiteral("--media"), QStringLiteral("stub"),
                      QStringLiteral("--no-window")});
        QVERIFY(m_proc.waitForStarted(5000));
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

    void cleanupTestCase() {
        if (m_proc.state() != QProcess::NotRunning) {
            m_proc.terminate();
            if (!m_proc.waitForFinished(3000)) m_proc.kill();
        }
    }
};

QTEST_GUILESS_MAIN(TestCallEngine)
#include "test_call_engine.moc"
