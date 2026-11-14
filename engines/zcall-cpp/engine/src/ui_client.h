// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "call_ui.h"
#include "call_window.h"

#include <QJsonObject>
#include <QObject>
#include <QTcpSocket>

#include <memory>

namespace zcall {

class ElectronCallUi : public QObject, public CallUi {
    Q_OBJECT

public:
    ElectronCallUi(quint16 port, const QString& token, QObject* parent = nullptr);
    ~ElectronCallUi() override;

    bool rich() const override { return !m_fallback; }
    void open(const UiOpen& o) override;
    void incoming(const UiIncoming& o, AcceptFn onAccept, RejectFn onReject) override;
    void status(const QString& code, double since = 0, const QJsonObject& params = {}) override;
    void close(const QString& code, const QJsonObject& params = {}) override;
    void notice(const QString& code, const QJsonObject& params = {}) override;
    void micVolume(int pct) override;
    void micLevel(int level) override;
    void peer(bool camOff, bool muted, bool sharing) override;
    void setCameraEncode(const QJsonObject& p) override;
    void setScreenEncode(const QJsonObject& p) override;
    void requestKeyFrame() override;
    void rename(const QString& name) override;
    void members(const QJsonArray& list) override;
    void speaking(uint32_t src, bool on) override;
    void video(const UiVideo& v) override;

private:
    void setState(const QJsonObject& s);
    void send(const QJsonObject& msg);
    void onData();
    void onAction(const QJsonObject& m);
    void useFallback();

    QTcpSocket* m_sock{nullptr};
    QByteArray m_buf;
    QJsonObject m_state;
    bool m_hasState{false};
    AcceptFn m_accept;
    RejectFn m_reject;
    std::unique_ptr<CallWindow> m_fallback;
    quint16 m_port;
    QString m_token;
};

}
