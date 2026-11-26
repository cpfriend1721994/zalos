#include "sync/v2/sync_v2_frames.h"

#include <zstd.h>

#include <cstring>
#include <memory>

namespace Zalos::Core::Sync::V2 {

namespace {

QString utf8(QByteArrayView bytes) {
    return QString::fromUtf8(bytes.data(), bytes.size());
}

bool readVarint(QByteArrayView data, int* pos, quint64* value) {
    quint64 result = 0;
    for (int shift = 0; shift < 64; shift += 7) {
        if (*pos >= data.size()) return false;
        const quint8 byte = static_cast<quint8>(data[(*pos)++]);
        result |= static_cast<quint64>(byte & 0x7f) << shift;
        if (!(byte & 0x80)) {
            *value = result;
            return true;
        }
    }
    return false;
}

}

bool splitFrames(const QByteArray& data, QVector<QByteArrayView>* frames) {
    frames->clear();
    int pos = 0;
    while (pos < data.size()) {
        if (data.size() - pos < 4) return false;
        const auto* p = reinterpret_cast<const uchar*>(data.constData() + pos);
        const quint32 length = static_cast<quint32>(p[0]) | (static_cast<quint32>(p[1]) << 8) |
                               (static_cast<quint32>(p[2]) << 16) | (static_cast<quint32>(p[3]) << 24);
        pos += 4;
        if (static_cast<quint64>(data.size() - pos) < length) return false;
        frames->append(QByteArrayView(data).mid(pos, static_cast<qsizetype>(length)));
        pos += static_cast<int>(length);
    }
    return true;
}

std::optional<QVector<ProtoField>> parseProto(QByteArrayView data) {
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
                field.bytes = QByteArrayView(data.data() + pos, 8);
                pos += 8;
                break;
            case 2: {
                quint64 length = 0;
                if (!readVarint(data, &pos, &length)) return std::nullopt;
                if (static_cast<quint64>(data.size() - pos) < length) return std::nullopt;
                field.bytes = QByteArrayView(data.data() + pos, static_cast<qsizetype>(length));
                pos += static_cast<int>(length);
                break;
            }
            case 5:
                if (data.size() - pos < 4) return std::nullopt;
                field.bytes = QByteArrayView(data.data() + pos, 4);
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

ZstdStatus zstdDecompressStreaming(const QByteArray& compressed, unsigned long long maxSize, QByteArray* out,
                                   QByteArray* detail) {
    std::unique_ptr<ZSTD_DStream, decltype(&ZSTD_freeDStream)> stream(ZSTD_createDStream(), &ZSTD_freeDStream);
    auto fail = [out, detail](const QByteArray& why) {
        if (detail) *detail = why;
        out->clear();
        return ZstdStatus::Error;
    };
    if (!stream) return fail(QByteArrayLiteral("zstd stream allocation failed"));
    ZSTD_inBuffer in{compressed.constData(), static_cast<size_t>(compressed.size()), 0};
    const unsigned long long initial =
        qMax<unsigned long long>(static_cast<unsigned long long>(compressed.size()) * 4ULL, 64ULL * 1024ULL);
    out->resize(static_cast<qsizetype>(qMin<unsigned long long>(initial, maxSize)));
    size_t produced = 0;
    size_t remaining = 1;
    while (remaining != 0 || in.pos < in.size) {
        if (produced == static_cast<size_t>(out->size())) {
            if (static_cast<unsigned long long>(out->size()) >= maxSize) {
                return fail(QByteArrayLiteral("zstd frame too large"));
            }
            out->resize(static_cast<qsizetype>(
                qMin<unsigned long long>(static_cast<unsigned long long>(out->size()) * 2ULL, maxSize)));
        }
        ZSTD_outBuffer outBuf{out->data(), static_cast<size_t>(out->size()), produced};
        const size_t inBefore = in.pos;
        remaining = ZSTD_decompressStream(stream.get(), &outBuf, &in);
        if (ZSTD_isError(remaining)) return fail(QByteArray(ZSTD_getErrorName(remaining)));
        const bool progressed = outBuf.pos != produced || in.pos != inBefore;
        produced = outBuf.pos;
        if (!progressed && in.pos == in.size && remaining != 0) {
            return fail(QByteArrayLiteral("truncated zstd frame"));
        }
        if (!progressed && in.pos < in.size && produced < static_cast<size_t>(out->size())) {
            return fail(QByteArrayLiteral("stalled zstd stream"));
        }
    }
    out->resize(static_cast<qsizetype>(produced));
    return ZstdStatus::Ok;
}

}

ZstdStatus zstdDecompress(const QByteArray& compressed, QByteArray* out, QByteArray* detail) {
    if (!out) return ZstdStatus::Error;
    constexpr unsigned long long kMaxZstdSize = 64ULL * 1024 * 1024;
    return zstdDecompressStreaming(compressed, kMaxZstdSize, out, detail);
}

namespace {

std::optional<ParsedSyncV2Attach> parseAttach(QByteArrayView data) {
    const auto fields = parseProto(data);
    if (!fields.has_value()) return std::nullopt;
    ParsedSyncV2Attach att;
    for (const auto& f : *fields) {
        if (f.wireType == 0) {
            if (f.number == 9) att.catId = static_cast<int>(f.varint);
            else if (f.number == 10) att.id = static_cast<int>(f.varint);
        } else if (f.wireType == 2) {
            const QString s = utf8(f.bytes);
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

std::optional<ParsedSyncV2Mention> parseMention(QByteArrayView data) {
    const auto fields = parseProto(data);
    if (!fields.has_value()) return std::nullopt;
    ParsedSyncV2Mention mention;
    for (const auto& f : *fields) {
        if (f.wireType == 2 && f.number == 1) {
            mention.uid = utf8(f.bytes);
        } else if (f.wireType == 0) {
            if (f.number == 2) mention.pos = static_cast<int>(static_cast<qint32>(f.varint));
            else if (f.number == 3) mention.len = static_cast<int>(static_cast<qint32>(f.varint));
            else if (f.number == 4) mention.type = static_cast<int>(static_cast<qint32>(f.varint));
        }
    }
    return mention;
}

std::optional<ParsedSyncV2Quote> parseQuote(QByteArrayView data) {
    const auto fields = parseProto(data);
    if (!fields.has_value()) return std::nullopt;
    ParsedSyncV2Quote quote;
    for (const auto& f : *fields) {
        if (f.wireType == 2) {
            if (f.number == 1) quote.attach = utf8(f.bytes);
            else if (f.number == 2) quote.ownerId = utf8(f.bytes);
            else if (f.number == 4) quote.fromD = utf8(f.bytes);
            else if (f.number == 7) quote.msg = utf8(f.bytes);
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

std::optional<ParsedSyncV2Conversation> parseConversation(QByteArrayView data) {
    const auto fields = parseProto(data);
    if (!fields.has_value()) return std::nullopt;
    ParsedSyncV2Conversation conv;
    bool hasConvType = false;
    for (const auto& f : *fields) {
        if (f.wireType == 2 && f.number == 1) {
            conv.convId = utf8(f.bytes);
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

std::optional<ParsedSyncV2Message> parseMessage(QByteArrayView data) {
    const auto fields = parseProto(data);
    if (!fields.has_value()) return std::nullopt;
    ParsedSyncV2Message msg;
    for (const auto& f : *fields) {
        if (f.wireType == 0) {
            if (f.number == 2) msg.clientId = f.varint;
            else if (f.number == 3) msg.msgStatus = static_cast<int>(f.varint);
            else if (f.number == 4) msg.msgType = static_cast<int>(f.varint);
            else if (f.number == 5) msg.timestamp = f.varint;
        } else if (f.wireType == 2) {
            if (f.number == 1) msg.senderId = utf8(f.bytes);
            else if (f.number == 6) msg.globalId = utf8(f.bytes);
            else if (f.number == 7) msg.content = utf8(f.bytes);
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

std::optional<ParsedSyncV2Partition> parsePartition(QByteArrayView data) {
    const auto fields = parseProto(data);
    if (!fields.has_value()) return std::nullopt;
    ParsedSyncV2Partition part;
    for (const auto& f : *fields) {
        if (f.number == 1 && f.wireType == 2) part.id = utf8(f.bytes);
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
