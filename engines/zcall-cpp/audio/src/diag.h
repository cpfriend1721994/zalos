// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "jitter.h"

#include <array>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace zcall::audio {

class Playout;
class Mic;

struct PwNode {
    int quantum{0};
    int rate{0};
    int err{0};
    bool driver{false};
};

std::vector<std::pair<std::string, PwNode>> parsePwTop(const std::string& out);
int countResyncs(const std::string& journal);

class Diag {
public:
    static constexpr int kDiagSec = 10;

    Diag(Playout& playout, Mic* mic, std::string tag);
    ~Diag();
    Diag(const Diag&) = delete;
    Diag& operator=(const Diag&) = delete;
    void requestStop();
    std::string line(const std::vector<std::pair<std::string, PwNode>>* nodes, int resyncs, const std::string& rt);

private:
    void loop();
    bool stopRequested();

    Playout& m_playout;
    Mic* m_mic;
    std::string m_tag;
    std::array<int64_t, size_t(Stat::Count)> m_prevStats{};
    std::map<std::string, int> m_prevErr;
    double m_since;
    bool m_pw;
    bool m_journal;
    std::mutex m_stopLock;
    std::condition_variable m_stopCv;
    bool m_stop{false};
    std::thread m_thread;
};

}
