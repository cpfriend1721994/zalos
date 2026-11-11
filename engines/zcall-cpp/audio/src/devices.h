// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "subprocess.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace zcall::audio {

using MaybeName = std::optional<std::string>;

bool pulseRunning();
std::vector<std::string> pulseNames(const std::string& kind);
std::vector<std::string> parsePulseNames(const std::string& out);
MaybeName present(const MaybeName& name, const std::string& kind);
MaybeName pulseMic();
MaybeName defaultSource();
std::vector<std::string> micCommand(const MaybeName& device, const MaybeName& source);

class ClipGuard;

class Mic {
public:
    Mic(MaybeName device, MaybeName source);
    ~Mic();
    void switchTo(const MaybeName& source);
    void close();
    bool nextFrame(int16_t* out);

    std::atomic<double> gapMax{0.0};
    std::atomic<int> gaps{0};
    std::shared_ptr<ClipGuard> guard;

private:
    std::shared_ptr<Process> current();
    MaybeName m_device;
    std::mutex m_lock;
    std::shared_ptr<Process> m_proc;
    bool m_closed{false};
    double m_lastFrame{-1};
};

std::string ecConfig(const std::string& tag, const MaybeName& mic, const MaybeName& speaker, bool highPass,
                     bool noiseSuppression, bool voiceDetection);

class Processing {
public:
    Processing(bool highPass, bool noiseSuppression, bool voiceDetection);
    ~Processing();
    bool start(const MaybeName& mic, const MaybeName& speaker);
    void stop();
    const std::string& tag() const { return m_tag; }
    const std::string& source() const { return m_source; }
    const std::string& sink() const { return m_sink; }

private:
    std::string m_tag, m_source, m_sink;
    bool m_highPass, m_noiseSuppression, m_voiceDetection;
    std::unique_ptr<Process> m_proc;
    std::string m_conf;
};

struct LoopThread {
    std::string comm;
    int pid;
    int tid;
};

std::vector<LoopThread> pipewireLoops();
std::optional<int> schedPolicy(int pid, int tid);
std::optional<int> parseSchedPolicy(const std::string& stat);
std::string rtSummary();
void pipewireRealtime();

}
