#pragma once

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QPointer>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>

#include <functional>

namespace TestSupport {

inline QByteArray jsonEnvelope(int errorCode, const QJsonValue& data = QJsonValue::Null) {
    return QJsonDocument(QJsonObject{{"error_code", errorCode}, {"data", data}}).toJson(QJsonDocument::Compact);
}

class LoopbackApi {
public:
    struct Request {
        QString target;
        int sequence{0};
    };
    using Responder = std::function<QByteArray(const Request&)>;

    explicit LoopbackApi(Responder responder = {})
        : m_responder(std::move(responder)) {
        m_server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this]() {
            while (QTcpSocket* socket = m_server.nextPendingConnection()) {
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() { onReadyRead(socket); });
                QObject::connect(socket, &QTcpSocket::disconnected, socket, [this, socket]() {
                    m_buffers.remove(socket);
                    socket->deleteLater();
                });
            }
        });
    }

    bool isListening() const { return m_server.isListening(); }

    QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }

    QJsonObject loginInfo(const QString& uid) const {
        QJsonObject services;
        for (const char* name : kServices) services.insert(QLatin1String(name), QJsonArray{baseUrl()});
        return QJsonObject{{"uid", uid},
                           {"zpw_enk", QString::fromLatin1(QByteArray(32, 'A').toBase64())},
                           {"zpw_service_map_v3", services}};
    }

    void setResponder(Responder responder) { m_responder = std::move(responder); }

    int total() const { return m_requests.size(); }

    int count(const QString& needle) const {
        int n = 0;
        for (const Request& r : m_requests) {
            if (r.target.contains(needle)) ++n;
        }
        return n;
    }

    int held() const {
        int n = 0;
        for (const QPointer<QTcpSocket>& socket : m_held) {
            if (socket) ++n;
        }
        return n;
    }

    void answerHeld(const QByteArray& body) {
        for (const QPointer<QTcpSocket>& socket : m_held) {
            if (socket) reply(socket.data(), body);
        }
        m_held.clear();
    }

private:
    static constexpr const char* kServices[] = {
        "profile", "chat", "group", "group_poll", "group_board", "group_cloud_message", "friend", "friend_board",
        "alias", "sticker", "file", "message", "zimsg", "reaction", "label", "conversation", "quick_message",
        "catalog", "auto_reply", "aext"};

    static int contentLength(const QByteArray& headers) {
        for (const QByteArray& line : headers.split('\n')) {
            const QByteArray lower = line.trimmed().toLower();
            if (lower.startsWith("content-length:")) return lower.mid(15).trimmed().toInt();
        }
        return 0;
    }

    static void reply(QTcpSocket* socket, const QByteArray& body) {
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                      + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        socket->disconnectFromHost();
    }

    void onReadyRead(QTcpSocket* socket) {
        QByteArray& buffer = m_buffers[socket];
        buffer += socket->readAll();
        const int headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;
        const QByteArray headers = buffer.left(headerEnd);
        if (buffer.size() < headerEnd + 4 + contentLength(headers)) return;
        m_buffers.remove(socket);
        const int lineEnd = headers.indexOf("\r\n");
        const QByteArray firstLine = lineEnd < 0 ? headers : headers.left(lineEnd);
        const QList<QByteArray> parts = firstLine.split(' ');
        const Request request{parts.size() > 1 ? QString::fromUtf8(parts.at(1)) : QString(), int(m_requests.size()) + 1};
        m_requests.append(request);
        const QByteArray body = m_responder ? m_responder(request) : jsonEnvelope(1);
        if (body.isNull()) {
            m_held.append(QPointer<QTcpSocket>(socket));
            return;
        }
        reply(socket, body);
    }

    QTcpServer m_server;
    Responder m_responder;
    QList<Request> m_requests;
    QHash<QTcpSocket*, QByteArray> m_buffers;
    QList<QPointer<QTcpSocket>> m_held;
};

}
