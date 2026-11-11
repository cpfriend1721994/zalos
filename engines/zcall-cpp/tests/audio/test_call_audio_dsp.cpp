// SPDX-License-Identifier: BSD-3-Clause
#include "clip_guard.h"
#include "devices.h"
#include "diag.h"
#include "dsp.h"
#include "log.h"

#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

using namespace zcall::audio;

namespace {

std::vector<int16_t> sine(size_t n, double period, double amp, double phase = 0) {
    std::vector<int16_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<int16_t>(amp * std::sin(2 * M_PI * (double(i) + phase) / period));
    return v;
}

std::vector<int16_t> noise(size_t n, int amp, uint32_t& seed) {
    std::vector<int16_t> v(n);
    for (auto& s : v) {
        seed = seed * 1664525u + 1013904223u;
        s = static_cast<int16_t>(int(seed % uint32_t(2 * amp + 1)) - amp);
    }
    return v;
}

class FakeVolume : public VolumeControl {
public:
    bool available() override { return true; }
    std::optional<std::string> getVolume(const std::string&) override {
        const double db = 60.0 * std::log10(double(raw) / 65536.0);
        const int pct = int(std::nearbyint(100.0 * double(raw) / 65536.0));
        return fmt("Volume: mono: %lld / %3d%% / %.2f dB\n        balance 0.00\n", static_cast<long long>(raw), pct, db);
    }
    void setVolume(const std::string&, int64_t r) override {
        raw = r;
        ++sets;
    }
    int64_t raw{65536};
    int sets{0};
};

}

class TestCallAudioDsp : public QObject {
    Q_OBJECT

private slots:
    void audioLevelFollowsRfc6464() {
        std::vector<int16_t> silent(kFrame, 0);
        QCOMPARE(audioLevel(silent.data(), silent.size()), 127);
        std::vector<int16_t> full(kFrame, 32767);
        QCOMPARE(audioLevel(full.data(), full.size()), 0);
        const auto s = sine(kFrame, 32, 3276.8);
        QCOMPARE(audioLevel(s.data(), s.size()), 23);
    }

    void wsolaShortensByOnePitchPeriod() {
        const auto in = sine(640, 80, 6000);
        int16_t played[kFrame];
        std::vector<int16_t> rest;
        wsolaShrink(in, played, rest);
        const int lag = pitchLag(in.data(), in.size());
        QCOMPARE(lag, 80);
        QCOMPARE(int(rest.size()) + kFrame, 640 - lag);
        for (int i = 1; i < kFrame; ++i) QVERIFY(std::abs(played[i] - played[i - 1]) < 600);
        QVERIFY(std::abs(rest.front() - played[kFrame - 1]) < 600);
    }

    void wsolaKeepsShortInputs() {
        std::vector<int16_t> in(500, 7);
        int16_t played[kFrame];
        std::vector<int16_t> rest;
        wsolaShrink(in, played, rest);
        QCOMPARE(rest.size(), size_t(180));
        QCOMPARE(int(played[0]), 7);
    }

    void pitchLagFindsHarmonicForHighTones() {
        const auto in = sine(640, 16, 8000);
        const int lag = pitchLag(in.data(), in.size());
        QCOMPARE(lag % 16, 0);
    }

    void limiterHoldsMixUnderLimitAndReleases() {
        Limiter lim;
        std::vector<int32_t> acc(kFrame, 50000);
        int16_t out[kFrame];
        lim.apply(acc.data(), 2, out);
        QVERIFY(std::abs(int(out[0]) - 26000) <= 1);
        std::vector<int32_t> quiet(kFrame, 1000);
        lim.apply(quiet.data(), 2, out);
        QVERIFY(lim.gain() < 1.0);
        QVERIFY(lim.gain() > 0.52);
        for (int i = 0; i < 100; ++i) lim.apply(quiet.data(), 2, out);
        QVERIFY(lim.gain() > 0.999);
        std::vector<int32_t> hot(kFrame, 40000);
        lim.apply(hot.data(), 1, out);
        QCOMPARE(int(out[0]), 32767);
    }

    void levelerLiftsQuietSpeechSlowly() {
        MicLeveler lev(nullptr, std::nullopt);
        uint32_t seed = 1;
        std::vector<int16_t> out(kFrame);
        for (int i = 0; i < 25; ++i) {
            const auto n = noise(kFrame, 20, seed);
            lev.process(n.data(), out.data(), n.size());
        }
        QCOMPARE(lev.gainDb(), 0.0);
        double prev = 0;
        for (int i = 0; i < 20; ++i) {
            const auto s = sine(kFrame, 40, 1000, i * 320);
            lev.process(s.data(), out.data(), s.size());
            QVERIFY(lev.gainDb() - prev <= MicLeveler::kUpDb + 1e-9);
            prev = lev.gainDb();
        }
        QVERIFY(lev.gainDb() > 5.0);
        const auto loud = sine(kFrame, 40, 32000);
        lev.process(loud.data(), out.data(), loud.size());
        for (int16_t v : out) QVERIFY(std::abs(int(v)) <= 32000);
    }

    void levelerHoldsWhileFarEndTalks() {
        bool far = true;
        MicLeveler lev([&far] { return far; }, std::nullopt);
        uint32_t seed = 3;
        std::vector<int16_t> out(kFrame);
        for (int i = 0; i < 25; ++i) {
            const auto n = noise(kFrame, 20, seed);
            lev.process(n.data(), out.data(), n.size());
        }
        for (int i = 0; i < 20; ++i) {
            const auto s = sine(kFrame, 40, 1000, i * 320);
            lev.process(s.data(), out.data(), s.size());
        }
        QCOMPARE(lev.gainDb(), 0.0);
    }

    void levelerPersistsGain() {
        QTemporaryDir dir;
        const std::string path = (dir.path() + "/mic-leveler.json").toStdString();
        double gain = 0;
        {
            MicLeveler lev(nullptr, path);
            uint32_t seed = 5;
            std::vector<int16_t> out(kFrame);
            for (int i = 0; i < MicLeveler::kLogEvery; ++i) {
                const auto f = i < 25 ? noise(kFrame, 20, seed) : sine(kFrame, 40, 1000, i * 320);
                lev.process(f.data(), out.data(), f.size());
            }
            gain = lev.gainDb();
        }
        QVERIFY(gain > 0);
        MicLeveler again(nullptr, path);
        QVERIFY(std::abs(again.gainDb() - gain) < 0.006);
    }

    void pactlParsers() {
        const std::string mono = "Volume: mono: 52429 /  80% / -5.81 dB\n        balance 0.00\n";
        QCOMPARE(*parseVolumePercent(mono), 80);
        QCOMPARE(*parseVolumeDb(mono), -5.81);
        const std::string stereo = "Volume: front-left: 65536 / 100% / 0.00 dB,   front-right: 65536 / 100% / 0.00 dB\n";
        QCOMPARE(*parseVolumePercent(stereo), 100);
        QCOMPARE(*parseVolumeDb(stereo), 0.0);
        QVERIFY(std::isinf(*parseVolumeDb("Volume: mono: 0 /   0% / -inf dB\n")));
        QVERIFY(!parseVolumePercent("garbage").has_value());
        QCOMPARE(rawFromPercent(50), int64_t(32768));
        QCOMPARE(rawFromDb(-6.0), int64_t(52057));
        QCOMPARE(QString::fromStdString(pyFloat(-5.81)), QStringLiteral("-5.81"));
        QCOMPARE(QString::fromStdString(pyFloat(0.0)), QStringLiteral("0.0"));
        QCOMPARE(QString::fromStdString(pyFloat(-24.0)), QStringLiteral("-24.0"));
    }

    void clipGuardStepsDownOncePerSecond() {
        qputenv("ZCALL_MIC_AGC", "1");
        auto vol = std::make_shared<FakeVolume>();
        ClipGuard g(std::string("alsa_input.test"), vol, false);
        QVERIFY(g.on());
        std::vector<int16_t> clip(kFrame, 0);
        for (int i = 0; i < 20; ++i) clip[size_t(i) * 10] = 32767;
        for (int i = 0; i < ClipGuard::kWindow; ++i) g.feed(clip.data(), clip.size());
        QCOMPARE(g.steps(), 1);
        QCOMPARE(vol->raw, rawFromDb(-3.0));
        for (int i = 0; i < ClipGuard::kWindow; ++i) g.feed(clip.data(), clip.size());
        QCOMPARE(g.steps(), 1);
        const std::string report = g.report();
        QVERIFY(report.find("clipped 400") != std::string::npos);
        QVERIFY(report.find("peak -0.0 dBFS") != std::string::npos);
    }

    void clipGuardIgnoresFewClipsAndRespectsFloor() {
        auto vol = std::make_shared<FakeVolume>();
        vol->raw = rawFromDb(-23.995);
        ClipGuard g(std::string("alsa_input.test"), vol, false);
        std::vector<int16_t> few(kFrame, 0);
        few[0] = -32768;
        for (int i = 0; i < ClipGuard::kWindow; ++i) g.feed(few.data(), few.size());
        QCOMPARE(g.steps(), 0);
        std::vector<int16_t> many(kFrame, 32000);
        for (int i = 0; i < ClipGuard::kWindow; ++i) g.feed(many.data(), many.size());
        QCOMPARE(g.steps(), 0);
    }

    void clipGuardManualSliderWins() {
        auto vol = std::make_shared<FakeVolume>();
        ClipGuard g(std::string("alsa_input.test"), vol, false);
        QTest::qWait(650);
        g.setPercent(40);
        QVERIFY(g.manual());
        QCOMPARE(vol->raw, rawFromPercent(40));
        const int sets = vol->sets;
        g.setPercent(100);
        QCOMPARE(vol->sets, sets);
        std::vector<int16_t> many(kFrame, 32000);
        for (int i = 0; i < ClipGuard::kWindow; ++i) g.feed(many.data(), many.size());
        QCOMPARE(g.steps(), 0);
    }

    void pwTopAndJournalParsers() {
        const std::string out =
            "S   ID  QUANT   RATE    WAIT    BUSY   W/Q   B/Q  ERR FORMAT           NAME\n"
            "R   30    256  48000  10.0us   5.0us  0.00  0.00    3 S16LE 2 48000 alsa_output.usb-X.analog-stereo\n"
            "S   ID  QUANT   RATE    WAIT    BUSY   W/Q   B/Q  ERR FORMAT           NAME\n"
            "R   30    256  48000  10.0us   5.0us  0.00  0.00    7 S16LE 2 48000 alsa_output.usb-X.analog-stereo\n"
            "R   41      0      0   1.0us   2.0us  0.00  0.00    0 S16LE 1 16000  + zcall_ec_42.capture\n"
            "S   50      0      0    ---     ---   ---   ---     0                  idle-node\n";
        const auto nodes = parsePwTop(out);
        QCOMPARE(nodes.size(), size_t(2));
        QCOMPARE(QString::fromStdString(nodes[0].first), QStringLiteral("alsa_output.usb-X.analog-stereo"));
        QCOMPARE(nodes[0].second.err, 7);
        QCOMPARE(nodes[0].second.quantum, 256);
        QVERIFY(nodes[0].second.driver);
        QVERIFY(!nodes[1].second.driver);
        QCOMPARE(countResyncs("spa.alsa: resync\nother\nspa.alsa: resync (suppressed 4 times)\n"), 2);
        QCOMPARE(countResyncs("spa.alsa: resync (4 suppressed)\n"), 5);
        std::string stat = "123 (data-loop.0) S 1 2 3";
        for (int i = 0; i < 40; ++i) stat += " " + std::to_string(i == 34 ? 2 : 0);
        QCOMPARE(*parseSchedPolicy(stat), 2);
    }

    void echoCancelConfigMatchesTemplate() {
        const std::string conf = ecConfig("zcall_ec_9", std::string("alsa_input.mic"), std::nullopt, true, false, false);
        QVERIFY(conf.find("node.name = \"zcall_ec_9.capture\" node.passive = true node.force-quantum = 256 target.object = \"alsa_input.mic\" node.dont-reconnect = true }") != std::string::npos);
        QVERIFY(conf.find("node.name = \"zcall_ec_9.playback\" node.passive = true node.force-quantum = 256  }") != std::string::npos);
        QVERIFY(conf.find("webrtc.high_pass_filter = true") != std::string::npos);
        QVERIFY(conf.find("webrtc.noise_suppression = false") != std::string::npos);
        QVERIFY(conf.find("webrtc.voice_detection = false") != std::string::npos);
        QVERIFY(conf.find("webrtc.gain_control = false") != std::string::npos);
        QVERIFY(conf.find("@") == std::string::npos);
        QCOMPARE(parsePulseNames("1\talsa_input.a\tmodule\ts16le\n2\tzcall_ec_1_source\tx\n").size(), size_t(2));
    }
};

QTEST_GUILESS_MAIN(TestCallAudioDsp)
#include "test_call_audio_dsp.moc"
