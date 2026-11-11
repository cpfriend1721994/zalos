// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "wire/ipc_crypto.h"

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QTcpSocket>

#include <functional>
#include <memory>

namespace zcall {

class IpcLink : public QObject {
    Q_OBJECT

public:
    struct Ports {
        quint16 recv{0};
        quint16 send{0};
        QString token;
        QByteArray key;
        bool verbose{false};
    };

    IpcLink(Ports ports, QObject* parent = nullptr);

    void setOnFrame(std::function<void(const QJsonObject&)> cb) { m_onFrame = std::move(cb); }
    void setOnReady(std::function<void()> cb) { m_onReady = std::move(cb); }
    void setOnClosed(std::function<void(const QString&)> cb) { m_onClosed = std::move(cb); }
    void connectAll();
    void emitFrame(const QJsonObject& frame);

private:
    void maybeReady();

    Ports m_ports;
    QTcpSocket* m_recv{nullptr};
    QTcpSocket* m_send{nullptr};
    std::unique_ptr<FrameParser> m_parser;
    bool m_started{false};
    std::function<void(const QJsonObject&)> m_onFrame;
    std::function<void()> m_onReady;
    std::function<void(const QString&)> m_onClosed;
};

}
