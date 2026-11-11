// SPDX-License-Identifier: BSD-3-Clause
#include "log.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace zcall::audio {

namespace {
std::mutex& logMutex() {
    static std::mutex m;
    return m;
}
}

void logLine(const std::string& text) {
    std::lock_guard<std::mutex> lock(logMutex());
    std::fputs("[audio-io] ", stderr);
    std::fputs(text.c_str(), stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

void fault(const std::string& kind, const std::string& why) {
    logLine("fault: " + kind + " " + why);
}

std::string fmt(const char* format, ...) {
    char small[512];
    va_list ap;
    va_start(ap, format);
    va_list copy;
    va_copy(copy, ap);
    const int n = std::vsnprintf(small, sizeof small, format, ap);
    va_end(ap);
    if (n < 0) {
        va_end(copy);
        return {};
    }
    if (static_cast<size_t>(n) < sizeof small) {
        va_end(copy);
        return std::string(small, static_cast<size_t>(n));
    }
    std::string out(static_cast<size_t>(n), '\0');
    std::vsnprintf(out.data(), out.size() + 1, format, copy);
    va_end(copy);
    return out;
}

std::string pyBool(bool v) { return v ? "True" : "False"; }

std::string pyStr(const std::string& s) { return "'" + s + "'"; }

std::string pyDict(const std::vector<std::pair<std::string, int64_t>>& items) {
    std::string out = "{";
    bool first = true;
    for (const auto& [k, v] : items) {
        if (!first) out += ", ";
        first = false;
        out += pyStr(k) + ": " + std::to_string(v);
    }
    return out + "}";
}

std::string pyIntDict(const std::vector<std::pair<int64_t, int64_t>>& items) {
    std::string out = "{";
    bool first = true;
    for (const auto& [k, v] : items) {
        if (!first) out += ", ";
        first = false;
        out += std::to_string(k) + ": " + std::to_string(v);
    }
    return out + "}";
}

double monoSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

}
