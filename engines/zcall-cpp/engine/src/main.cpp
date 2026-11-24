// SPDX-License-Identifier: BSD-3-Clause
#include "call_ui.h"
#include "call_window.h"
#include "engine_core.h"
#include "ipc_link.h"
#include "log.h"
#include "stub_media.h"
#include "ui_client.h"
#include "zrtc_media.h"

#include <QCoreApplication>
#include <QDir>
#include <QSocketNotifier>
#include <QStringList>
#include <QTimer>

#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <memory>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <unistd.h>

using namespace zcall;

namespace {

int g_sigFd[2]{-1, -1};

void handleExitSignal(int) {
    if (g_sigFd[1] < 0) return;
    const char b = 1;
    const ssize_t ignored = ::write(g_sigFd[1], &b, 1);
    (void)ignored;
}

}

int main(int argc, char** argv) {
#ifdef __linux__
    ::prctl(PR_SET_PDEATHSIG, SIGTERM);
#endif
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    auto has = [&args](const QString& name) { return args.contains(QStringLiteral("--") + name); };
    auto opt = [&args](const QString& name, const QString& fallback) {
        const int i = args.indexOf(QStringLiteral("--") + name);
        return i >= 0 && i + 1 < args.size() ? args.at(i + 1) : fallback;
    };

    const QString token = opt(QStringLiteral("token"), QString());
    if (token.isEmpty()) {
        std::fputs("need --token <TK> (the value Zalo passes on the command line)\n", stderr);
        return 2;
    }
    const QString keyFile = opt(QStringLiteral("key-file"), QString());
    if (keyFile.isEmpty()) {
        std::fputs("need --key-file <PATH> (base64 transport key written by zalos)\n", stderr);
        return 2;
    }
    const quint16 recvPort = quint16(opt(QStringLiteral("recv"), QStringLiteral("29631")).toUInt());
    const quint16 sendPort = quint16(opt(QStringLiteral("send"), QStringLiteral("29632")).toUInt());
    const QString mediaMode = opt(QStringLiteral("media"), QStringLiteral("zrtc"));
    const bool sendUdp = has(QStringLiteral("send-udp"));
    const bool verbose = has(QStringLiteral("verbose")) || !qEnvironmentVariable("ZCALL_VERBOSE").isEmpty();
    const bool group = has(QStringLiteral("group"));

    QString keyError;
    const auto key = loadKey(keyFile, &keyError);
    if (!key) {
        std::fprintf(stderr, "%s\n", qPrintable(keyError));
        return 1;
    }

    const QString logDir = opt(QStringLiteral("log-dir"), QString());
    if (!logDir.isEmpty()) {
        Log::openFile(logDir);
    } else {
        Log::setSink(nullptr);
    }
    zlog(QStringLiteral("start: media=%1 sendUdp=%2 group=%3 ports %4/%5")
             .arg(mediaMode)
             .arg(sendUdp ? QStringLiteral("true") : QStringLiteral("false"))
             .arg(group ? QStringLiteral("true") : QStringLiteral("false"))
             .arg(recvPort)
             .arg(sendPort));

    std::unique_ptr<CallUi> ui;
    if (has(QStringLiteral("no-window"))) {
        ui = std::make_unique<HeadlessUi>();
    } else {
        const quint16 uiPort = quint16(opt(QStringLiteral("ui-port"), QStringLiteral("0")).toUInt());
        const QString uiToken = opt(QStringLiteral("ui-token"), QString());
        if (uiPort && !uiToken.isEmpty()) ui = std::make_unique<ElectronCallUi>(uiPort, uiToken);
        else ui = std::make_unique<CallWindow>();
    }

    std::unique_ptr<MediaBackend> media;
    if (mediaMode == QLatin1String("stub")) media = std::make_unique<StubMedia>();
    else media = std::make_unique<ZrtcMedia>(sendUdp);

    IpcLink* ipcPtr = nullptr;
    EngineCore core([&ipcPtr](const QJsonObject& frame) {
        if (ipcPtr) ipcPtr->emitFrame(frame);
    }, media.get(), ui.get(), group);

    IpcLink::Ports ports;
    ports.recv = recvPort;
    ports.send = sendPort;
    ports.token = token;
    ports.key = *key;
    ports.verbose = verbose;
    IpcLink ipc(ports);
    ipcPtr = &ipc;
    ipc.setOnFrame([&core](const QJsonObject& f) { core.onZaloFrame(f); });
    ipc.setOnReady([&core] { core.start(); });
    ipc.setOnClosed([&core, &app](const QString&) {
        core.hangup(QStringLiteral("engine-exit"));
        QTimer::singleShot(300, &app, [] { QCoreApplication::exit(0); });
    });
    ipc.connectAll();

    QSocketNotifier* sigNotifier = nullptr;
#ifdef __linux__
    const bool pipeOk = ::pipe2(g_sigFd, O_NONBLOCK | O_CLOEXEC) == 0;
#else
    const bool pipeOk = ::pipe(g_sigFd) == 0;
    if (pipeOk) {
        ::fcntl(g_sigFd[0], F_SETFL, O_NONBLOCK);
        ::fcntl(g_sigFd[1], F_SETFL, O_NONBLOCK);
    }
#endif
    if (pipeOk) {
        std::signal(SIGINT, handleExitSignal);
        std::signal(SIGTERM, handleExitSignal);
        sigNotifier = new QSocketNotifier(g_sigFd[0], QSocketNotifier::Read, &app);
        QObject::connect(sigNotifier, &QSocketNotifier::activated, &app, [&core, &app, sigNotifier]() {
            sigNotifier->setEnabled(false);
            char buf[32];
            while (::read(g_sigFd[0], buf, sizeof(buf)) > 0) {}
            core.hangup(QStringLiteral("engine-exit"));
            QTimer::singleShot(250, &app, [] { QCoreApplication::exit(0); });
        });
    }

    const int code = app.exec();
    ipcPtr = nullptr;
    if (g_sigFd[0] >= 0) ::close(g_sigFd[0]);
    if (g_sigFd[1] >= 0) ::close(g_sigFd[1]);
    g_sigFd[0] = g_sigFd[1] = -1;
    return code;
}
