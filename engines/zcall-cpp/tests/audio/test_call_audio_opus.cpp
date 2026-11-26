// SPDX-License-Identifier: BSD-3-Clause
#include "dsp.h"
#include "log.h"
#include "opus_api.h"
#include "wire.h"

#include <opus.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

using namespace zcall::audio;

namespace {

std::vector<int16_t> toneFrame(int index, double hz, double amp) {
    std::vector<int16_t> v(kFrame);
    for (int i = 0; i < kFrame; ++i)
        v[size_t(i)] = static_cast<int16_t>(amp * std::sin(2 * M_PI * hz * (index * kFrame + i) / 16000.0));
    return v;
}

}

class TestCallAudioOpus : public QObject {
    Q_OBJECT

private:
    ::OpusEncoder* m_enc{nullptr};

    std::vector<uint8_t> encode(const std::vector<int16_t>& pcm) {
        unsigned char buf[1500];
        const int n = opus_encode(m_enc, pcm.data(), kFrame, buf, sizeof buf);
        return n > 0 ? std::vector<uint8_t>(buf, buf + n) : std::vector<uint8_t>{};
    }

private slots:
    void initTestCase() {
        int err = 0;
        m_enc = opus_encoder_create(16000, 1, OPUS_APPLICATION_VOIP, &err);
        QCOMPARE(err, OPUS_OK);
        opus_encoder_ctl(m_enc, OPUS_SET_BITRATE(32000));
        opus_encoder_ctl(m_enc, OPUS_SET_COMPLEXITY(10));
        opus_encoder_ctl(m_enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
        opus_encoder_ctl(m_enc, OPUS_SET_INBAND_FEC(1));
        opus_encoder_ctl(m_enc, OPUS_SET_PACKET_LOSS_PERC(5));
    }

    void cleanupTestCase() {
        if (m_enc) opus_encoder_destroy(m_enc);
        m_enc = nullptr;
    }

    void roundTripKeepsTheTone() {
        auto dec = OpusVoiceDecoder::create();
        QVERIFY(dec);
        std::vector<int16_t> out(kMaxDecodeSamples);
        double inEnergy = 0, outEnergy = 0;
        for (int f = 0; f < 50; ++f) {
            const auto pcm = toneFrame(f, 440, 8000);
            const auto pkt = encode(pcm);
            QVERIFY(!pkt.empty());
            const int n = dec->decode(pkt.data(), int(pkt.size()), out.data(), kMaxDecodeSamples, false);
            QCOMPARE(n, kFrame);
            if (f >= 10) {
                inEnergy += rms(pcm.data(), kFrame);
                outEnergy += rms(out.data(), kFrame);
            }
        }
        QVERIFY(outEnergy > inEnergy * 0.7);
        QVERIFY(outEnergy < inEnergy * 1.3);
    }

    void fecAndPlcGiveOneFrame() {
        auto dec = OpusVoiceDecoder::create();
        std::vector<int16_t> out(kMaxDecodeSamples);
        std::vector<std::vector<uint8_t>> pkts;
        for (int f = 0; f < 10; ++f) pkts.push_back(encode(toneFrame(f, 300, 6000)));
        for (int f = 0; f < 5; ++f) QCOMPARE(dec->decode(pkts[size_t(f)].data(), int(pkts[size_t(f)].size()), out.data(), kMaxDecodeSamples, false), kFrame);
        QCOMPARE(dec->decode(pkts[6].data(), int(pkts[6].size()), out.data(), kFrame, true), kFrame);
        QCOMPARE(dec->decode(nullptr, 0, out.data(), kFrame, false), kFrame);
    }

    void binaryRunsAsDropIn() {
        QTemporaryDir dir;
        const QString outFile = dir.path() + "/played.raw";
        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("ZCALL_LOG_DIR"), dir.path());
        env.insert(QStringLiteral("ZCALL_AUDIO_DUMP"), QStringLiteral("1"));
        p.setProcessEnvironment(env);
        p.start(QStringLiteral(ZCALL_AUDIO_BIN), {QStringLiteral("--out"), outFile, QStringLiteral("--tone"), QStringLiteral("--mix")});
        QVERIFY(p.waitForStarted());
        const char ctl[] = {'M', 0, 'S', 0};
        p.write(ctl, sizeof ctl);
        QElapsedTimer clock;
        clock.start();
        for (int f = 0; f < 30; ++f) {
            const auto pkt = encode(toneFrame(f, 440, 8000));
            const auto frame = encodeInFrame(uint32_t(f), 4242, pkt);
            p.write(reinterpret_cast<const char*>(frame.data()), qint64(frame.size()));
            QVERIFY(p.waitForBytesWritten(2000));
            while (clock.elapsed() < (f + 1) * 20) QTest::qWait(2);
        }
        QTest::qWait(400);
        p.closeWriteChannel();
        QVERIFY(p.waitForFinished(5000));
        QCOMPARE(p.exitStatus(), QProcess::NormalExit);
        QCOMPARE(p.exitCode(), 0);
        const QByteArray out = p.readAllStandardOutput();
        const QString err = QString::fromUtf8(p.readAllStandardError());
        QVERIFY2(err.contains(QStringLiteral("[audio-io] first microphone frame encoded")), qPrintable(err));
        QVERIFY2(err.contains(QStringLiteral("[audio-io] playout: new source 4242")), qPrintable(err));
        QVERIFY2(err.contains(QStringLiteral("[audio-io] stats {")), qPrintable(err));
        QVERIFY2(err.contains(QStringLiteral("[audio-io] audio config: aec True ns False high-pass True vad False delay 100..200 ms")), qPrintable(err));
        int frames = 0;
        int pos = 0;
        while (pos + 4 <= out.size()) {
            QCOMPARE(out[pos], 'L');
            const int level = uint8_t(out[pos + 1]);
            const int n = (uint8_t(out[pos + 2]) << 8) | uint8_t(out[pos + 3]);
            QVERIFY(level >= 10 && level <= 20);
            QVERIFY(n > 0);
            pos += 4 + n;
            ++frames;
        }
        QCOMPARE(pos, out.size());
        QVERIFY(frames >= 20);
        QVERIFY2(err.contains(QStringLiteral("-played.raw / -mic.raw (16 kHz mono s16le)")), qPrintable(err));
        const QStringList dumps = QDir(dir.path()).entryList({QStringLiteral("call-*-mic.raw"), QStringLiteral("call-*-played.raw")}, QDir::Files);
        QCOMPARE(dumps.size(), 2);
        for (const QString& d : dumps) QVERIFY(QFileInfo(dir.path() + "/" + d).size() >= 20 * kFrameBytes);
        QFile played(outFile);
        QVERIFY(played.open(QIODevice::ReadOnly));
        const QByteArray pcm = played.readAll();
        QVERIFY(pcm.size() >= 30 * kFrameBytes);
        const auto* s = reinterpret_cast<const int16_t*>(pcm.constData());
        int loud = 0;
        for (int f = 0; f + 1 <= pcm.size() / kFrameBytes; ++f) loud += rms(s + f * kFrame, kFrame) > 2000 ? 1 : 0;
        QVERIFY2(loud >= 25, qPrintable(QStringLiteral("loud frames %1 of %2\n%3").arg(loud).arg(pcm.size() / kFrameBytes).arg(err)));
    }

    void badKindEndsCleanly() {
        QTemporaryDir dir;
        QProcess p;
        p.start(QStringLiteral(ZCALL_AUDIO_BIN), {QStringLiteral("--out"), dir.path() + "/x.wav", QStringLiteral("--no-mic")});
        QVERIFY(p.waitForStarted());
        p.write("X");
        QVERIFY(p.waitForFinished(5000));
        QCOMPARE(p.exitCode(), 0);
        QVERIFY(QString::fromUtf8(p.readAllStandardError()).contains(QStringLiteral("[audio-io] bad frame kind b'X'")));
        QFile wav(dir.path() + "/x.wav");
        QVERIFY(wav.open(QIODevice::ReadOnly));
        QVERIFY(wav.read(4) == "RIFF");
    }

    void unknownOptionExitsTwo() {
        QProcess p;
        p.start(QStringLiteral(ZCALL_AUDIO_BIN), {QStringLiteral("--bogus")});
        QVERIFY(p.waitForFinished(5000));
        QCOMPARE(p.exitCode(), 2);
    }
};

QTEST_GUILESS_MAIN(TestCallAudioOpus)
#include "test_call_audio_opus.moc"
