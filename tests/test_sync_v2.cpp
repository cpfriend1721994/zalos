#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QTest>

#include "sync/v2/sync_v2_crypto.h"
#include "sync/v2/sync_v2_fallback.h"
#include "sync/v2/sync_v2_frames.h"

using namespace Zalos::Core::Sync::V2;

namespace {

QByteArray bytesOf(std::initializer_list<unsigned char> values) {
    QByteArray out;
    for (unsigned char v : values) out.append(static_cast<char>(v));
    return out;
}

}  // namespace

class TestSyncV2 : public QObject {
    Q_OBJECT

private slots:
    void framesSplitInLittleEndianLength();
    void framesRejectTruncatedBody();
    void framesRejectOversizedLength();
    void protoParsesVarintBytesAndFixed32();
    void protoRejectsTruncatedAndGroups();
    void zstdRawBlockFrame();
    void zstdRejectsGarbageOrSkipsWhenMissing();
    void messageVectorsDecrypt();
    void messageNegativeVectorRejected();
    void messageBadInputSizes();
    void metadataVectorsDecrypt();
    void metadataNegativeVectorRejected();
    void keyPairAndAgreementRoundTrip();
    void protoSyncV2ChunkParsesPartitionAndMessage();
    void protoSyncV2ConversationAndMetaFields();
    void fallbackDecisions();
};

void TestSyncV2::framesSplitInLittleEndianLength() {
    const QByteArray data = bytesOf({3, 0, 0, 0, 'a', 'b', 'c', 0, 0, 0, 0});
    QVector<QByteArray> frames;
    QVERIFY(splitFrames(data, &frames));
    QCOMPARE(frames.size(), 2);
    QCOMPARE(frames.at(0), QByteArray("abc"));
    QVERIFY(frames.at(1).isEmpty());
}

void TestSyncV2::framesRejectTruncatedBody() {
    QVector<QByteArray> frames;
    QVERIFY(!splitFrames(bytesOf({4, 0, 0, 0, 'a'}), &frames));
    QVERIFY(!splitFrames(bytesOf({1, 0}), &frames));
}

void TestSyncV2::framesRejectOversizedLength() {
    QVector<QByteArray> frames;
    QVERIFY(!splitFrames(bytesOf({0xff, 0xff, 0xff, 0x7f, 'a', 'b'}), &frames));
    QVERIFY(!splitFrames(bytesOf({0x01, 0x00, 0x00, 0x04, 'a', 'b'}), &frames));
}

void TestSyncV2::protoParsesVarintBytesAndFixed32() {
    const QByteArray data = bytesOf({0x08, 0x96, 0x01, 0x12, 0x02, 0x61, 0x62, 0x1d, 0x01, 0x00, 0x00, 0x00});
    const auto fields = parseProto(data);
    QVERIFY(fields.has_value());
    QCOMPARE(fields->size(), 3);
    QCOMPARE(fields->at(0).number, 1);
    QCOMPARE(fields->at(0).wireType, 0);
    QCOMPARE(fields->at(0).varint, quint64(150));
    QCOMPARE(fields->at(1).number, 2);
    QCOMPARE(fields->at(1).bytes, QByteArray("ab"));
    QCOMPARE(fields->at(2).number, 3);
    QCOMPARE(fields->at(2).wireType, 5);
    QCOMPARE(fields->at(2).bytes.size(), 4);
}

void TestSyncV2::protoRejectsTruncatedAndGroups() {
    QVERIFY(!parseProto(bytesOf({0x12, 0x05, 0x61})).has_value());
    QVERIFY(!parseProto(bytesOf({0x0b})).has_value());
    QVERIFY(!parseProto(bytesOf({0x08})).has_value());
    QVERIFY(!parseProto(bytesOf({0x00, 0x01})).has_value());
}

void TestSyncV2::zstdRawBlockFrame() {
    // Frame with one raw block holding "hello": magic, single-segment header with content size 5,
    // block header (last, raw, size 5).
    const QByteArray frame = bytesOf({0x28, 0xb5, 0x2f, 0xfd, 0x20, 0x05, 0x29, 0x00, 0x00, 'h', 'e', 'l', 'l', 'o'});
    QByteArray out;
    QByteArray detail;
    const ZstdStatus status = zstdDecompress(frame, &out, &detail);
    if (status == ZstdStatus::Unavailable) QSKIP("libzstd.so.1 is not installed in this environment");
    QCOMPARE(static_cast<int>(status), static_cast<int>(ZstdStatus::Ok));
    QCOMPARE(out, QByteArray("hello"));
}

void TestSyncV2::zstdRejectsGarbageOrSkipsWhenMissing() {
    QByteArray out;
    QByteArray detail;
    const ZstdStatus status = zstdDecompress(bytesOf({0x01, 0x02, 0x03, 0x04, 0x05, 0x06}), &out, &detail);
    if (status == ZstdStatus::Unavailable) QSKIP("libzstd.so.1 is not installed in this environment");
    QCOMPARE(static_cast<int>(status), static_cast<int>(ZstdStatus::Error));
    QVERIFY(out.isEmpty());
}

void TestSyncV2::messageVectorsDecrypt() {
    QFile file(QStringLiteral(ZALOS_SOURCE_DIR) + QStringLiteral("/tests/data/sync_v2/message_v1.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonArray cases = doc.object().value(QStringLiteral("cases")).toArray();
    QVERIFY(cases.size() >= 3);
    int checked = 0;
    for (const QJsonValue& caseValue : cases) {
        const QJsonObject c = caseValue.toObject();
        SessionRecord record;
        record.sessionId = QByteArray::fromHex(c.value("sessionId").toString().toLatin1());
        record.key = QByteArray::fromHex(c.value("recordKey").toString().toLatin1());
        record.recordId = c.value("recordId").toString().toULongLong();
        for (const QJsonValue& m : c.value("messages").toArray()) {
            const QJsonObject mo = m.toObject();
            QByteArray plain;
            const CryptoStatus status =
                openMessage(record, QByteArray::fromHex(mo.value("ciphertext").toString().toLatin1()), &plain);
            QCOMPARE(cryptoStatusName(status), QStringLiteral("ok"));
            QCOMPARE(plain, QByteArray::fromHex(mo.value("plaintext").toString().toLatin1()));
            ++checked;
        }
    }
    QCOMPARE(checked, 9);
}

void TestSyncV2::messageNegativeVectorRejected() {
    QFile file(QStringLiteral(ZALOS_SOURCE_DIR) + QStringLiteral("/tests/data/sync_v2/message_v1.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject neg = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("negative")).toObject();
    SessionRecord record;
    record.sessionId = QByteArray::fromHex(neg.value("sessionId").toString().toLatin1());
    record.key = QByteArray::fromHex(neg.value("recordKey").toString().toLatin1());
    const QByteArray idLe = QByteArray::fromHex(neg.value("recordIdLe").toString().toLatin1());
    quint64 id = 0;
    for (int i = 7; i >= 0; --i) id = (id << 8) | static_cast<quint8>(idLe.at(i));
    record.recordId = id;
    QByteArray plain;
    const CryptoStatus status = openMessage(record, QByteArray::fromHex(neg.value("ciphertext").toString().toLatin1()), &plain);
    QCOMPARE(cryptoStatusName(status), QStringLiteral("auth_failed"));
    QVERIFY(plain.isEmpty());
}

void TestSyncV2::messageBadInputSizes() {
    SessionRecord record;
    record.sessionId = QByteArray(31, 'x');
    record.key = QByteArray(32, 'k');
    QByteArray plain;
    QCOMPARE(cryptoStatusName(openMessage(record, QByteArray(80, 'c'), &plain)), QStringLiteral("bad_input"));
    record.sessionId = QByteArray(32, 'x');
    QCOMPARE(cryptoStatusName(openMessage(record, QByteArray(59, 'c'), &plain)), QStringLiteral("bad_input"));
}

void TestSyncV2::metadataVectorsDecrypt() {
    QFile file(QStringLiteral(ZALOS_SOURCE_DIR) + QStringLiteral("/tests/data/sync_v2/metadata_v1.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonArray cases = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("cases")).toArray();
    QVERIFY(cases.size() >= 3);
    for (const QJsonValue& v : cases) {
        const QJsonObject c = v.toObject();
        const QByteArray rIkPub = QByteArray::fromHex(c.value("receiverIdentityPub").toString().toLatin1());
        const QByteArray rIkPriv = QByteArray::fromHex(c.value("receiverIdentityPriv").toString().toLatin1());
        const QByteArray rEkPriv = QByteArray::fromHex(c.value("receiverEphemeralPriv").toString().toLatin1());
        const QByteArray env = QByteArray::fromHex(c.value("envelope").toString().toLatin1());
        MetadataPlain out;
        QCOMPARE(cryptoStatusName(openMetadata(env, rIkPub, rIkPriv, rEkPriv, &out)), QStringLiteral("ok"));
        QCOMPARE(out.plaintext, QByteArray::fromHex(c.value("plaintext").toString().toLatin1()));
        QCOMPARE(out.session.sessionId, QByteArray::fromHex(c.value("sessionId").toString().toLatin1()));
        QCOMPARE(out.session.key, QByteArray::fromHex(c.value("sessionKey").toString().toLatin1()));
        QCOMPARE(out.session.recordId, c.value("recordId").toString().toULongLong());
        QCOMPARE(out.senderIdentity, QByteArray::fromHex(c.value("senderIdentityPub").toString().toLatin1()));
    }
}

void TestSyncV2::metadataNegativeVectorRejected() {
    QFile file(QStringLiteral(ZALOS_SOURCE_DIR) + QStringLiteral("/tests/data/sync_v2/metadata_v1.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject c = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("cases")).toArray().at(0).toObject();
    const QByteArray rIkPub = QByteArray::fromHex(c.value("receiverIdentityPub").toString().toLatin1());
    const QByteArray rIkPriv = QByteArray::fromHex(c.value("receiverIdentityPriv").toString().toLatin1());
    const QByteArray rEkPriv = QByteArray::fromHex(c.value("receiverEphemeralPriv").toString().toLatin1());
    QByteArray env = QByteArray::fromHex(c.value("envelope").toString().toLatin1());
    env[env.size() - 20] = static_cast<char>(env.at(env.size() - 20) ^ 0xff);
    MetadataPlain out;
    QCOMPARE(cryptoStatusName(openMetadata(env, rIkPub, rIkPriv, rEkPriv, &out)), QStringLiteral("auth_failed"));
    QVERIFY(out.plaintext.isEmpty());
}

void TestSyncV2::keyPairAndAgreementRoundTrip() {
    const KeyPair a = generateKeyPair();
    const KeyPair b = generateKeyPair();
    QCOMPARE(a.pub.size(), 33);
    QCOMPARE(static_cast<unsigned char>(a.pub.at(0)), static_cast<unsigned char>(0x05));
    QCOMPARE(a.priv.size(), 32);
    QCOMPARE(b.pub.size(), 33);
    QCOMPARE(b.priv.size(), 32);
    const QByteArray sab = calculateAgreement(a.priv, b.pub);
    const QByteArray sba = calculateAgreement(b.priv, a.pub);
    QCOMPARE(sab.size(), 32);
    QCOMPARE(sab, sba);
}

void TestSyncV2::protoSyncV2ChunkParsesPartitionAndMessage() {
    auto fieldBytes = [](int fn, const QByteArray& body) {
        QByteArray out;
        out.append(static_cast<char>((fn << 3) | 2));
        int len = body.size();
        while (len >= 0x80) {
            out.append(static_cast<char>((len & 0x7f) | 0x80));
            len >>= 7;
        }
        out.append(static_cast<char>(len));
        out.append(body);
        return out;
    };
    auto fieldVarint = [](int fn, quint64 v) {
        QByteArray out;
        out.append(static_cast<char>(fn << 3));
        while (v >= 0x80) {
            out.append(static_cast<char>((v & 0x7f) | 0x80));
            v >>= 7;
        }
        out.append(static_cast<char>(v));
        return out;
    };

    const QByteArray attach = fieldBytes(5, QByteArray("photo.jpg")) + fieldBytes(6, QByteArray("https://example/p.jpg"));
    const QByteArray meta = fieldBytes(1, attach);
    const QByteArray msg = fieldBytes(1, QByteArray("oneone/sender1"))
                         + fieldVarint(2, 1001)
                         + fieldVarint(4, 3)
                         + fieldVarint(5, 1700000000000ULL)
                         + fieldBytes(6, QByteArray("g999"))
                         + fieldBytes(7, QByteArray("xin chao"))
                         + fieldBytes(8, meta);
    const QByteArray part = fieldBytes(1, QByteArray("oneone/peer1")) + fieldBytes(2, msg);
    const QByteArray chunk = fieldBytes(2, part);

    QVector<ParsedSyncV2Partition> parts;
    QVERIFY(parseSyncV2Chunk(chunk, &parts));
    QCOMPARE(parts.size(), 1);
    QCOMPARE(parts.at(0).id, QStringLiteral("oneone/peer1"));
    QCOMPARE(parts.at(0).messages.size(), 1);
    const ParsedSyncV2Message& m = parts.at(0).messages.at(0);
    QCOMPARE(m.senderId, QStringLiteral("oneone/sender1"));
    QCOMPARE(m.clientId, quint64(1001));
    QCOMPARE(m.msgType, 3);
    QCOMPARE(m.timestamp, quint64(1700000000000ULL));
    QCOMPARE(m.globalId, QStringLiteral("g999"));
    QCOMPARE(m.content, QStringLiteral("xin chao"));
    QCOMPARE(m.attaches.size(), 1);
    QCOMPARE(m.attaches.at(0).title, QStringLiteral("photo.jpg"));
    QCOMPARE(m.attaches.at(0).href, QStringLiteral("https://example/p.jpg"));
}

void TestSyncV2::protoSyncV2ConversationAndMetaFields() {
    auto fieldBytes = [](int fn, const QByteArray& body) {
        QByteArray out;
        out.append(static_cast<char>((fn << 3) | 2));
        int len = body.size();
        while (len >= 0x80) {
            out.append(static_cast<char>((len & 0x7f) | 0x80));
            len >>= 7;
        }
        out.append(static_cast<char>(len));
        out.append(body);
        return out;
    };
    auto fieldVarint = [](int fn, quint64 v) {
        QByteArray out;
        out.append(static_cast<char>(fn << 3));
        while (v >= 0x80) {
            out.append(static_cast<char>((v & 0x7f) | 0x80));
            v >>= 7;
        }
        out.append(static_cast<char>(v));
        return out;
    };

    const QByteArray c1 = fieldBytes(1, QByteArray("CONV1")) + fieldVarint(2, 1) + fieldVarint(3, 1)
                          + fieldVarint(4, 1700000100000ULL) + fieldVarint(5, 55) + fieldVarint(6, 99);
    const QByteArray c2 = fieldBytes(1, QByteArray("GRP2")) + fieldVarint(2, 2) + fieldVarint(4, 1700000200000ULL);
    const QByteArray convChunk = fieldBytes(2, c1) + fieldBytes(2, c2);

    QVector<ParsedSyncV2Conversation> convs;
    QVERIFY(parseSyncV2ConversationChunk(convChunk, &convs));
    QCOMPARE(convs.size(), 2);
    QCOMPARE(convs.at(0).convId, QStringLiteral("CONV1"));
    QCOMPARE(convs.at(0).convType, 1);
    QVERIFY(convs.at(0).respondedByMe);
    QCOMPARE(convs.at(0).lastTs, quint64(1700000100000ULL));
    QCOMPARE(convs.at(0).lastGlobalId, quint64(55));
    QCOMPARE(convs.at(0).lastClientId, quint64(99));
    QCOMPARE(convs.at(1).convId, QStringLiteral("GRP2"));
    QCOMPARE(convs.at(1).convType, 2);

    const QByteArray mention = fieldBytes(1, QByteArray("u-ment")) + fieldVarint(2, 1) + fieldVarint(3, 5)
                               + fieldVarint(4, 0);
    const QByteArray quote = fieldBytes(2, QByteArray("oneone/u-owner")) + fieldVarint(3, 1700000000000ULL)
                             + fieldBytes(4, QByteArray("Owner Name")) + fieldVarint(5, 321) + fieldVarint(6, 654)
                             + fieldBytes(7, QByteArray("quoted text")) + fieldVarint(8, 1) + fieldVarint(9, 1000);
    const QByteArray meta = fieldBytes(2, mention) + fieldBytes(4, quote) + fieldVarint(6, 60000);
    const QByteArray msg = fieldBytes(1, QByteArray("oneone/u-sender")) + fieldVarint(2, 2002)
                         + fieldVarint(4, 1) + fieldVarint(5, 1700000050000ULL)
                         + fieldBytes(6, QByteArray("g2002")) + fieldBytes(7, QByteArray("@Owner hi"))
                         + fieldBytes(8, meta);
    const QByteArray msgChunk = fieldBytes(2, fieldBytes(1, QByteArray("oneone/u-peer")) + fieldBytes(2, msg));

    QVector<ParsedSyncV2Conversation> rejected;
    QVERIFY(!parseSyncV2ConversationChunk(msgChunk, &rejected));

    QVector<ParsedSyncV2Partition> parts;
    QVERIFY(parseSyncV2Chunk(msgChunk, &parts));
    QCOMPARE(parts.size(), 1);
    const ParsedSyncV2Message& parsed = parts.at(0).messages.at(0);
    QCOMPARE(parsed.ttl, quint64(60000));
    QCOMPARE(parsed.mentions.size(), 1);
    QCOMPARE(parsed.mentions.at(0).uid, QStringLiteral("u-ment"));
    QCOMPARE(parsed.mentions.at(0).pos, 1);
    QCOMPARE(parsed.mentions.at(0).len, 5);
    QVERIFY(parsed.quote.has_value());
    QCOMPARE(parsed.quote->ownerId, QStringLiteral("oneone/u-owner"));
    QCOMPARE(parsed.quote->globalMsgId, quint64(654));
    QCOMPARE(parsed.quote->cliMsgId, quint64(321));
    QCOMPARE(parsed.quote->msg, QStringLiteral("quoted text"));
    QCOMPARE(parsed.quote->fromD, QStringLiteral("Owner Name"));
    QCOMPARE(parsed.quote->ttl, quint64(1000));
}

void TestSyncV2::fallbackDecisions() {
    using T = FallbackTrigger;
    QCOMPARE(static_cast<int>(decideFallback(211, false, false)), static_cast<int>(T::PullRejected211));
    QCOMPARE(static_cast<int>(decideFallback(0, true, true)), static_cast<int>(T::ConfirmTimeoutAfterV2));
    QCOMPARE(static_cast<int>(decideFallback(0, true, false)), static_cast<int>(T::None));
    QCOMPARE(static_cast<int>(decideFallback(500, false, true)), static_cast<int>(T::None));
    QCOMPARE(static_cast<int>(decideFallback(400, false, false)), static_cast<int>(T::None));
    QCOMPARE(static_cast<int>(decideFallback(0, false, true)), static_cast<int>(T::None));
}

QTEST_APPLESS_MAIN(TestSyncV2)
#include "test_sync_v2.moc"
