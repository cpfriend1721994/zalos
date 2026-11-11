// SPDX-License-Identifier: BSD-3-Clause
#include "devices.h"

#include "log.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace zcall::audio {

namespace {

std::string strip(const std::string& s) {
    const char* ws = " \t\r\n\v\f";
    const size_t a = s.find_first_not_of(ws);
    if (a == std::string::npos) return {};
    const size_t b = s.find_last_not_of(ws);
    return s.substr(a, b - a + 1);
}

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool startsWith(const std::string& s, const std::string& prefix) { return s.compare(0, prefix.size(), prefix) == 0; }

void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

std::string readFile(const std::string& path, bool* ok) {
    std::ifstream f(path);
    if (!f) {
        if (ok) *ok = false;
        return {};
    }
    std::stringstream ss;
    ss << f.rdbuf();
    if (ok) *ok = true;
    return ss.str();
}

const char* kEcConf = R"EC(context.properties = {
    log.level = 0
    default.clock.rate = 48000
    default.clock.quantum = 256
    default.clock.min-quantum = 256
    default.clock.max-quantum = 256
}
context.spa-libs = {
    audio.convert.* = audioconvert/libspa-audioconvert
    support.*       = support/libspa-support
}
context.modules = [
    { name = libpipewire-module-rt args = { nice.level = -11 rt.prio = 88 rtportal.enabled = false } flags = [ ifexists nofail ] }
    { name = libpipewire-module-protocol-native }
    { name = libpipewire-module-client-node }
    { name = libpipewire-module-adapter }
    { name = libpipewire-module-echo-cancel
      args = {
        library.name = aec/libspa-aec-webrtc
        aec.args = {
            webrtc.gain_control = false
            webrtc.noise_suppression = @NS@
            webrtc.high_pass_filter = @HPF@
            webrtc.voice_detection = @VAD@
            webrtc.transient_suppression = false
            webrtc.delay_agnostic = false
            webrtc.extended_filter = true
        }
        audio.rate = 16000
        audio.channels = 1
        audio.position = [ MONO ]
        capture.props  = { node.name = "@TAG@.capture" node.passive = true node.force-quantum = 256 @MIC@ }
        source.props   = { node.name = "@TAG@_source" node.description = "Zalo call microphone (processed)" node.force-quantum = 256 priority.session = 0 priority.driver = 0 }
        sink.props     = { node.name = "@TAG@_sink" node.description = "Zalo call speaker (processed)" node.force-quantum = 256 priority.session = 0 priority.driver = 0 }
        playback.props = { node.name = "@TAG@.playback" node.passive = true node.force-quantum = 256 @SPEAKER@ }
      }
    }
]
)EC";

}

bool pulseRunning() {
    const RunResult r = run({"pactl", "info"}, 3000);
    return r.ok() && r.exitCode == 0;
}

std::vector<std::string> parsePulseNames(const std::string& out) {
    std::vector<std::string> names;
    for (const auto& line : splitLines(out)) {
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        const size_t end = line.find('\t', tab + 1);
        names.push_back(line.substr(tab + 1, end == std::string::npos ? std::string::npos : end - tab - 1));
    }
    return names;
}

std::vector<std::string> pulseNames(const std::string& kind) {
    const RunResult r = run({"pactl", "list", "short", kind}, 3000);
    if (!r.ok()) return {};
    return parsePulseNames(r.out);
}

MaybeName present(const MaybeName& name, const std::string& kind) {
    if (!name || name->empty()) return std::nullopt;
    for (const auto& n : pulseNames(kind))
        if (n == *name) return name;
    logLine(*name + " not found; using the default");
    return std::nullopt;
}

MaybeName pulseMic() {
    if (const char* env = std::getenv("ZCALL_MIC"); env && *env) return std::string(env);
    const RunResult d = run({"pactl", "get-default-source"}, 3000);
    if (!d.started || d.timedOut) return std::nullopt;
    const std::string def = strip(d.out);
    if (!def.empty() && def.find("aloop") == std::string::npos && !endsWith(def, ".monitor")) return std::nullopt;
    const RunResult names = run({"pactl", "list", "short", "sources"}, 3000);
    if (!names.started || names.timedOut) return std::nullopt;
    for (const auto& name : parsePulseNames(names.out)) {
        if (startsWith(name, "alsa_input.") && name.find("aloop") == std::string::npos) {
            logLine("default source " + def + " is a loopback; using " + name);
            return name;
        }
    }
    return std::nullopt;
}

MaybeName defaultSource() {
    const RunResult r = run({"pactl", "get-default-source"}, 3000);
    if (!r.ok()) return std::nullopt;
    const std::string s = strip(r.out);
    if (s.empty()) return std::nullopt;
    return s;
}

std::vector<std::string> micCommand(const MaybeName& device, const MaybeName& source) {
    if (!device && pulseRunning()) {
        MaybeName src = present(source, "sources");
        if (!src) src = pulseMic();
        std::vector<std::string> cmd{"parec"};
        if (src) {
            cmd.push_back("-d");
            cmd.push_back(*src);
        }
        for (const char* a : {"--raw", "--rate=16000", "--channels=1", "--format=s16le", "--latency-msec=20"}) cmd.emplace_back(a);
        return cmd;
    }
    return {"arecord", "-q", "-D", device ? *device : std::string("default"), "-t", "raw", "-f", "S16_LE", "-r", "16000", "-c", "1"};
}

Mic::Mic(MaybeName device, MaybeName source) : m_device(std::move(device)) { switchTo(source); }

Mic::~Mic() { close(); }

std::shared_ptr<Process> Mic::current() {
    std::lock_guard<std::mutex> lock(m_lock);
    return m_proc;
}

void Mic::switchTo(const MaybeName& source) {
    const auto cmd = micCommand(m_device, source);
    std::string joined;
    for (const auto& a : cmd) joined += (joined.empty() ? "" : " ") + a;
    logLine("mic: " + joined);
    Process::Options o;
    o.out = Process::Io::Pipe;
    std::string error;
    std::shared_ptr<Process> fresh(Process::spawn(cmd, o, &error).release());
    if (!fresh) fault("mic", error);
    std::shared_ptr<Process> old;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_closed) return;
        old = m_proc;
        m_proc = fresh;
    }
    if (old) {
        old->kill();
        old->wait(-1);
    }
}

void Mic::close() {
    std::shared_ptr<Process> proc;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        m_closed = true;
        proc = std::move(m_proc);
    }
    if (proc && proc->running()) proc->stop(2000);
}

bool Mic::nextFrame(int16_t* out) {
    for (;;) {
        std::shared_ptr<Process> proc = current();
        if (!proc) return false;
        const bool ok = readExact(proc->stdoutFd(), out, size_t(kFrameBytes));
        const double now = monoSeconds();
        if (m_lastFrame >= 0 && ok) {
            const double gap = now - m_lastFrame;
            if (gap > gapMax.load()) gapMax.store(gap);
            if (gap > 0.06) gaps.fetch_add(1);
        }
        m_lastFrame = now;
        if (!ok) {
            if (proc != current()) continue;
            logLine("microphone stream ended");
            fault("mic", "capture ended");
            return false;
        }
        return true;
    }
}

std::string ecConfig(const std::string& tag, const MaybeName& mic, const MaybeName& speaker, bool highPass,
                     bool noiseSuppression, bool voiceDetection) {
    auto target = [](const MaybeName& n) {
        return n && !n->empty() ? "target.object = \"" + *n + "\" node.dont-reconnect = true" : std::string();
    };
    std::string conf = kEcConf;
    replaceAll(conf, "@TAG@", tag);
    replaceAll(conf, "@MIC@", target(mic));
    replaceAll(conf, "@SPEAKER@", target(speaker));
    replaceAll(conf, "@HPF@", highPass ? "true" : "false");
    replaceAll(conf, "@NS@", noiseSuppression ? "true" : "false");
    replaceAll(conf, "@VAD@", voiceDetection ? "true" : "false");
    return conf;
}

Processing::Processing(bool highPass, bool noiseSuppression, bool voiceDetection)
    : m_tag("zcall_ec_" + std::to_string(::getpid())), m_highPass(highPass), m_noiseSuppression(noiseSuppression),
      m_voiceDetection(voiceDetection) {
    m_source = m_tag + "_source";
    m_sink = m_tag + "_sink";
}

Processing::~Processing() { stop(); }

bool Processing::start(const MaybeName& mic, const MaybeName& speaker) {
    stop();
    const std::string conf = ecConfig(m_tag, mic, speaker, m_highPass, m_noiseSuppression, m_voiceDetection);
    const char* tmp = std::getenv("TMPDIR");
    std::string path = std::string(tmp && *tmp ? tmp : "/tmp") + "/zcall-ec-XXXXXX.conf";
    std::vector<char> buf(path.begin(), path.end());
    buf.push_back('\0');
    const int fd = ::mkstemps(buf.data(), 5);
    if (fd < 0) {
        logLine(std::string("audio processing unavailable: ") + std::strerror(errno));
        return false;
    }
    m_conf = buf.data();
    writeAll(fd, conf.data(), conf.size());
    ::close(fd);
    Process::Options o;
    o.out = Process::Io::Null;
    o.err = Process::Io::Null;
    o.dieWithParent = true;
    std::string error;
    m_proc = Process::spawn({"pipewire", "-c", m_conf}, o, &error);
    if (!m_proc) {
        logLine("audio processing unavailable: " + error);
        stop();
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!m_proc->running()) break;
        bool haveSource = false, haveSink = false;
        for (const auto& n : pulseNames("sources")) haveSource = haveSource || n == m_source;
        if (haveSource)
            for (const auto& n : pulseNames("sinks")) haveSink = haveSink || n == m_sink;
        if (haveSource && haveSink) {
            logLine("audio processing on (mic " + (mic ? *mic : std::string("default")) + ", speaker "
                    + (speaker ? *speaker : std::string("default")) + ", quantum 256)");
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    logLine("audio processing did not start; using the devices directly");
    stop();
    return false;
}

void Processing::stop() {
    if (m_proc) {
        m_proc->terminate();
        if (!m_proc->wait(2000)) {
            m_proc->kill();
            m_proc->wait(-1);
        }
        m_proc.reset();
    }
    if (!m_conf.empty()) {
        ::unlink(m_conf.c_str());
        m_conf.clear();
    }
}

std::optional<int> parseSchedPolicy(const std::string& stat) {
    const size_t paren = stat.rfind(')');
    if (paren == std::string::npos) return std::nullopt;
    std::istringstream in(stat.substr(paren + 1));
    std::string field;
    for (int i = 0; i <= 38; ++i) {
        if (!(in >> field)) return std::nullopt;
    }
    char* end = nullptr;
    const long v = std::strtol(field.c_str(), &end, 10);
    if (!end || *end) return std::nullopt;
    return static_cast<int>(v);
}

std::optional<int> schedPolicy(int pid, int tid) {
    bool ok = false;
    const std::string s = readFile("/proc/" + std::to_string(pid) + "/task/" + std::to_string(tid) + "/stat", &ok);
    if (!ok) return std::nullopt;
    return parseSchedPolicy(s);
}

std::vector<LoopThread> pipewireLoops() {
    std::vector<LoopThread> out;
    const uid_t uid = ::getuid();
    DIR* proc = ::opendir("/proc");
    if (!proc) return out;
    while (dirent* e = ::readdir(proc)) {
        const std::string d = e->d_name;
        if (d.empty() || d.find_first_not_of("0123456789") != std::string::npos) continue;
        struct stat st {};
        if (::stat(("/proc/" + d).c_str(), &st) != 0 || st.st_uid != uid) continue;
        bool ok = false;
        const std::string comm = strip(readFile("/proc/" + d + "/comm", &ok));
        if (!ok || (comm != "pipewire" && comm != "pipewire-pulse")) continue;
        DIR* tasks = ::opendir(("/proc/" + d + "/task").c_str());
        if (!tasks) continue;
        while (dirent* t = ::readdir(tasks)) {
            const std::string tid = t->d_name;
            if (tid.empty() || tid[0] == '.') continue;
            bool tok = false;
            const std::string tcomm = readFile("/proc/" + d + "/task/" + tid + "/comm", &tok);
            if (tok && startsWith(tcomm, "data-loop")) out.push_back({comm, std::atoi(d.c_str()), std::atoi(tid.c_str())});
        }
        ::closedir(tasks);
    }
    ::closedir(proc);
    return out;
}

namespace {
std::string schedName(const std::optional<int>& p) {
    if (!p) return "?";
    switch (*p) {
    case 0: return "TS";
    case 1: return "FIFO";
    case 2: return "RR";
    case 3: return "BATCH";
    case 5: return "IDLE";
    case 6: return "DL";
    default: return "?";
    }
}
}

std::string rtSummary() {
    std::string out;
    for (const auto& l : pipewireLoops()) {
        if (!out.empty()) out += ", ";
        out += l.comm + "/" + std::to_string(l.pid) + " " + schedName(schedPolicy(l.pid, l.tid));
    }
    return out.empty() ? "-" : out;
}

void pipewireRealtime() {
    std::vector<LoopThread> low;
    for (const auto& l : pipewireLoops())
        if (schedPolicy(l.pid, l.tid) == 0) low.push_back(l);
    if (low.empty()) {
        logLine("realtime: " + rtSummary());
        return;
    }
    const char* env = std::getenv("ZCALL_PW_RT");
    if ((env && std::string(env) == "0") || !which("busctl")) {
        logLine("realtime: PipeWire audio at normal priority (" + rtSummary() + "); expect crackle when the CPU is busy");
        return;
    }
    const std::vector<std::string> rtkit{"org.freedesktop.RealtimeKit1", "/org/freedesktop/RealtimeKit1",
                                         "org.freedesktop.RealtimeKit1"};
    int prio = 20;
    {
        std::vector<std::string> cmd{"busctl", "--system", "get-property"};
        cmd.insert(cmd.end(), rtkit.begin(), rtkit.end());
        cmd.emplace_back("MaxRealtimePriority");
        const RunResult r = run(cmd, 3000);
        std::istringstream in(r.out);
        std::string type, value;
        if (r.ok() && (in >> type >> value)) {
            char* end = nullptr;
            const long v = std::strtol(value.c_str(), &end, 10);
            if (end && !*end) prio = static_cast<int>(v);
        }
    }
    for (const auto& l : low) {
        std::vector<std::string> cmd{"busctl", "--system", "call"};
        cmd.insert(cmd.end(), rtkit.begin(), rtkit.end());
        for (const auto& a : {std::string("MakeThreadRealtimeWithPID"), std::string("ttu"), std::to_string(l.pid),
                              std::to_string(l.tid), std::to_string(prio)})
            cmd.push_back(a);
        const RunResult r = run(cmd, 3000);
        if (!r.started) {
            logLine("realtime: RTKit unavailable: " + r.error);
            break;
        }
        if (r.timedOut) {
            logLine("realtime: RTKit unavailable: timed out");
            break;
        }
        if (r.exitCode) logLine("realtime: RTKit refused " + l.comm + "/" + std::to_string(l.pid) + " thread "
                                + std::to_string(l.tid) + ": " + strip(r.err));
    }
    logLine("realtime: PipeWire audio was at normal priority, asked RTKit for RR " + std::to_string(prio) + ": now "
            + rtSummary());
}

}
