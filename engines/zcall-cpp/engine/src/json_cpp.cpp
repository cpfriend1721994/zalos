// SPDX-License-Identifier: BSD-3-Clause
#include "json_cpp.h"

#include "js.h"

#include <QJsonArray>
#include <QJsonObject>

namespace zcall {

namespace {

void quote(QByteArray& out, const QString& s) {
    out += '"';
    const int n = int(s.size());
    for (int i = 0; i < n; ++i) {
        const char16_t c = s.at(i).unicode();
        switch (c) {
        case u'"':
            out += "\\\"";
            continue;
        case u'\\':
            out += "\\\\";
            continue;
        case u'\b':
            out += "\\b";
            continue;
        case u'\f':
            out += "\\f";
            continue;
        case u'\n':
            out += "\\n";
            continue;
        case u'\r':
            out += "\\r";
            continue;
        case u'\t':
            out += "\\t";
            continue;
        default:
            break;
        }
        if (c < 0x20) {
            out += "\\u00";
            out += QByteArray::number(c, 16).rightJustified(2, '0');
            continue;
        }
        if (c >= 0xd800 && c <= 0xdbff && i + 1 < n && s.at(i + 1).unicode() >= 0xdc00 && s.at(i + 1).unicode() <= 0xdfff) {
            out += QString(s.mid(i, 2)).toUtf8();
            ++i;
            continue;
        }
        if (c >= 0xd800 && c <= 0xdfff) {
            out += "\\u";
            out += QByteArray::number(c, 16);
            continue;
        }
        out += QString(QChar(c)).toUtf8();
    }
    out += '"';
}

void write(QByteArray& out, const QJsonValue& v) {
    switch (v.type()) {
    case QJsonValue::Null:
    case QJsonValue::Undefined:
        out += "null";
        return;
    case QJsonValue::Bool:
        out += v.toBool() ? "true" : "false";
        return;
    case QJsonValue::Double: {
        const double d = v.toDouble();
        out += std::isfinite(d) ? js::numberString(d).toLatin1() : QByteArray("null");
        return;
    }
    case QJsonValue::String:
        quote(out, v.toString());
        return;
    case QJsonValue::Array: {
        out += '[';
        bool first = true;
        for (const QJsonValue& e : v.toArray()) {
            if (!first) out += ',';
            first = false;
            write(out, e);
        }
        out += ']';
        return;
    }
    case QJsonValue::Object: {
        const QJsonObject o = v.toObject();
        out += '{';
        bool first = true;
        for (auto it = o.begin(); it != o.end(); ++it) {
            if (it.value().isUndefined()) continue;
            if (!first) out += ',';
            first = false;
            quote(out, it.key());
            out += ':';
            write(out, it.value());
        }
        out += '}';
        return;
    }
    }
}

}

QByteArray jsonStringify(const QJsonValue& v) {
    QByteArray out;
    write(out, v);
    return out;
}

QString jsonCpp(const QJsonValue& v) { return QString::fromUtf8(jsonStringify(v)) + QLatin1Char('\n'); }

}
