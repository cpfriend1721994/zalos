#pragma once

#include <QString>

namespace Zalos::Core::Network::ErrorCatalog {

struct Message {
    QString vi;
    QString en;
    bool showCode{false};
    bool traceRaw{false};

    bool isEmpty() const { return vi.isEmpty() && en.isEmpty(); }
};

Message apiError(int code);

Message transportError(const QString& raw);

Message phoneSyncError(int code);

QString phoneSyncName(int code);

Message describe(int code, const QString& raw);

QString pick(const Message& message, bool english);

QString render(int code, const QString& raw, bool english);

void trace(const QString& scope, const QString& line);

}
