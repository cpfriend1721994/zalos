// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocale>
#include <QString>

#include <cmath>
#include <cstdint>
#include <limits>

namespace zcall::js {

inline double number(const QJsonValue& v) {
    switch (v.type()) {
    case QJsonValue::Null:
        return 0;
    case QJsonValue::Bool:
        return v.toBool() ? 1 : 0;
    case QJsonValue::Double:
        return v.toDouble();
    case QJsonValue::String: {
        const QString s = v.toString().trimmed();
        if (s.isEmpty()) return 0;
        bool ok = false;
        double d = 0;
        if (s.startsWith(QLatin1String("0x")) || s.startsWith(QLatin1String("0X"))) {
            const qulonglong n = s.mid(2).toULongLong(&ok, 16);
            d = double(n);
        } else if (s == QLatin1String("Infinity") || s == QLatin1String("+Infinity")) {
            return std::numeric_limits<double>::infinity();
        } else if (s == QLatin1String("-Infinity")) {
            return -std::numeric_limits<double>::infinity();
        } else {
            d = s.toDouble(&ok);
        }
        return ok ? d : std::numeric_limits<double>::quiet_NaN();
    }
    case QJsonValue::Array: {
        const QJsonArray a = v.toArray();
        if (a.isEmpty()) return 0;
        if (a.size() == 1) return number(a.at(0));
        return std::numeric_limits<double>::quiet_NaN();
    }
    default:
        return std::numeric_limits<double>::quiet_NaN();
    }
}

inline bool finite(double d) { return std::isfinite(d); }

inline double trunc(double d) { return std::trunc(d); }

inline int32_t toInt32(double d) {
    if (!std::isfinite(d)) return 0;
    const double t = std::trunc(d);
    const double m = std::fmod(t, 4294967296.0);
    const double u = m < 0 ? m + 4294967296.0 : m;
    return static_cast<int32_t>(static_cast<uint32_t>(u));
}

inline uint32_t toUint32(double d) { return static_cast<uint32_t>(toInt32(d)); }

inline bool truthy(const QJsonValue& v) {
    switch (v.type()) {
    case QJsonValue::Bool:
        return v.toBool();
    case QJsonValue::Double: {
        const double d = v.toDouble();
        return d != 0 && !std::isnan(d);
    }
    case QJsonValue::String:
        return !v.toString().isEmpty();
    case QJsonValue::Array:
    case QJsonValue::Object:
        return true;
    default:
        return false;
    }
}

inline QString numberString(double d) {
    if (std::isnan(d)) return QStringLiteral("NaN");
    if (std::isinf(d)) return d > 0 ? QStringLiteral("Infinity") : QStringLiteral("-Infinity");
    if (d == 0) return QStringLiteral("0");
    if (d == std::trunc(d) && std::fabs(d) < 1e21) return QString::number(static_cast<qlonglong>(d));
    if (std::fabs(d) >= 1e-6 && std::fabs(d) < 1e21) return QString::number(d, 'f', QLocale::FloatingPointShortest);
    return QString::number(d, 'g', QLocale::FloatingPointShortest);
}

inline QString toFixed1(double x) { return QString::number(std::floor(x * 10 + 0.5) / 10, 'f', 1); }

inline QString string(const QJsonValue& v) {
    switch (v.type()) {
    case QJsonValue::String:
        return v.toString();
    case QJsonValue::Double:
        return numberString(v.toDouble());
    case QJsonValue::Bool:
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Null:
        return QStringLiteral("null");
    case QJsonValue::Undefined:
        return QStringLiteral("undefined");
    case QJsonValue::Object:
        return QStringLiteral("[object Object]");
    default:
        return QString();
    }
}

inline QString stringOr(const QJsonValue& v, const QString& fallback = QString()) {
    return truthy(v) ? string(v) : fallback;
}

inline QJsonObject parseMaybeJson(const QJsonValue& v) {
    if (v.isString()) {
        const QJsonDocument d = QJsonDocument::fromJson(v.toString().toUtf8());
        return d.isObject() ? d.object() : QJsonObject();
    }
    return v.isObject() ? v.toObject() : QJsonObject();
}

inline bool defined(const QJsonValue& v) { return !v.isUndefined(); }

inline bool nullish(const QJsonValue& v) { return v.isUndefined() || v.isNull(); }

inline QString stringify(const QJsonValue& v) {
    if (v.isObject()) return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    if (v.isArray()) return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    if (v.isString()) {
        const QJsonArray wrap{v};
        const QByteArray s = QJsonDocument(wrap).toJson(QJsonDocument::Compact);
        return QString::fromUtf8(s.mid(1, s.size() - 2));
    }
    return string(v);
}

}
