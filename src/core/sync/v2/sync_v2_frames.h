#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include <optional>

namespace Zalos::Core::Sync::V2 {

struct ProtoField {
    int number{0};
    int wireType{0};
    quint64 varint{0};
    QByteArray bytes;
};

struct ParsedSyncV2Attach {
    QString attachType;
    QString action;
    QString params;
    QString title;
    QString href;
    QString thumb;
    QString description;
    int catId{0};
    int id{0};
};

struct ParsedSyncV2Mention {
    QString uid;
    int pos{0};
    int len{0};
    int type{0};
};

struct ParsedSyncV2Quote {
    QString attach;
    QString ownerId;
    quint64 ts{0};
    QString fromD;
    quint64 cliMsgId{0};
    quint64 globalMsgId{0};
    QString msg;
    int cliMsgType{0};
    quint64 ttl{0};
};

struct ParsedSyncV2Message {
    QString senderId;
    quint64 clientId{0};
    int msgStatus{0};
    int msgType{0};
    quint64 timestamp{0};
    QString globalId;
    QString content;
    QVector<ParsedSyncV2Attach> attaches;
    QVector<ParsedSyncV2Mention> mentions;
    std::optional<ParsedSyncV2Quote> quote;
    quint64 ttl{0};
};

struct ParsedSyncV2Partition {
    QString id;
    QVector<ParsedSyncV2Message> messages;
};

struct ParsedSyncV2Conversation {
    QString convId;
    int convType{0};
    bool respondedByMe{false};
    quint64 lastTs{0};
    quint64 lastGlobalId{0};
    quint64 lastClientId{0};
};

bool splitFrames(const QByteArray& data, QVector<QByteArray>* frames);

std::optional<QVector<ProtoField>> parseProto(const QByteArray& data);

bool parseSyncV2Chunk(const QByteArray& data, QVector<ParsedSyncV2Partition>* partitions);

bool parseSyncV2ConversationChunk(const QByteArray& data,
                                  QVector<ParsedSyncV2Conversation>* conversations);

enum class ZstdStatus { Ok, Unavailable, Error, UnknownSize };

ZstdStatus zstdDecompress(const QByteArray& compressed, QByteArray* out, QByteArray* detail);

}  // namespace Zalos::Core::Sync::V2
