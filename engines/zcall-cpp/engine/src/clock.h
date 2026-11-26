// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <chrono>
#include <functional>

namespace zcall {

struct EngineClock {
    std::function<double()> monotonic;
    std::function<double()> wall;
};

inline double monotonicMsNow() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline double wallMsNow() {
    return std::chrono::duration<double, std::milli>(std::chrono::system_clock::now().time_since_epoch()).count();
}

inline EngineClock systemClock() { return EngineClock{monotonicMsNow, wallMsNow}; }

}
