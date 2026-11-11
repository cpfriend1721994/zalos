// SPDX-License-Identifier: BSD-3-Clause
#include "ipc_link.h"

#include "js.h"
#include "log.h"
#include "wire/bytes.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>

namespace zcall {

namespace {

QString brief(const QJsonObject& f) {
    const QString type = f.value(QStringLiteral("type")).toString();
    QString cmd = js::string(f.value(QStringLiteral("command")));
    if (cmd.isEmpty()) cmd = js::string(f.value(QStringLiteral("data")).toObject().value(QStringLiteral("act")));
    return type + QLatin1Char(' ') + cmd;
}

}

IpcLink::IpcLink(Ports ports, QObject* parent) : QObject(parent), m_ports(std::move(ports)) {}

void IpcLink::connectAll() {
    const QByteArray tokenLine = m_ports.token.toUtf8() + '\n';

    m_recv = new QTcpSocket(this);
    connect(m_recv, &QTcpSocket::connected, this, [this, tokenLine] {
        m_recv->write(tokenLine);
        maybeReady();
    });
    connect(m_recv, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        zlog(QStringLiteral("recv error %1").arg(m_recv->errorString()));
    });
    connect(m_recv, &QTcpSocket::disconnected, this, [this] {
        zlog(QStringLiteral("recv closed"));
        if (m_onClosed) m_onClosed(QStringLiteral("recv"));
    });

    m_send = new QTcpSocket(this);
    m_parser = std::make_unique<FrameParser>(m_ports.key, false, [this](const IpcFrame& f) {
        if (f.isError) {
            zlog(QStringLiteral("bad frame %1").arg(QString::fromLatin1(f.raw)));
            return;
        }
        if (!f.json.isObject()) return;
        const QJsonObject obj = f.json.toObject();
        zlog(QStringLiteral("<- %1").arg(brief(obj)) +
             (m_ports.verbose ? QStringLiteral(" ") + QString::fromUtf8(QJsonDocument(obj.value(QStringLiteral("data")).toObject()).toJson(QJsonDocument::Compact)) : QString()));
        if (m_onFrame) m_onFrame(obj);
    });
    connect(m_send, &QTcpSocket::connected, this, [this, tokenLine] {
        m_send->write(tokenLine);
        maybeReady();
    });
    connect(m_send, &QTcpSocket::readyRead, this, [this] { m_parser->push(m_send->readAll()); });
    connect(m_send, &QTcpSocket::disconnected, this, [this] {
        zlog(QStringLiteral("send closed"));
        if (m_onClosed) m_onClosed(QStringLiteral("send"));
    });

    m_recv->connectToHost(QStringLiteral("127.0.0.1"), m_ports.recv);
    m_send->connectToHost(QStringLiteral("127.0.0.1"), m_ports.send);
}

void IpcLink::maybeReady() {
    if (m_started || !m_recv || !m_send) return;
    if (m_recv->state() != QAbstractSocket::ConnectedState || m_send->state() != QAbstractSocket::ConnectedState) return;
    m_started = true;
    zlog(QStringLiteral("connected; engine ready"));
    if (m_onReady) m_onReady();
}

void IpcLink::emitFrame(const QJsonObject& frame) {
    zlog(QStringLiteral("-> %1").arg(brief(frame)) +
         (m_ports.verbose ? QStringLiteral(" ") + QString::fromUtf8(QJsonDocument(frame.value(QStringLiteral("data")).toObject()).toJson(QJsonDocument::Compact)) : QString()));
    const QByteArray json = QJsonDocument(frame).toJson(QJsonDocument::Compact);
    const QByteArray hex = ipcEncrypt(json, m_ports.key);
    if (hex.isEmpty() || !m_recv) return;
    if (hex.size() <= 4000) {
        m_recv->write(hex + '$');
        return;
    }
    const qint64 id = QDateTime::currentMSecsSinceEpoch();
    for (const QByteArray& part : ipcChunks(hex, id)) m_recv->write(part);
}

}
