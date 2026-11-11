#pragma once

#include <QByteArray>
#include <QVector>

#include <optional>

namespace Zalos::Core::Sync::V2 {

struct ProtoField {
    int number{0};
    int wireType{0};
    quint64 varint{0};
    QByteArray bytes;
};

bool splitFrames(const QByteArray& data, QVector<QByteArray>* frames);

std::optional<QVector<ProtoField>> parseProto(const QByteArray& data);

enum class ZstdStatus { Ok, Unavailable, Error, UnknownSize };

ZstdStatus zstdDecompress(const QByteArray& compressed, QByteArray* out, QByteArray* detail);

}  // namespace Zalos::Core::Sync::V2
