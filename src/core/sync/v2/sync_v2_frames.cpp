#include "sync/v2/sync_v2_frames.h"

#ifdef ZALOS_HAVE_SYSTEM_ZSTD
#include <zstd.h>
#else
#include <QLibrary>
#endif

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

// Same layout as ZSTD_inBuffer / ZSTD_outBuffer (stable since zstd 1.0), so the dlopen path needs no header.
struct ZstdInBuffer {
    const void* src;
    size_t size;
    size_t pos;
};
struct ZstdOutBuffer {
    void* dst;
    size_t size;
    size_t pos;
};

struct ZstdApi {
    using DecompressFn = size_t (*)(void*, size_t, const void*, size_t);
    using ContentSizeFn = unsigned long long (*)(const void*, size_t);
    using IsErrorFn = unsigned (*)(size_t);
    using ErrorNameFn = const char* (*)(size_t);
    using CreateStreamFn = void* (*)();
    using FreeStreamFn = size_t (*)(void*);
    using StreamFn = size_t (*)(void*, ZstdOutBuffer*, ZstdInBuffer*);

    DecompressFn decompress{nullptr};
    ContentSizeFn contentSize{nullptr};
    IsErrorFn isError{nullptr};
    ErrorNameFn errorName{nullptr};
    CreateStreamFn createStream{nullptr};
    FreeStreamFn freeStream{nullptr};
    StreamFn decompressStream{nullptr};
};

const ZstdApi* loadZstd() {
#ifdef ZALOS_HAVE_SYSTEM_ZSTD
    static_assert(sizeof(ZstdInBuffer) == sizeof(ZSTD_inBuffer) && sizeof(ZstdOutBuffer) == sizeof(ZSTD_outBuffer));
    static const ZstdApi api{
        &ZSTD_decompress,
        &ZSTD_getFrameContentSize,
        &ZSTD_isError,
        &ZSTD_getErrorName,
        [] { return static_cast<void*>(ZSTD_createDStream()); },
        [](void* ds) { return ZSTD_freeDStream(static_cast<ZSTD_DStream*>(ds)); },
        [](void* ds, ZstdOutBuffer* out, ZstdInBuffer* in) {
            return ZSTD_decompressStream(static_cast<ZSTD_DStream*>(ds), reinterpret_cast<ZSTD_outBuffer*>(out),
                                         reinterpret_cast<ZSTD_inBuffer*>(in));
        },
    };
    return &api;
#else
    static const ZstdApi api = [] {
        ZstdApi a;
        auto tryLoad = [&](QLibrary& lib) {
            lib.setLoadHints(QLibrary::ExportExternalSymbolsHint);
            if (!lib.load()) return false;
            a.decompress = reinterpret_cast<ZstdApi::DecompressFn>(lib.resolve("ZSTD_decompress"));
            a.contentSize = reinterpret_cast<ZstdApi::ContentSizeFn>(lib.resolve("ZSTD_getFrameContentSize"));
            a.isError = reinterpret_cast<ZstdApi::IsErrorFn>(lib.resolve("ZSTD_isError"));
            a.errorName = reinterpret_cast<ZstdApi::ErrorNameFn>(lib.resolve("ZSTD_getErrorName"));
            a.createStream = reinterpret_cast<ZstdApi::CreateStreamFn>(lib.resolve("ZSTD_createDStream"));
            a.freeStream = reinterpret_cast<ZstdApi::FreeStreamFn>(lib.resolve("ZSTD_freeDStream"));
            a.decompressStream = reinterpret_cast<ZstdApi::StreamFn>(lib.resolve("ZSTD_decompressStream"));
            if (!a.decompress || !a.contentSize || !a.isError || !a.errorName || !a.createStream || !a.freeStream ||
                !a.decompressStream) {
                a = ZstdApi{};
                return false;
            }
            return true;
        };
        QLibrary versioned(QStringLiteral("zstd"), 1);
        if (!tryLoad(versioned)) {
            QLibrary unversioned(QStringLiteral("zstd"));
            tryLoad(unversioned);
        }
        return a;
    }();
    return api.decompress ? &api : nullptr;
#endif
}

}

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

namespace {

// Frames written by a streaming encoder carry no content size, so grow the output until the frame ends.
ZstdStatus zstdDecompressStreaming(const ZstdApi& api, const QByteArray& compressed, unsigned long long maxSize,
                                   QByteArray* out, QByteArray* detail) {
    void* stream = api.createStream();
    if (!stream) {
        if (detail) *detail = QByteArrayLiteral("zstd stream allocation failed");
        return ZstdStatus::Error;
    }
    ZstdInBuffer in{compressed.constData(), static_cast<size_t>(compressed.size()), 0};
    out->resize(qMax<qsizetype>(compressed.size() * 4, 64 * 1024));
    size_t produced = 0;
    size_t remaining = 1;
    while (remaining != 0) {
        if (produced == static_cast<size_t>(out->size())) {
            if (static_cast<unsigned long long>(out->size()) >= maxSize) {
                if (detail) *detail = QByteArrayLiteral("zstd frame too large");
                out->clear();
                api.freeStream(stream);
                return ZstdStatus::Error;
            }
            out->resize(static_cast<qsizetype>(qMin<unsigned long long>(out->size() * 2ULL, maxSize)));
        }
        ZstdOutBuffer outBuf{out->data(), static_cast<size_t>(out->size()), produced};
        const size_t inBefore = in.pos;
        remaining = api.decompressStream(stream, &outBuf, &in);
        if (api.isError(remaining)) {
            if (detail) *detail = QByteArray(api.errorName(remaining));
            out->clear();
            api.freeStream(stream);
            return ZstdStatus::Error;
        }
        const bool progressed = outBuf.pos != produced || in.pos != inBefore;
        produced = outBuf.pos;
        if (remaining != 0 && in.pos == in.size && !progressed) {
            if (detail) *detail = QByteArrayLiteral("truncated zstd frame");
            out->clear();
            api.freeStream(stream);
            return ZstdStatus::Error;
        }
    }
    api.freeStream(stream);
    out->resize(static_cast<qsizetype>(produced));
    return ZstdStatus::Ok;
}

}  // namespace

ZstdStatus zstdDecompress(const QByteArray& compressed, QByteArray* out, QByteArray* detail) {
    if (!out) return ZstdStatus::Error;
    const ZstdApi* api = loadZstd();
    if (!api) return ZstdStatus::Unavailable;
    const unsigned long long size = api->contentSize(compressed.constData(), static_cast<size_t>(compressed.size()));
    constexpr unsigned long long kUnknownSize = 0ULL - 1ULL;
    constexpr unsigned long long kError = 0ULL - 2ULL;
    constexpr unsigned long long kMaxZstdSize = 64ULL * 1024 * 1024;
    if (size == kUnknownSize) return zstdDecompressStreaming(*api, compressed, kMaxZstdSize, out, detail);
    if (size == kError || size > kMaxZstdSize) {
        if (detail) *detail = size > kMaxZstdSize ? QByteArrayLiteral("zstd frame too large") : QByteArrayLiteral("invalid zstd frame");
        return ZstdStatus::Error;
    }
    out->resize(static_cast<qsizetype>(size));
    const size_t written = api->decompress(out->data(), static_cast<size_t>(size), compressed.constData(),
                                           static_cast<size_t>(compressed.size()));
    if (api->isError(written) || written != static_cast<size_t>(size)) {
        if (detail) *detail = QByteArray(api->errorName(written));
        out->clear();
        return ZstdStatus::Error;
    }
    return ZstdStatus::Ok;
}

namespace {

std::optional<ParsedSyncV2Attach> parseAttach(const QByteArray& data) {
    const auto fields = parseProto(data);
    if (!fields.has_value()) return std::nullopt;
    ParsedSyncV2Attach att;
    for (const auto& f : *fields) {
        if (f.wireType == 0) {
            if (f.number == 9) att.catId = static_cast<int>(f.varint);
            else if (f.number == 10) att.id = static_cast<int>(f.varint);
        } else if (f.wireType == 2) {
            const QString s = QString::fromUtf8(f.bytes);
            if (f.number == 1) att.attachType = s;
            else if (f.number == 3) att.action = s;
            else if (f.number == 4) att.params = s;
            else if (f.number == 5) att.title = s;
            else if (f.number == 6) att.href = s;
            else if (f.number == 7) att.thumb = s;
            else if (f.number == 8) att.description = s;
        }
    }
    return att;
}

std::optional<ParsedSyncV2Mention> parseMention(const QByteArray& data) {
    const auto fields = parseProto(data);
    if (!fields.has_value()) return std::nullopt;
    ParsedSyncV2Mention mention;
    for (const auto& f : *fields) {
        if (f.wireType == 2 && f.number == 1) {
            mention.uid = QString::fromUtf8(f.bytes);
        } else if (f.wireType == 0) {
            if (f.number == 2) mention.pos = static_cast<int>(static_cast<qint32>(f.varint));
            else if (f.number == 3) mention.len = static_cast<int>(static_cast<qint32>(f.varint));
            else if (f.number == 4) mention.type = static_cast<int>(static_cast<qint32>(f.varint));
        }
    }
    return mention;
}

std::optional<ParsedSyncV2Quote> parseQuote(const QByteArray& data) {
    const auto fields = parseProto(data);
    if (!fields.has_value()) return std::nullopt;
    ParsedSyncV2Quote quote;
    for (const auto& f : *fields) {
        if (f.wireType == 2) {
            if (f.number == 1) quote.attach = QString::fromUtf8(f.bytes);
            else if (f.number == 2) quote.ownerId = QString::fromUtf8(f.bytes);
            else if (f.number == 4) quote.fromD = QString::fromUtf8(f.bytes);
            else if (f.number == 7) quote.msg = QString::fromUtf8(f.bytes);
        } else if (f.wireType == 0) {
            if (f.number == 3) quote.ts = f.varint;
            else if (f.number == 5) quote.cliMsgId = f.varint;
            else if (f.number == 6) quote.globalMsgId = f.varint;
            else if (f.number == 8) quote.cliMsgType = static_cast<int>(static_cast<qint32>(f.varint));
            else if (f.number == 9) quote.ttl = f.varint;
        }
    }
    return quote;
}

std::optional<ParsedSyncV2Conversation> parseConversation(const QByteArray& data) {
    const auto fields = parseProto(data);
    if (!fields.has_value()) return std::nullopt;
    ParsedSyncV2Conversation conv;
    bool hasConvType = false;
    for (const auto& f : *fields) {
        if (f.wireType == 2 && f.number == 1) {
            conv.convId = QString::fromUtf8(f.bytes);
        } else if (f.wireType == 0) {
            if (f.number == 2) {
                conv.convType = static_cast<int>(static_cast<qint32>(f.varint));
                hasConvType = true;
            }
            else if (f.number == 3) conv.respondedByMe = f.varint != 0;
            else if (f.number == 4) conv.lastTs = f.varint;
            else if (f.number == 5) conv.lastGlobalId = f.varint;
            else if (f.number == 6) conv.lastClientId = f.varint;
        } else if (f.number == 2 && f.wireType == 2) {
            return std::nullopt;
        }
    }
    if (conv.convId.isEmpty() || !hasConvType || (conv.convType != 1 && conv.convType != 2)) {
        return std::nullopt;
    }
    return conv;
}

std::optional<ParsedSyncV2Message> parseMessage(const QByteArray& data) {
    const auto fields = parseProto(data);
    if (!fields.has_value()) return std::nullopt;
    ParsedSyncV2Message msg;
    for (const auto& f : *fields) {
        if (f.wireType == 0) {
            if (f.number == 2) msg.clientId = f.varint;
            else if (f.number == 3) msg.msgStatus = static_cast<int>(f.varint);
            else if (f.number == 4) msg.msgType = static_cast<int>(f.varint);
            else if (f.number == 5) msg.timestamp = f.varint;
            // globalId is an int64 on the wire; read it as an unsigned 64-bit integer to keep every digit.
            else if (f.number == 6) msg.globalId = QString::number(f.varint);
        } else if (f.wireType == 2) {
            if (f.number == 1) msg.senderId = QString::fromUtf8(f.bytes);
            else if (f.number == 6) msg.globalId = QString::fromUtf8(f.bytes);
            else if (f.number == 7) msg.content = QString::fromUtf8(f.bytes);
            else if (f.number == 8) {
                const auto metaFields = parseProto(f.bytes);
                if (metaFields.has_value()) {
                    for (const auto& mf : *metaFields) {
                        if (mf.wireType == 2) {
                            if (mf.number == 1) {
                                if (const auto att = parseAttach(mf.bytes)) msg.attaches.append(*att);
                            } else if (mf.number == 2) {
                                if (const auto mention = parseMention(mf.bytes)) msg.mentions.append(*mention);
                            } else if (mf.number == 4) {
                                if (const auto quote = parseQuote(mf.bytes)) msg.quote = quote;
                            }
                        } else if (mf.wireType == 0 && mf.number == 6) {
                            msg.ttl = mf.varint;
                        }
                    }
                }
            }
        }
    }
    return msg;
}

std::optional<ParsedSyncV2Partition> parsePartition(const QByteArray& data) {
    const auto fields = parseProto(data);
    if (!fields.has_value()) return std::nullopt;
    ParsedSyncV2Partition part;
    for (const auto& f : *fields) {
        if (f.number == 1 && f.wireType == 2) part.id = QString::fromUtf8(f.bytes);
        else if (f.number == 2 && f.wireType == 2) {
            if (const auto msg = parseMessage(f.bytes)) part.messages.append(*msg);
        }
    }
    return part;
}

}

bool parseSyncV2Chunk(const QByteArray& data, QVector<ParsedSyncV2Partition>* partitions) {
    if (!partitions) return false;
    const auto fields = parseProto(data);
    if (!fields.has_value()) return false;
    for (const auto& f : *fields) {
        if (f.number == 2 && f.wireType == 2) {
            if (const auto part = parsePartition(f.bytes)) partitions->append(*part);
        }
    }
    return true;
}

bool parseSyncV2ConversationChunk(const QByteArray& data,
                                  QVector<ParsedSyncV2Conversation>* conversations) {
    if (!conversations) return false;
    const auto fields = parseProto(data);
    if (!fields.has_value()) return false;
    for (const auto& f : *fields) {
        if (f.number == 2 && f.wireType == 2) {
            const auto conv = parseConversation(f.bytes);
            if (!conv.has_value()) return false;
            conversations->append(*conv);
        }
    }
    return true;
}

}
