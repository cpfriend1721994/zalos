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
    void protoParsesVarintBytesAndFixed32();
    void protoRejectsTruncatedAndGroups();
    void zstdRawBlockFrame();
    void zstdRejectsGarbageOrSkipsWhenMissing();
    void messageVectorsDecrypt();
    void messageNegativeVectorRejected();
    void messageBadInputSizes();
    void metadataIsUnsupportedUntilSealIsPinned();
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

void TestSyncV2::metadataIsUnsupportedUntilSealIsPinned() {
    QByteArray plain;
    QCOMPARE(cryptoStatusName(openMetadata(QByteArray(200, 'e'), &plain)), QStringLiteral("unsupported"));
    QVERIFY(plain.isEmpty());
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
