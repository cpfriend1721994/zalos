#include "sync/v2/sync_v2_frames.h"

#include <QLibrary>

#include <cstring>

namespace Zalos::Core::Sync::V2 {

namespace {

bool readVarint(const QByteArray& data, int* pos, quint64* value) {
    quint64 result = 0;
    for (int shift = 0; shift < 64; shift += 7) {
        if (*pos >= data.size()) return false;
        const quint8 byte = static_cast<quint8>(data.at((*pos)++));
        result |= static_cast<quint64>(byte & 0x7f) << shift;
        if (!(byte & 0x80)) {
            *value = result;
            return true;
        }
    }
    return false;
}

struct ZstdApi {
    using DecompressFn = size_t (*)(void*, size_t, const void*, size_t);
    using ContentSizeFn = unsigned long long (*)(const void*, size_t);
    using IsErrorFn = unsigned (*)(size_t);
    using ErrorNameFn = const char* (*)(size_t);

    DecompressFn decompress{nullptr};
    ContentSizeFn contentSize{nullptr};
    IsErrorFn isError{nullptr};
    ErrorNameFn errorName{nullptr};
};

const ZstdApi* loadZstd() {
    static const ZstdApi api = [] {
        ZstdApi a;
        QLibrary lib(QStringLiteral("zstd"));
        lib.setLoadHints(QLibrary::ExportExternalSymbolsHint);
        if (!lib.load()) return a;
        a.decompress = reinterpret_cast<ZstdApi::DecompressFn>(lib.resolve("ZSTD_decompress"));
        a.contentSize = reinterpret_cast<ZstdApi::ContentSizeFn>(lib.resolve("ZSTD_getFrameContentSize"));
        a.isError = reinterpret_cast<ZstdApi::IsErrorFn>(lib.resolve("ZSTD_isError"));
        a.errorName = reinterpret_cast<ZstdApi::ErrorNameFn>(lib.resolve("ZSTD_getErrorName"));
        if (!a.decompress || !a.contentSize || !a.isError || !a.errorName) {
            a = ZstdApi{};
        }
        return a;
    }();
    return api.decompress ? &api : nullptr;
}

}  // namespace

bool splitFrames(const QByteArray& data, QVector<QByteArray>* frames) {
    frames->clear();
    int pos = 0;
    while (pos < data.size()) {
        if (data.size() - pos < 4) return false;
        const auto* p = reinterpret_cast<const uchar*>(data.constData() + pos);
        const quint32 length = static_cast<quint32>(p[0]) | (static_cast<quint32>(p[1]) << 8) |
                               (static_cast<quint32>(p[2]) << 16) | (static_cast<quint32>(p[3]) << 24);
        pos += 4;
        if (static_cast<quint64>(data.size() - pos) < length) return false;
        frames->append(data.mid(pos, static_cast<int>(length)));
        pos += static_cast<int>(length);
    }
    return true;
}

std::optional<QVector<ProtoField>> parseProto(const QByteArray& data) {
    QVector<ProtoField> fields;
    int pos = 0;
    while (pos < data.size()) {
        quint64 tag = 0;
        if (!readVarint(data, &pos, &tag)) return std::nullopt;
        ProtoField field;
        field.number = static_cast<int>(tag >> 3);
        field.wireType = static_cast<int>(tag & 7);
        if (field.number <= 0) return std::nullopt;
        switch (field.wireType) {
            case 0:
                if (!readVarint(data, &pos, &field.varint)) return std::nullopt;
                break;
            case 1:
                if (data.size() - pos < 8) return std::nullopt;
                field.bytes = data.mid(pos, 8);
                pos += 8;
                break;
            case 2: {
                quint64 length = 0;
                if (!readVarint(data, &pos, &length)) return std::nullopt;
                if (static_cast<quint64>(data.size() - pos) < length) return std::nullopt;
                field.bytes = data.mid(pos, static_cast<int>(length));
                pos += static_cast<int>(length);
                break;
            }
            case 5:
                if (data.size() - pos < 4) return std::nullopt;
                field.bytes = data.mid(pos, 4);
                pos += 4;
                break;
            default:
                return std::nullopt;
        }
        fields.append(field);
    }
    return fields;
}

ZstdStatus zstdDecompress(const QByteArray& compressed, QByteArray* out, QByteArray* detail) {
    const ZstdApi* api = loadZstd();
    if (!api) return ZstdStatus::Unavailable;
    const unsigned long long size = api->contentSize(compressed.constData(), static_cast<size_t>(compressed.size()));
    constexpr unsigned long long kUnknownSize = 0ULL - 1ULL;
    constexpr unsigned long long kError = 0ULL - 2ULL;
    if (size == kUnknownSize) return ZstdStatus::UnknownSize;
    if (size == kError) {
        if (detail) *detail = "invalid zstd frame";
        return ZstdStatus::Error;
    }
    out->resize(static_cast<int>(size));
    const size_t written = api->decompress(out->data(), static_cast<size_t>(size), compressed.constData(),
                                           static_cast<size_t>(compressed.size()));
    if (api->isError(written) || written != static_cast<size_t>(size)) {
        if (detail) *detail = QByteArray(api->errorName(written));
        out->clear();
        return ZstdStatus::Error;
    }
    return ZstdStatus::Ok;
}

}  // namespace Zalos::Core::Sync::V2
