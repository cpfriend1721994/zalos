// SPDX-License-Identifier: BSD-3-Clause
#include "call_window.h"

#include "log.h"

#include <QDateTime>

namespace zcall {

CallWindow::CallWindow(QObject* parent) : QObject(parent) {
    m_hangup = [this] {
        if (m_listener) m_listener->uiHangup();
    };
}

CallWindow::~CallWindow() { kill(); }

void CallWindow::open(const UiOpen& o) {
    kill();
    auto* proc = new QProcess(this);
    proc->setStandardOutputFile(QProcess::nullDevice());
    proc->setStandardErrorFile(QProcess::nullDevice());
    connect(proc, &QProcess::errorOccurred, this, [proc](QProcess::ProcessError) {
        zlog(QStringLiteral("call window: %1").arg(proc->errorString()));
    });
    connect(proc, &QProcess::finished, this, [this, proc](int code, QProcess::ExitStatus) {
        proc->deleteLater();
        if (m_proc != proc) return;
        m_proc = nullptr;
        m_timer.stop();
        if (code == 1) m_hangup();
        else if (code != 0) zlog(QStringLiteral("call window: zenity exited %1").arg(code));
    });
    m_proc = proc;
    proc->start(QStringLiteral("zenity"), {QStringLiteral("--progress"), QStringLiteral("--pulsate"),
                                           QStringLiteral("--title=Zalo — %1").arg(o.title), QStringLiteral("--text=%1").arg(o.text),
                                           QStringLiteral("--cancel-label=Kết thúc"), QStringLiteral("--width=360")});
}

void CallWindow::incoming(const UiIncoming& o, AcceptFn onAccept, RejectFn onReject) {
    kill();
    auto* proc = new QProcess(this);
    proc->setStandardInputFile(QProcess::nullDevice());
    proc->setStandardOutputFile(QProcess::nullDevice());
    proc->setStandardErrorFile(QProcess::nullDevice());
    connect(proc, &QProcess::errorOccurred, this, [proc](QProcess::ProcessError) {
        zlog(QStringLiteral("call window: %1").arg(proc->errorString()));
    });
    connect(proc, &QProcess::finished, this, [this, proc, onAccept, onReject](int code, QProcess::ExitStatus) {
        proc->deleteLater();
        if (m_proc != proc) return;
        m_proc = nullptr;
        if (code == 0) onAccept(false);
        else if (code == 1 || code == 5) onReject(code == 5 ? QStringLiteral("timeout") : QStringLiteral("reject"));
        else {
            zlog(QStringLiteral("call window: zenity exited %1").arg(code));
            onReject(QStringLiteral("ui-error"));
        }
    });
    m_proc = proc;
    proc->start(QStringLiteral("zenity"), {QStringLiteral("--question"), QStringLiteral("--title=Zalo — %1").arg(o.title),
                                           QStringLiteral("--text=%1").arg(o.text), QStringLiteral("--ok-label=Nghe"),
                                           QStringLiteral("--cancel-label=Từ chối"), QStringLiteral("--timeout=60"),
                                           QStringLiteral("--width=360")});
}

void CallWindow::status(const QString& text, double since) {
    m_timer.stop();
    m_timer.disconnect();
    auto write = [this, text, since] {
        if (!m_proc) return;
        QString line = text;
        if (since != 0) {
            const qint64 s = (QDateTime::currentMSecsSinceEpoch() - qint64(since)) / 1000;
            line += QStringLiteral(" — %1:%2").arg(s / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
        }
        m_proc->write(QStringLiteral("# %1\n").arg(line).toUtf8());
    };
    write();
    if (since != 0) {
        connect(&m_timer, &QTimer::timeout, this, write);
        m_timer.start(1000);
    }
}

void CallWindow::close(const QString& text) {
    if (!text.isEmpty()) zlog(QStringLiteral("call window: %1").arg(text));
    kill();
}

void CallWindow::kill() {
    m_timer.stop();
    QProcess* p = m_proc;
    m_proc = nullptr;
    if (p) p->kill();
}

}
