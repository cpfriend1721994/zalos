// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstdint>
#include <utility>
#include <vector>
#include <string>

namespace zcall::audio {

constexpr int kFrame = 320;
constexpr int kFrameBytes = kFrame * 2;
constexpr int kMaxDecodeSamples = 1920;

void logLine(const std::string& text);
void fault(const std::string& kind, const std::string& why);
std::string fmt(const char* format, ...) __attribute__((format(printf, 1, 2)));
std::string pyBool(bool v);
std::string pyStr(const std::string& s);
std::string pyDict(const std::vector<std::pair<std::string, int64_t>>& items);
std::string pyIntDict(const std::vector<std::pair<int64_t, int64_t>>& items);

double monoSeconds();

}
