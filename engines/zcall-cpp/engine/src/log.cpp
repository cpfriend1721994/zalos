// SPDX-License-Identifier: BSD-3-Clause
#include "log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>

#include <cstdio>
#include <memory>

namespace zcall {

namespace {

QMutex& mutex() {
    static QMutex m;
    return m;
}

std::unique_ptr<QFile>& file() {
    static std::unique_ptr<QFile> f;
    return f;
}

LogFn& sink() {
    static LogFn s;
    return s;
}

}

void Log::openFile(const QString& dir) {
    QMutexLocker lock(&mutex());
    const QString name = QStringLiteral("native-engine-%1.log").arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd")));
    auto f = std::make_unique<QFile>(QDir(dir).filePath(name));
    if (f->open(QIODevice::Append | QIODevice::WriteOnly)) file() = std::move(f);
}

void Log::setSink(LogFn s) {
    QMutexLocker lock(&mutex());
    sink() = std::move(s);
}

QString Log::stamp() { return QDateTime::currentDateTimeUtc().toString(QStringLiteral("HH:mm:ss.zzz")); }

void Log::write(const QString& line) {
    const QString full = stamp() + QStringLiteral(" [zalos-call] ") + line;
    QMutexLocker lock(&mutex());
    if (sink()) {
        sink()(line);
        return;
    }
    const QByteArray bytes = full.toUtf8() + '\n';
    if (file()) {
        file()->write(bytes);
        file()->flush();
    } else {
        std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stderr);
    }
}

}
