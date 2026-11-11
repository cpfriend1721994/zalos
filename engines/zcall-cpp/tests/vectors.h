// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace zcall::test {

inline QJsonValue loadVectors(const char* name) {
    QFile f(QStringLiteral(ZCALL_VECTORS_DIR "/") + QLatin1String(name));
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll());
    if (d.isArray()) return d.array();
    return d.object();
}

inline QByteArray hx(const QJsonValue& v) { return QByteArray::fromHex(v.toString().toLatin1()); }

inline QString hexs(const QByteArray& b) { return QString::fromLatin1(b.toHex()); }

inline uint32_t u32(const QJsonValue& v) { return static_cast<uint32_t>(static_cast<qint64>(v.toDouble())); }

}
