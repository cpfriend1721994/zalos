// SPDX-License-Identifier: BSD-3-Clause
#include "options.h"
#include "wire.h"

#include <QtTest>

using namespace zcall::audio;

class TestCallAudioWire : public QObject {
    Q_OBJECT

private slots:
    void parsesEveryKindByteByByte() {
        std::vector<uint8_t> stream;
        const std::vector<uint8_t> payload{1, 2, 3, 4, 5};
        auto f = encodeInFrame(0x00020003u, 0xA1B2C3D4u, payload);
        stream.insert(stream.end(), f.begin(), f.end());
        for (uint8_t b : {uint8_t('M'), uint8_t(1), uint8_t('S'), uint8_t(0), uint8_t('V'), uint8_t(73)}) stream.push_back(b);
        for (uint8_t b : {uint8_t('D'), uint8_t('i'), uint8_t(0), uint8_t(3), uint8_t('a'), uint8_t('b'), uint8_t('c')}) stream.push_back(b);
        for (uint8_t b : {uint8_t('D'), uint8_t('o'), uint8_t(0), uint8_t(0)}) stream.push_back(b);

        InParser p;
        std::vector<InMessage> got;
        InMessage m;
        for (uint8_t b : stream) {
            p.feed(&b, 1);
            while (p.next(m)) got.push_back(m);
        }
        QCOMPARE(got.size(), size_t(6));
        QCOMPARE(int(got[0].kind), int(InMessage::Kind::Frame));
        QCOMPARE(got[0].idx, 0x00020003u);
        QCOMPARE(got[0].src, 0xA1B2C3D4u);
        QVERIFY(got[0].payload == payload);
        QCOMPARE(int(got[1].kind), int(InMessage::Kind::Mute));
        QVERIFY(got[1].on);
        QCOMPARE(int(got[2].kind), int(InMessage::Kind::Speaker));
        QVERIFY(!got[2].on);
        QCOMPARE(int(got[3].kind), int(InMessage::Kind::Volume));
        QCOMPARE(int(got[3].value), 73);
        QCOMPARE(int(got[4].kind), int(InMessage::Kind::Device));
        QCOMPARE(got[4].which, 'i');
        QCOMPARE(QString::fromStdString(got[4].name), QStringLiteral("abc"));
        QCOMPARE(got[5].which, 'o');
        QVERIFY(got[5].name.empty());
        QCOMPARE(p.pending(), size_t(0));
    }

    void partialFrameWaits() {
        InParser p;
        auto f = encodeInFrame(7, 9, std::vector<uint8_t>(300, 0x55));
        p.feed(f.data(), f.size() - 1);
        InMessage m;
        QVERIFY(!p.next(m));
        p.feed(&f.back(), 1);
        QVERIFY(p.next(m));
        QCOMPARE(m.payload.size(), size_t(300));
    }

    void badKindStops() {
        InParser p;
        const uint8_t data[] = {'M', 0, 'X', 'M', 1};
        p.feed(data, sizeof data);
        InMessage m;
        QVERIFY(p.next(m));
        QCOMPARE(int(m.kind), int(InMessage::Kind::Mute));
        QVERIFY(p.next(m));
        QCOMPARE(int(m.kind), int(InMessage::Kind::Bad));
        QCOMPARE(QString::fromStdString(pyBytesRepr(m.badKind)), QStringLiteral("b'X'"));
        QVERIFY(!p.next(m));
        QCOMPARE(QString::fromStdString(pyBytesRepr(0x01)), QStringLiteral("b'\\x01'"));
    }

    void levelFrameIsBigEndian() {
        const uint8_t opus[] = {9, 8, 7};
        const auto out = encodeLevelFrame(45, opus, 3);
        const std::vector<uint8_t> want{'L', 45, 0, 3, 9, 8, 7};
        QVERIFY(out == want);
        std::vector<uint8_t> big(0x1234, 1);
        const auto out2 = encodeLevelFrame(127, big.data(), uint16_t(big.size()));
        QCOMPARE(int(out2[2]), 0x12);
        QCOMPARE(int(out2[3]), 0x34);
    }

    void optionsMatchArgparse() {
        Options o;
        std::string err;
        QCOMPARE(parseOptions({"--mic", "src.a", "--speaker=sink.b", "--mix", "--min-delay", "80", "--max-d", "160",
                               "--bitrate", "20000", "--no-fec", "--complexity", "5", "--no-aec", "--no-ns", "--vad",
                               "--no-high-pass"},
                              o, &err),
                 -1);
        QCOMPARE(QString::fromStdString(*o.mic), QStringLiteral("src.a"));
        QCOMPARE(QString::fromStdString(*o.speaker), QStringLiteral("sink.b"));
        QVERIFY(o.mix && o.noFec && o.noAec && o.noNs && o.vad && o.noHighPass);
        QCOMPARE(o.minDelay, 80);
        QCOMPARE(o.maxDelay, 160);
        QCOMPARE(o.bitrate, 20000);
        QCOMPARE(o.complexity, 5);

        Options d;
        QCOMPARE(parseOptions(std::vector<std::string>{}, d, &err), -1);
        QCOMPARE(d.bitrate, 32000);
        QCOMPARE(d.loss, 5);
        QCOMPARE(d.minDelay, 100);
        QCOMPARE(d.maxDelay, 0);
        QCOMPARE(d.jitter, 10);

        Options bad;
        QCOMPARE(parseOptions({"--mi", "x"}, bad, &err), 2);
        QVERIFY(err.find("ambiguous") != std::string::npos);
        QCOMPARE(parseOptions({"--bitrate", "fast"}, bad, &err), 2);
        QCOMPARE(parseOptions({"--nope"}, bad, &err), 2);
        QCOMPARE(parseOptions({"--min-delay"}, bad, &err), 2);
    }
};

QTEST_GUILESS_MAIN(TestCallAudioWire)
#include "test_call_audio_wire.moc"
