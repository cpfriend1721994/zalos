// SPDX-License-Identifier: BSD-3-Clause
#include "fake_decoder.h"
#include "jitter.h"
#include "playout.h"

#include <QtTest>

using namespace zcall::audio;
using namespace zcall::audio::test;

namespace {

struct Harness {
    Stats stats;
    FakeDecoder* dec;
    Stream s;
    explicit Harness(int minTarget = 5, int maxTarget = 10)
        : dec(new FakeDecoder), s(std::unique_ptr<Decoder>(dec), maxTarget, stats, minTarget) {}
    void push(int64_t idx, double at, int amp = 1, int frames = 1, int flag = 0) {
        const auto p = packet(int(idx), frames, amp, flag);
        s.push(idx, p.data(), p.size(), at);
    }
    bool pull(int16_t* out) { return s.pull(out); }
};

std::vector<int16_t> expectedFrame(int tag, int amp, int offset = 0) {
    std::vector<int16_t> v(kFrame);
    for (int i = 0; i < kFrame; ++i) v[size_t(i)] = int16_t(((tag * 131 + (i + offset) * 17) % 201 - 100) * amp);
    return v;
}

bool same(const int16_t* a, const std::vector<int16_t>& b) { return std::equal(b.begin(), b.end(), a); }

class NullSink : public Sink {
public:
    bool write(const int16_t*, int, std::string*) override { return true; }
    void close() override {}
};

}

class TestCallAudioJitter : public QObject {
    Q_OBJECT

private slots:
    void waitsForTargetThenPlaysInOrder() {
        Harness h;
        int16_t out[kFrame];
        for (int i = 0; i < 4; ++i) h.push(i, i * 0.02);
        QVERIFY(!h.pull(out));
        h.push(4, 0.08);
        for (int i = 0; i < 5; ++i) {
            QVERIFY(h.pull(out));
            QVERIFY(same(out, expectedFrame(i, 1)));
        }
        QCOMPARE(h.stats.get(Stat::OpusOk), int64_t(5));
    }

    void reorderedPacketsPlayInSequence() {
        Harness h;
        int16_t out[kFrame];
        for (int i : {1, 0, 3, 2, 4, 6, 5}) h.push(i, 0.1);
        for (int i = 0; i < 7; ++i) {
            QVERIFY(h.pull(out));
            QVERIFY(same(out, expectedFrame(i, 1)));
        }
    }

    void latePacketsAndDuplicatesAreDropped() {
        Harness h;
        int16_t out[kFrame];
        for (int i = 0; i < 6; ++i) h.push(i, 0.1);
        h.push(5, 0.11);
        QCOMPARE(h.stats.get(Stat::Dup), int64_t(1));
        QVERIFY(h.pull(out));
        QVERIFY(h.pull(out));
        h.push(0, 0.2);
        QCOMPARE(h.stats.get(Stat::Late), int64_t(1));
    }

    void lossUsesFecFromNextPacket() {
        Harness h;
        int16_t out[kFrame];
        for (int i : {0, 2, 3, 4, 5, 6}) h.push(i, 0.1, 3);
        QVERIFY(h.pull(out));
        QVERIFY(h.pull(out));
        QCOMPARE(h.stats.get(Stat::Lost), int64_t(1));
        QCOMPARE(h.stats.get(Stat::Fec), int64_t(1));
        QCOMPARE(h.dec->fecs, 1);
        QVERIFY(h.pull(out));
        QVERIFY(same(out, expectedFrame(2, 3)));
    }

    void lossWithoutNextPacketIsConcealed() {
        Harness h;
        int16_t out[kFrame];
        for (int i : {0, 1, 2, 3, 5, 7, 8}) h.push(i, 0.1 + i * 0.02);
        for (int i = 0; i < 4; ++i) QVERIFY(h.pull(out));
        for (int i = 0; i < Stream::kMaxExpand; ++i) QVERIFY(h.pull(out));
        QCOMPARE(h.stats.get(Stat::Underrun), int64_t(Stream::kMaxExpand));
        QCOMPARE(h.stats.get(Stat::Plc), int64_t(Stream::kMaxExpand));
        QCOMPARE(h.stats.get(Stat::Lost), int64_t(0));
        QVERIFY(h.pull(out));
        QCOMPARE(h.stats.get(Stat::Lost), int64_t(1));
        QCOMPARE(h.stats.get(Stat::Fec), int64_t(1));
        QVERIFY(h.pull(out));
        QVERIFY(same(out, expectedFrame(5, 1)));
    }

    void underrunConcealsThenGoesQuiet() {
        Harness h;
        int16_t out[kFrame];
        for (int i = 0; i < 5; ++i) h.push(i, 0.1);
        for (int i = 0; i < 5; ++i) QVERIFY(h.pull(out));
        for (int i = 0; i < Stream::kMaxExpand; ++i) QVERIFY(h.pull(out));
        QCOMPARE(h.stats.get(Stat::Plc), int64_t(Stream::kMaxExpand));
        QVERIFY(!h.pull(out));
        QVERIFY(!h.s.playing());
        QCOMPARE(h.stats.get(Stat::Underrun), int64_t(Stream::kMaxExpand + 1));
    }

    void farOverTargetResetsBuffer() {
        Harness h;
        int16_t out[kFrame];
        for (int i = 0; i < 30; ++i) h.push(i, 0.1 + i * 0.02);
        QVERIFY(h.pull(out));
        QCOMPARE(h.stats.get(Stat::Reset), int64_t(1));
        QVERIFY(int(h.s.buffered()) <= h.s.target());
    }

    void quietOverTargetAccelerates() {
        Harness h;
        int16_t out[kFrame];
        for (int i = 0; i < 13; ++i) h.push(i, 0.1 + i * 0.02, 1);
        for (int i = 0; i < 10; ++i) QVERIFY(h.pull(out));
        QCOMPARE(h.stats.get(Stat::Accelerate), int64_t(0));
        for (int i = 13; i < 22; ++i) h.push(i, 0.1 + i * 0.02, 1);
        QCOMPARE(h.s.target(), 5);
        QVERIFY(h.pull(out));
        QCOMPARE(h.stats.get(Stat::Accelerate), int64_t(1));
    }

    void loudSpeechIsNotAccelerated() {
        Harness h;
        int16_t out[kFrame];
        for (int i = 0; i < 10; ++i) h.push(i, 0.1 + i * 0.02, 40);
        for (int i = 0; i < 9; ++i) QVERIFY(h.pull(out));
        for (int i = 10; i < 18; ++i) h.push(i, 0.1 + i * 0.02, 40);
        QVERIFY(h.pull(out));
        QVERIFY(same(out, expectedFrame(9, 40)));
        QCOMPARE(h.stats.get(Stat::Accelerate), int64_t(0));
    }

    void targetFollowsJitterWithinBounds() {
        Harness calm(5, 10);
        for (int i = 0; i < 100; ++i) calm.push(i, i * 0.02 + 0.05);
        QCOMPARE(calm.s.target(), 5);
        Harness rough(5, 10);
        for (int i = 0; i < 100; ++i) rough.push(i, i * 0.02 + ((i * 37) % 7) * 0.03);
        QVERIFY(rough.s.target() > 5);
        QVERIFY(rough.s.target() <= 10);
        Harness huge(3, 6);
        for (int i = 0; i < 100; ++i) huge.push(i, i * 0.02 + (i % 2) * 1.0);
        QCOMPARE(huge.s.target(), 6);
        QCOMPARE(huge.s.minTarget(), 3);
    }

    void longPacketsCarryOver() {
        Harness h(2, 2);
        int16_t out[kFrame];
        h.push(0, 0.1, 2, 2);
        for (int i = 1; i < 5; ++i) h.push(i, 0.1 + i * 0.02, 2, 1);
        QVERIFY(h.pull(out));
        QVERIFY(same(out, expectedFrame(0, 2)));
        QVERIFY(h.pull(out));
        QVERIFY(same(out, expectedFrame(0, 2, kFrame)));
        QVERIFY(h.pull(out));
        QVERIFY(same(out, expectedFrame(1, 2)));
    }

    void decoderFailureIsSilenceAndCounted() {
        Harness h(2, 2);
        int16_t out[kFrame];
        h.push(0, 0.1, 5, 1, 255);
        for (int i = 1; i < 5; ++i) h.push(i, 0.1 + i * 0.02, 5);
        QVERIFY(h.pull(out));
        QCOMPARE(h.stats.get(Stat::OpusFail), int64_t(1));
        QVERIFY(std::all_of(out, out + kFrame, [](int16_t v) { return v == 0; }));
    }

    void playoutMixesSourcesWithLimiter() {
        NullSink sink;
        Playout p(&sink, [] { return std::unique_ptr<Decoder>(new FakeDecoder); }, 10, 40, 0);
        QCOMPARE(p.minTarget(), 2);
        QCOMPARE(p.maxTarget(), 10);
        const double now = monoSeconds();
        for (int i = 0; i < 5; ++i) {
            for (int64_t src : {111, 222, 333}) {
                const auto pkt = packet(i, 1, 160);
                p.push(i, pkt.data(), pkt.size(), src, now);
            }
        }
        int16_t out[kFrame];
        p.mixOnce(now, out);
        int peak = 0;
        for (int16_t v : out) peak = std::max(peak, std::abs(int(v)));
        QVERIFY(peak <= Limiter::kLimit);
        QVERIFY(peak > 20000);
        QCOMPARE(p.sourceCount(), size_t(3));
        QCOMPARE(p.stats().get(Stat::Mixed), int64_t(1));
    }

    void playoutForgetsIdleSources() {
        NullSink sink;
        Playout p(&sink, [] { return std::unique_ptr<Decoder>(new FakeDecoder); }, 10, 40, 0);
        const double now = monoSeconds();
        for (int i = 0; i < 5; ++i) {
            const auto pkt = packet(i);
            p.push(i, pkt.data(), pkt.size(), 5, now + i * 0.02);
        }
        int16_t out[kFrame];
        for (int i = 0; i < 11; ++i) p.mixOnce(now + i * 0.02, out);
        QCOMPARE(p.sourceCount(), size_t(1));
        p.mixOnce(now + 1.0, out);
        QCOMPARE(p.sourceCount(), size_t(1));
        p.mixOnce(now + 10, out);
        QCOMPARE(p.sourceCount(), size_t(0));
        QVERIFY(std::all_of(out, out + kFrame, [](int16_t v) { return v == 0; }));
    }

    void maxDelayOverridesDepth() {
        NullSink sink;
        Playout p(&sink, nullptr, 10, 100, 210);
        QCOMPARE(p.minTarget(), 5);
        QCOMPARE(p.maxTarget(), 11);
        Playout q(&sink, nullptr, 3, 100, 0);
        QCOMPARE(q.maxTarget(), 5);
    }
};

QTEST_GUILESS_MAIN(TestCallAudioJitter)
#include "test_call_audio_jitter.moc"
