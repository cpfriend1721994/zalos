// SPDX-License-Identifier: BSD-3-Clause
#include "options.h"

#include <cstdio>
#include <cstdlib>
#include <functional>

namespace zcall::audio {

namespace {

const char* kUsage =
    "usage: zalos-call-audio [-h] [--bitrate BITRATE] [--complexity COMPLEXITY] [--loss LOSS] [--no-fec]\n"
    "                        [--no-high-pass] [--no-ns] [--no-aec] [--vad] [--jitter JITTER]\n"
    "                        [--min-delay MIN_DELAY] [--max-delay MAX_DELAY] [--device DEVICE] [--out OUT]\n"
    "                        [--tone] [--mix] [--no-mic] [--mic MIC] [--speaker SPEAKER]\n";

const char* kHelp =
    "\nAudio side of the call engine: microphone -> Opus, Opus -> speaker.\n"
    "stdout 'L' u8 level, u16 len, Opus; stdin 'F' u32 idx, u32 src, u16 len, Opus | 'M' u8 | 'S' u8 | 'V' u8 |\n"
    "'D' u8 kind, u16 len, name (big endian). Stops when stdin closes.\n";

struct Spec {
    const char* name;
    bool takesValue;
    std::function<bool(Options&, const std::string&)> apply;
};

bool toInt(const std::string& s, int& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (!end || *end) return false;
    out = static_cast<int>(v);
    return true;
}

std::vector<Spec> specs() {
    auto intOpt = [](int Options::*field) {
        return [field](Options& o, const std::string& v) { return toInt(v, o.*field); };
    };
    auto strOpt = [](std::optional<std::string> Options::*field) {
        return [field](Options& o, const std::string& v) {
            o.*field = v;
            return true;
        };
    };
    auto flag = [](bool Options::*field) {
        return [field](Options& o, const std::string&) {
            o.*field = true;
            return true;
        };
    };
    return {
        {"--bitrate", true, intOpt(&Options::bitrate)},
        {"--complexity", true, intOpt(&Options::complexity)},
        {"--loss", true, intOpt(&Options::loss)},
        {"--no-fec", false, flag(&Options::noFec)},
        {"--no-high-pass", false, flag(&Options::noHighPass)},
        {"--no-ns", false, flag(&Options::noNs)},
        {"--no-aec", false, flag(&Options::noAec)},
        {"--vad", false, flag(&Options::vad)},
        {"--jitter", true, intOpt(&Options::jitter)},
        {"--min-delay", true, intOpt(&Options::minDelay)},
        {"--max-delay", true, intOpt(&Options::maxDelay)},
        {"--device", true, strOpt(&Options::device)},
        {"--out", true, strOpt(&Options::out)},
        {"--tone", false, flag(&Options::tone)},
        {"--mix", false, flag(&Options::mix)},
        {"--no-mic", false, flag(&Options::noMic)},
        {"--mic", true, strOpt(&Options::mic)},
        {"--speaker", true, strOpt(&Options::speaker)},
    };
}

}

int parseOptions(const std::vector<std::string>& args, Options& o, std::string* error) {
    if (const char* m = std::getenv("ZCALL_MIC")) o.mic = std::string(m);
    if (const char* s = std::getenv("ZCALL_SPEAKER")) o.speaker = std::string(s);
    const auto all = specs();
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "-h" || a == "--help") return 0;
        if (a.size() < 3 || a.compare(0, 2, "--") != 0) {
            if (error) *error = "unrecognized arguments: " + a;
            return 2;
        }
        const size_t eq = a.find('=');
        const std::string key = eq == std::string::npos ? a : a.substr(0, eq);
        const Spec* match = nullptr;
        std::vector<const Spec*> prefixed;
        for (const auto& s : all) {
            if (key == s.name) {
                match = &s;
                break;
            }
            if (std::string(s.name).compare(0, key.size(), key) == 0) prefixed.push_back(&s);
        }
        if (!match && prefixed.size() == 1) match = prefixed[0];
        if (!match) {
            if (error) {
                if (prefixed.size() > 1) {
                    std::string list;
                    for (const auto* p : prefixed) list += std::string(list.empty() ? "" : ", ") + p->name;
                    *error = "ambiguous option: " + key + " could match " + list;
                } else {
                    *error = "unrecognized arguments: " + a;
                }
            }
            return 2;
        }
        std::string value;
        if (match->takesValue) {
            if (eq != std::string::npos) {
                value = a.substr(eq + 1);
            } else if (i + 1 < args.size()) {
                value = args[++i];
            } else {
                if (error) *error = "argument " + std::string(match->name) + ": expected one argument";
                return 2;
            }
        } else if (eq != std::string::npos) {
            if (error) *error = "argument " + std::string(match->name) + ": ignored explicit argument '" + a.substr(eq + 1) + "'";
            return 2;
        }
        if (!match->apply(o, value)) {
            if (error) *error = "argument " + std::string(match->name) + ": invalid int value: '" + value + "'";
            return 2;
        }
    }
    return -1;
}

int parseOptions(int argc, char** argv, Options& out) {
    std::vector<std::string> args(argv + 1, argv + argc);
    std::string error;
    const int code = parseOptions(args, out, &error);
    if (code == 0) {
        std::fputs(kUsage, stdout);
        std::fputs(kHelp, stdout);
        std::fflush(stdout);
    } else if (code == 2) {
        std::fputs(kUsage, stderr);
        std::fprintf(stderr, "zalos-call-audio: error: %s\n", error.c_str());
    }
    return code;
}

}
