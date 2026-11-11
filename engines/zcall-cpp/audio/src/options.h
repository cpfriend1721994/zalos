// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace zcall::audio {

struct Options {
    int bitrate{32000};
    int complexity{10};
    int loss{5};
    bool noFec{false};
    bool noHighPass{false};
    bool noNs{false};
    bool noAec{false};
    bool vad{false};
    int jitter{10};
    int minDelay{100};
    int maxDelay{0};
    std::optional<std::string> device;
    std::optional<std::string> out;
    bool tone{false};
    bool mix{false};
    bool noMic{false};
    std::optional<std::string> mic;
    std::optional<std::string> speaker;
};

int parseOptions(int argc, char** argv, Options& out);
int parseOptions(const std::vector<std::string>& args, Options& out, std::string* error);

}
