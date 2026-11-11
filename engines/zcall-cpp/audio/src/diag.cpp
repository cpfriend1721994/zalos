// SPDX-License-Identifier: BSD-3-Clause
#include "diag.h"

#include "clip_guard.h"
#include "devices.h"
#include "playout.h"
#include "subprocess.h"

#include <cstdlib>
#include <ctime>
#include <sstream>

namespace zcall::audio {

namespace {

std::vector<std::string> splitWs(const std::string& line) {
    std::istringstream in(line);
    std::vector<std::string> out;
    std::string w;
    while (in >> w) out.push_back(w);
    return out;
}

bool parseInt(const std::string& s, int& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (!end || *end) return false;
    out = static_cast<int>(v);
    return true;
}

bool startsWithAny(const std::string& s, std::initializer_list<std::string> prefixes) {
    for (const auto& p : prefixes)
        if (s.compare(0, p.size(), p) == 0) return true;
    return false;
}

std::string firstDotPart(const std::string& s) { return s.substr(0, s.find('.')); }

std::string lastDotPart(const std::string& s) {
    const size_t d = s.rfind('.');
    return d == std::string::npos ? s : s.substr(d + 1);
}

}

std::vector<std::pair<std::string, PwNode>> parsePwTop(const std::string& out) {
    const std::string marker = "S   ID";
    const size_t at = out.rfind(marker);
    const std::string last = at == std::string::npos ? out : out.substr(at + marker.size());
    std::vector<std::pair<std::string, PwNode>> nodes;
    std::istringstream in(last);
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (first) {
            first = false;
            continue;
        }
        const auto c = splitWs(line);
        if (c.size() < 10 || c[0] != "R") continue;
        const std::string& name = c.back();
        bool follower = false;
        for (size_t i = 9; i < c.size(); ++i) follower = follower || c[i] == "+";
        PwNode n;
        if (!parseInt(c[2], n.quantum) || !parseInt(c[3], n.rate) || !parseInt(c[8], n.err)) continue;
        n.driver = !follower;
        bool found = false;
        for (auto& [k, v] : nodes) {
            if (k == name) {
                v = n;
                found = true;
                break;
            }
        }
        if (!found) nodes.emplace_back(name, n);
    }
    return nodes;
}

int countResyncs(const std::string& journal) {
    int n = 0;
    std::istringstream in(journal);
    std::string line;
    while (std::getline(in, line)) {
        if (line.find("resync") == std::string::npos) continue;
        ++n;
        if (line.find("suppressed") != std::string::npos) {
            const size_t p = line.rfind('(');
            if (p == std::string::npos) continue;
            const auto words = splitWs(line.substr(p + 1));
            int extra = 0;
            if (!words.empty() && parseInt(words[0], extra)) n += extra;
        }
    }
    return n;
}

Diag::Diag(Playout& playout, Mic* mic, std::string tag)
    : m_playout(playout), m_mic(mic), m_tag(std::move(tag)), m_since(static_cast<double>(std::time(nullptr))) {
    m_pw = which("pw-top").has_value();
    m_journal = which("journalctl").has_value();
    m_thread = std::thread([this] { loop(); });
    m_thread.detach();
}

Diag::~Diag() = default;

std::string Diag::line(const std::vector<std::pair<std::string, PwNode>>* nodes, int resyncs, const std::string& rt) {
    const auto st = m_playout.stats().snapshot();
    std::vector<std::pair<std::string, int64_t>> d;
    for (Stat s : {Stat::OpusOk, Stat::Lost, Stat::Fec, Stat::Plc, Stat::Underrun, Stat::Accelerate, Stat::AccelerateLoud,
                   Stat::Reset, Stat::Late, Stat::Dup}) {
        const int64_t delta = st[size_t(s)] - m_prevStats[size_t(s)];
        if (delta) d.emplace_back(Stats::name(s), delta);
    }
    m_prevStats = st;
    const auto period = m_playout.takeDiagPeriod();
    const std::string dev = period.devMin ? fmt("%.0f..%.0f", *period.devMin, *period.devMax) : std::string("-");
    std::string mic = "-";
    if (m_mic) {
        mic = fmt("max gap %.0f ms, stalls %d", m_mic->gapMax.load() * 1000, m_mic->gaps.load());
        m_mic->gapMax.store(0.0);
        m_mic->gaps.store(0);
        if (m_mic->guard) mic += ", " + m_mic->guard->report() + ", gain steps " + std::to_string(m_mic->guard->steps());
    }
    std::string pw;
    if (nodes) {
        std::string drivers, xr;
        for (const auto& [n, v] : *nodes) {
            const bool ours = startsWithAny(n, {"alsa_", "bluez_", m_tag}) || n == "Zalo" || n == "parec";
            if (!ours) continue;
            if (v.driver) drivers += (drivers.empty() ? "" : ", ") + n + " q" + std::to_string(v.quantum);
        }
        for (const auto& [n, v] : *nodes) {
            if (!startsWithAny(n, {"alsa_", "bluez_"})) continue;
            auto prev = m_prevErr.find(n);
            if (prev != m_prevErr.end() && v.err > prev->second)
                xr += (xr.empty() ? "" : ", ") + firstDotPart(n) + "." + lastDotPart(n) + " +" + std::to_string(v.err - prev->second);
            m_prevErr[n] = v.err;
        }
        pw = "; pipewire driver " + (drivers.empty() ? std::string("-") : drivers) + ", xruns " + (xr.empty() ? std::string("0") : xr);
    }
    return "diag: playout " + pyDict(d) + ", buffer ms " + pyIntDict(period.buffers) + ", card queue ms " + dev
        + fmt(", mix max %.1f ms; mic ", period.mixMax * 1000) + mic + pw
        + (resyncs >= 0 ? ", resyncs " + std::to_string(resyncs) : std::string()) + "; rt " + rt;
}

void Diag::loop() {
    for (;;) {
        if (m_playout.waitStop(kDiagSec)) return;
        std::vector<std::pair<std::string, PwNode>> nodes;
        if (m_pw) {
            const RunResult r = run({"pw-top", "-b", "-n", "2"}, 5000);
            if (r.ok()) nodes = parsePwTop(r.out);
        }
        int rs = -1;
        const double now = static_cast<double>(std::time(nullptr));
        if (m_journal) {
            const RunResult r = run({"journalctl", "--user", "-u", "pipewire", "--since",
                                     "@" + std::to_string(static_cast<long long>(m_since)), "-o", "cat", "--no-pager"},
                                    5000);
            rs = r.ok() ? countResyncs(r.out) : -1;
        }
        m_since = now;
        logLine(line(m_pw ? &nodes : nullptr, rs, rtSummary()));
    }
}

}
