// SPDX-License-Identifier: BSD-3-Clause
#include "clip_guard.h"
#include "devices.h"
#include "diag.h"
#include "dsp.h"
#include "dump.h"
#include "log.h"
#include "opus_api.h"

#include <opus.h>
#include "options.h"
#include "playout.h"
#include "pulse_api.h"
#include "serial_worker.h"
#include "sink.h"
#include "wire.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <thread>
#include <unistd.h>

using namespace zcall::audio;

namespace {

constexpr int kVolumeKey = 1;
constexpr std::chrono::milliseconds kMicRetry{200};
constexpr int kMicRecoverEvery = 5;
constexpr double kMixJoinSec = 1.0;

bool envIs(const char* name, const char* value) {
    const char* v = std::getenv(name);
    return v && std::strcmp(v, value) == 0;
}

bool envNot(const char* name, const char* value) { return !envIs(name, value); }

void encoderLoop(const Options& args, std::atomic<bool>* muted,
                 std::atomic<bool>* stopFlag, Mic* mic, std::shared_ptr<ClipGuard> guard,
                 std::function<bool()> farActive) {
    int err = 0;
    ::OpusEncoder* enc = opus_encoder_create(16000, 1, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK || !enc) {
        logLine(fmt("opus_encoder_create failed %d", err));
        return;
    }
    struct EncoderGuard {
        ::OpusEncoder* enc{nullptr};
        ~EncoderGuard() {
            if (enc) opus_encoder_destroy(enc);
        }
    } encGuard{enc};
    opus_encoder_ctl(enc, OPUS_SET_BITRATE(args.bitrate));
    opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(args.complexity));
    opus_encoder_ctl(enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    const int fec = args.noFec ? 0 : 1;
    opus_encoder_ctl(enc, OPUS_SET_INBAND_FEC(fec));
    opus_encoder_ctl(enc, OPUS_SET_PACKET_LOSS_PERC(args.noFec ? 0 : args.loss));
    logLine(fmt("encode %d bps complexity %d fec %d high-pass %s", args.bitrate, args.complexity, fec,
                pyBool(!args.noHighPass).c_str()));

    std::unique_ptr<MicLeveler> leveler;
    if (!args.tone && envNot("ZCALL_MIC_GAIN", "0")) {
        std::optional<std::string> state;
        if (const char* dir = std::getenv("ZCALL_LOG_DIR"); dir && *dir) state = std::string(dir) + "/mic-leveler.json";
        leveler = std::make_unique<MicLeveler>(farActive, state);
    }

    std::vector<int16_t> raw(kFrame), pcm(kFrame), silence(kFrame, 0);
    unsigned char buf[1500];
    double phase = 0;
    const double start = monoSeconds();
    long n = 0;
    long sent = 0;
    int misses = 0;
    for (;;) {
        if (stopFlag->load()) break;
        if (args.tone) {
            for (int i = 0; i < kFrame; ++i) {
                raw[size_t(i)] = static_cast<int16_t>(static_cast<int>(8000 * std::sin(phase)));
                phase += 2 * M_PI * 440 / 16000;
            }
            ++n;
            const double delay = start + n * 0.02 - monoSeconds();
            if (delay > 0) std::this_thread::sleep_for(std::chrono::duration<double>(delay));
        } else if (!mic || !mic->nextFrame(raw.data())) {
            if (!mic || stopFlag->load() || mic->closed()) break;
            if (++misses % kMicRecoverEvery == 0) mic->recover();
            std::this_thread::sleep_for(kMicRetry);
            continue;
        }
        misses = 0;
        const bool isMuted = muted->load();
        if (guard && !isMuted) guard->feed(raw.data(), raw.size());
        const int16_t* processed = raw.data();
        if (leveler) {
            leveler->process(raw.data(), pcm.data(), raw.size());
            processed = pcm.data();
        }
        const int16_t* frame = isMuted ? silence.data() : processed;
        Dump::instance().mic(frame, kFrame);
        const int32_t size = opus_encode(enc, frame, kFrame, buf, sizeof buf);
        if (size <= 0) continue;
        const int level = audioLevel(frame, kFrame);
        const auto out = encodeLevelFrame(static_cast<uint8_t>(level), buf, static_cast<uint16_t>(size));
        if (!writeAll(1, out.data(), out.size())) return;
        if (++sent == 1) logLine("first microphone frame encoded");
    }
}

void primePlayback(Sink& sink, int frames = 5) {
    std::vector<int16_t> silence(kFrame, 0);
    std::string error;
    for (int i = 0; i < frames; ++i) {
        if (!sink.write(silence.data(), kFrame, &error)) {
            logLine("playout prime failed: " + error);
            return;
        }
    }
    logLine(fmt("playout primed %d ms before the microphone", frames * 20));
}

void switchSpeaker(Sink& sink, const MaybeName& name) {
    std::string error;
    if (auto* pulse = dynamic_cast<PulseOut*>(&sink)) {
        if (!pulse->switchTo(present(name, "sinks"), &error)) logLine("player: " + error);
        return;
    }
    if (auto* pipe = dynamic_cast<PipeSink*>(&sink)) {
        if (!pipe->replace(speakerCommand(present(name, "sinks")), &error)) logLine("player: " + error);
    }
}

MaybeName orElse(const MaybeName& a, const std::function<MaybeName()>& b) { return a ? a : b(); }

MaybeName fallbackMicSource(const MaybeName& chosen) {
    if (chosen) return chosen;
    if (MaybeName pulse = pulseMic()) return pulse;
    return defaultSource();
}

}

int checkDeps() {
    std::printf("opus=%s\n", opus_get_version_string());
    std::string simpleError;
    const auto simple = PulseSimpleApi::load(&simpleError);
    std::printf("pulse-simple=%s\n", simple ? "system" : ("missing (" + simpleError + ")").c_str());
    std::printf("pulse=%s\n", PulseApi::available() ? "system" : "missing");
    return 0;
}

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);
#ifdef __linux__
    ::prctl(PR_SET_PDEATHSIG, SIGTERM);
#endif
    if (argc > 1 && std::strcmp(argv[1], "--check-deps") == 0) return checkDeps();
    Options args;
    if (const int code = parseOptions(argc, argv, args); code >= 0) return code;

    Dump::instance().init();
    std::string error;
    const bool pulse = !args.out && !args.device && pulseRunning();
    MaybeName chosenMic = pulse ? present(args.mic, "sources") : std::nullopt;
    MaybeName chosenSpeaker = pulse ? present(args.speaker, "sinks") : std::nullopt;
    const bool wantProc = pulse && envNot("ZCALL_AUDIO_PROCESSING", "0") && !args.noAec;
    const bool nsOn = !args.noNs && envIs("ZCALL_NOISE_SUPPRESSION", "1");
    std::unique_ptr<Processing> proc;
    if (wantProc) proc = std::make_unique<Processing>(!args.noHighPass, nsOn, args.vad);
    logLine("audio config: aec " + pyBool(!args.noAec) + " ns " + pyBool(nsOn) + " high-pass " + pyBool(!args.noHighPass)
            + " vad " + pyBool(args.vad) + fmt(" delay %d..%d ms", args.minDelay, args.maxDelay ? args.maxDelay : args.jitter * 20));

    auto startProcessing = [&]() -> bool {
        return proc ? proc->start(orElse(chosenMic, pulseMic), chosenSpeaker) : false;
    };
    SerialWorker worker;
    bool processed = startProcessing();
    if (pulse) worker.post(pipewireRealtime);
    const MaybeName speakerName = processed ? MaybeName(proc->sink()) : chosenSpeaker;

    std::unique_ptr<Sink> sink;
    if (pulse) {
        sink = PulseOut::open(speakerName, &error);
        if (!sink) logLine("libpulse-simple unavailable, using pacat: " + error);
    }
    if (!sink) {
        if (args.out) sink = FileSink::open(*args.out, &error);
        else if (pulse) sink = PipeSink::start(speakerCommand(speakerName), &error);
        else sink = PipeSink::start(fallbackPlayerCommand(args.device, false), &error);
        if (!sink) {
            logLine("player unavailable: " + error);
            return 1;
        }
    }
    if (processed && dynamic_cast<PulseOut*>(sink.get())) primePlayback(*sink);

    Playout playout(sink.get(), []() -> std::unique_ptr<Decoder> { return OpusVoiceDecoder::create(); }, args.jitter,
                    args.minDelay, args.maxDelay);
    playout.start();
    std::atomic<bool> muted{false};
    std::atomic<bool> stopEnc{false};
    std::unique_ptr<Mic> mic;
    std::thread encThread;
    if (!args.noMic) {
        if (!args.tone) mic = std::make_unique<Mic>(args.device, processed ? MaybeName(proc->source()) : chosenMic);
        if (pulse && mic) {
            mic->guard = std::make_shared<ClipGuard>(fallbackMicSource(chosenMic), pactlVolumeControl());
        }
        encThread = std::thread(encoderLoop, std::cref(args), &muted, &stopEnc, mic.get(),
                                mic ? mic->guard : nullptr,
                                std::function<bool()>([&playout] { return playout.farActive(); }));
    }
    std::unique_ptr<Diag> diag;
    if (pulse && envNot("ZCALL_AUDIO_DIAG", "0")) {
        diag = std::make_unique<Diag>(playout, mic.get(), proc ? proc->tag() : std::string("zcall_ec_"));
    }

    InParser parser;
    InMessage msg;
    uint8_t buf[8192];
    bool done = false;
    while (!done) {
        const ssize_t r = ::read(0, buf, sizeof buf);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        parser.feed(buf, static_cast<size_t>(r));
        while (!done) {
            const double at = monoSeconds();
            if (!parser.next(msg)) break;
            switch (msg.kind) {
            case InMessage::Kind::Frame:
                playout.push(msg.idx, msg.payload.data(), msg.payload.size(), msg.src, at);
                break;
            case InMessage::Kind::Speaker:
                sink->silent.store(msg.on);
                logLine(msg.on ? "speaker off" : "speaker on");
                break;
            case InMessage::Kind::Volume:
                if (mic && mic->guard) {
                    auto guard = mic->guard;
                    const int pct = msg.value;
                    worker.post([guard, pct] { guard->setPercent(pct); }, kVolumeKey);
                } else {
                    logLine("mic volume: no microphone level to set");
                }
                break;
            case InMessage::Kind::AutoGain:
                if (mic && mic->guard) mic->guard->setAutoGain(msg.on);
                else logLine("mic mode: manual");
                break;
            case InMessage::Kind::Mute:
                muted.store(msg.on);
                logLine(msg.on ? "mute" : "unmute");
                break;
            case InMessage::Kind::Device: {
                if (!pulse || (msg.which != 'i' && msg.which != 'o')) break;
                const char which = msg.which;
                const MaybeName name = msg.name.empty() ? std::nullopt : MaybeName(msg.name);
                worker.post([&, which, name] {
                    if (which == 'i') {
                        chosenMic = present(name, "sources");
                        if (mic && mic->guard) {
                            mic->guard->setSource(fallbackMicSource(chosenMic));
                            mic->guard->announce();
                            mic->guard->announceMode();
                        }
                    } else {
                        chosenSpeaker = present(name, "sinks");
                    }
                    if (processed) {
                        processed = startProcessing();
                        switchSpeaker(*sink, processed ? MaybeName(proc->sink()) : chosenSpeaker);
                        if (processed && dynamic_cast<PulseOut*>(sink.get())) primePlayback(*sink);
                        if (mic) mic->switchTo(processed ? MaybeName(proc->source()) : chosenMic);
                    } else if (which == 'i' && mic) {
                        mic->switchTo(chosenMic);
                    } else if (which == 'o') {
                        switchSpeaker(*sink, chosenSpeaker);
                    }
                });
                break;
            }
            case InMessage::Kind::Bad:
                logLine("bad frame kind " + pyBytesRepr(msg.badKind));
                done = true;
                break;
            }
        }
    }

    if (diag) diag->requestStop();
    worker.stop();
    stopEnc.store(true);
    if (mic) mic->close();
    if (proc) proc->stop();
    if (encThread.joinable()) encThread.join();
    if (!playout.flush(kMixJoinSec)) {
        sink->abort();
        if (!playout.flush(kMixJoinSec)) {
            std::fflush(stderr);
            ::_exit(0);
        }
    }
    sink->close();
    std::vector<std::pair<std::string, int64_t>> stats;
    const auto snap = playout.stats().snapshot();
    for (size_t i = 0; i < snap.size(); ++i)
        if (snap[i]) stats.emplace_back(Stats::name(Stat(i)), snap[i]);
    logLine("stats " + pyDict(stats));
    std::fflush(stderr);
    ::_exit(0);
}
