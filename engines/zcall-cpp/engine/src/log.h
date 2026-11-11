// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <QString>
#include <QStringList>

#include <functional>

namespace zcall {

using LogFn = std::function<void(const QString&)>;

class Log {
public:
    static void openFile(const QString& dir);
    static void write(const QString& line);
    static void setSink(LogFn sink);
    static QString stamp();
};

inline void zlog(const QString& line) { Log::write(line); }

}
