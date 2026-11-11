// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "call_ui.h"

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>

#include <functional>

namespace zcall {

class CallWindow : public QObject, public CallUi {
    Q_OBJECT

public:
    explicit CallWindow(QObject* parent = nullptr);
    ~CallWindow() override;

    void setHangup(std::function<void()> cb) { m_hangup = std::move(cb); }
    void open(const UiOpen& o) override;
    void incoming(const UiIncoming& o, AcceptFn onAccept, RejectFn onReject) override;
    void status(const QString& text, double since = 0) override;
    void close(const QString& text) override;

private:
    void kill();
    QPointer<QProcess> m_proc;
    QTimer m_timer;
    std::function<void()> m_hangup;
};

}
