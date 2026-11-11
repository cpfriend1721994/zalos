// SPDX-License-Identifier: BSD-3-Clause
#include "sink.h"

#include "log.h"

#include <chrono>
#include <cstring>
#include <dlfcn.h>
#include <thread>

namespace zcall::audio {

struct PulseOut::Lib {
    struct Spec {
        int format;
        uint32_t rate;
        uint8_t channels;
    };
    struct Attr {
        uint32_t maxlength, tlength, prebuf, minreq, fragsize;
    };
    void* handle{nullptr};
    void* (*simpleNew)(const char*, const char*, int, const char*, const char*, const Spec*, const void*, const Attr*, int*){nullptr};
    int (*simpleWrite)(void*, const void*, size_t, int*){nullptr};
    void (*simpleFree)(void*){nullptr};
    uint64_t (*simpleGetLatency)(void*, int*){nullptr};

    static std::shared_ptr<Lib> load(std::string* error) {
        void* h = ::dlopen("libpulse-simple.so.0", RTLD_NOW | RTLD_LOCAL);
        if (!h) {
            const char* e = ::dlerror();
            if (error) *error = e ? e : "libpulse-simple.so.0: cannot open shared object file";
            return nullptr;
        }
        auto lib = std::make_shared<Lib>();
        lib->handle = h;
        lib->simpleNew = reinterpret_cast<decltype(lib->simpleNew)>(::dlsym(h, "pa_simple_new"));
        lib->simpleWrite = reinterpret_cast<decltype(lib->simpleWrite)>(::dlsym(h, "pa_simple_write"));
        lib->simpleFree = reinterpret_cast<decltype(lib->simpleFree)>(::dlsym(h, "pa_simple_free"));
        lib->simpleGetLatency = reinterpret_cast<decltype(lib->simpleGetLatency)>(::dlsym(h, "pa_simple_get_latency"));
        if (!lib->simpleNew || !lib->simpleWrite || !lib->simpleFree || !lib->simpleGetLatency) {
            if (error) *error = "libpulse-simple.so.0 lacks pa_simple_*";
            return nullptr;
        }
        return lib;
    }
};

std::unique_ptr<PulseOut> PulseOut::open(const std::optional<std::string>& device, std::string* error) {
    auto lib = Lib::load(error);
    if (!lib) return nullptr;
    std::unique_ptr<PulseOut> out(new PulseOut);
    out->m_lib = std::move(lib);
    out->m_zero.assign(1920, 0);
    out->m_stream = out->openStream(device, error);
    if (!out->m_stream) return nullptr;
    return out;
}

void* PulseOut::openStream(const std::optional<std::string>& device, std::string* error) {
    const Lib::Spec spec{3, 16000, 1};
    const uint32_t none = 0xFFFFFFFFu;
    const Lib::Attr attr{none, static_cast<uint32_t>(kFrameBytes * kLatencyMs / 20), none, none, none};
    int err = 0;
    void* s = m_lib->simpleNew(nullptr, "Zalo", 1, device ? device->c_str() : nullptr, "Call", &spec, nullptr, &attr, &err);
    if (!s) {
        if (error) *error = fmt("pa_simple_new failed (%d)", err);
        return nullptr;
    }
    logLine(fmt("player: libpulse-simple %s, %d ms", device ? device->c_str() : "default", kLatencyMs));
    return s;
}

PulseOut::~PulseOut() { close(); }

bool PulseOut::switchTo(const std::optional<std::string>& device, std::string* error) {
    void* fresh = openStream(device, error);
    if (!fresh) return false;
    void* old = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        old = m_stream;
        m_stream = fresh;
    }
    if (old) m_lib->simpleFree(old);
    return true;
}

bool PulseOut::write(const int16_t* pcm, int samples, std::string*) {
    const int16_t* data = silent.load() ? m_zero.data() : pcm;
    int err = 0;
    bool ok = false;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        ok = m_stream && m_lib->simpleWrite(m_stream, data, static_cast<size_t>(samples) * 2, &err) >= 0;
    }
    if (!ok) {
        if (!m_failed) logLine(fmt("pa_simple_write failed (%d)", err));
        m_failed = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    } else {
        m_failed = false;
    }
    return true;
}

std::optional<double> PulseOut::latencyMs() {
    std::lock_guard<std::mutex> lock(m_lock);
    if (!m_stream) return std::nullopt;
    int err = 0;
    const uint64_t us = m_lib->simpleGetLatency(m_stream, &err);
    if (err) return std::nullopt;
    return static_cast<double>(us) / 1000.0;
}

void PulseOut::close() {
    std::lock_guard<std::mutex> lock(m_lock);
    if (m_stream) {
        m_lib->simpleFree(m_stream);
        m_stream = nullptr;
    }
}

std::unique_ptr<PipeSink> PipeSink::start(const std::vector<std::string>& cmd, std::string* error) {
    std::string joined;
    for (const auto& a : cmd) joined += (joined.empty() ? "" : " ") + a;
    logLine("player: " + joined);
    Process::Options o;
    o.in = Process::Io::Pipe;
    auto proc = Process::spawn(cmd, o, error);
    if (!proc) return nullptr;
    auto sink = std::unique_ptr<PipeSink>(new PipeSink);
    sink->m_proc = std::move(proc);
    sink->m_zero.assign(1920, 0);
    return sink;
}

bool PipeSink::write(const int16_t* pcm, int samples, std::string* error) {
    const int16_t* data = silent.load() ? m_zero.data() : pcm;
    std::lock_guard<std::mutex> lock(m_lock);
    if (!m_proc || m_proc->stdinFd() < 0) {
        if (error) *error = "I/O operation on closed file.";
        return false;
    }
    if (!writeAll(m_proc->stdinFd(), data, static_cast<size_t>(samples) * 2)) {
        if (error) *error = "[Errno 32] Broken pipe";
        return false;
    }
    return true;
}

bool PipeSink::replace(const std::vector<std::string>& cmd, std::string* error) {
    std::string joined;
    for (const auto& a : cmd) joined += (joined.empty() ? "" : " ") + a;
    logLine("player: " + joined);
    Process::Options o;
    o.in = Process::Io::Pipe;
    auto fresh = Process::spawn(cmd, o, error);
    if (!fresh) return false;
    std::unique_ptr<Process> old;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        old = std::move(m_proc);
        m_proc = std::move(fresh);
    }
    if (old) {
        old->closeStdin();
        old->wait(-1);
    }
    return true;
}

void PipeSink::close() {
    std::unique_ptr<Process> old;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        old = std::move(m_proc);
    }
    if (old) {
        old->closeStdin();
        old->wait(-1);
    }
}

namespace {
void putU32(unsigned char* p, uint32_t v) {
    p[0] = v & 0xff;
    p[1] = (v >> 8) & 0xff;
    p[2] = (v >> 16) & 0xff;
    p[3] = (v >> 24) & 0xff;
}

void wavHeader(unsigned char* h, uint32_t dataBytes) {
    std::memcpy(h, "RIFF", 4);
    putU32(h + 4, 36 + dataBytes);
    std::memcpy(h + 8, "WAVEfmt ", 8);
    putU32(h + 16, 16);
    h[20] = 1;
    h[21] = 0;
    h[22] = 1;
    h[23] = 0;
    putU32(h + 24, 16000);
    putU32(h + 28, 32000);
    h[32] = 2;
    h[33] = 0;
    h[34] = 16;
    h[35] = 0;
    std::memcpy(h + 36, "data", 4);
    putU32(h + 40, dataBytes);
}
}

std::unique_ptr<FileSink> FileSink::open(const std::string& path, std::string* error) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        if (error) *error = std::strerror(errno);
        return nullptr;
    }
    auto sink = std::unique_ptr<FileSink>(new FileSink);
    sink->m_file = f;
    sink->m_zero.assign(1920, 0);
    sink->m_wav = path.size() >= 4 && path.compare(path.size() - 4, 4, ".wav") == 0;
    if (sink->m_wav) {
        unsigned char h[44];
        wavHeader(h, 0);
        std::fwrite(h, 1, sizeof h, f);
    }
    return sink;
}

FileSink::~FileSink() { close(); }

bool FileSink::write(const int16_t* pcm, int samples, std::string* error) {
    if (!m_file) {
        if (error) *error = "I/O operation on closed file.";
        return false;
    }
    const int16_t* data = silent.load() ? m_zero.data() : pcm;
    const size_t n = static_cast<size_t>(samples) * 2;
    if (std::fwrite(data, 1, n, m_file) != n) {
        if (error) *error = std::strerror(errno);
        return false;
    }
    m_bytes += static_cast<uint32_t>(n);
    return true;
}

void FileSink::close() {
    if (!m_file) return;
    if (m_wav) {
        unsigned char h[44];
        wavHeader(h, m_bytes);
        std::fseek(m_file, 0, SEEK_SET);
        std::fwrite(h, 1, sizeof h, m_file);
    }
    std::fclose(m_file);
    m_file = nullptr;
}

std::vector<std::string> speakerCommand(const std::optional<std::string>& sink) {
    std::vector<std::string> cmd{"pacat"};
    if (sink) {
        cmd.push_back("-d");
        cmd.push_back(*sink);
    }
    for (const char* a : {"--raw", "--rate=16000", "--channels=1", "--format=s16le", "--latency-msec=60"}) cmd.emplace_back(a);
    return cmd;
}

std::vector<std::string> fallbackPlayerCommand(const std::optional<std::string>& device, bool pulse) {
    if (!device && pulse) return {"pacat", "--raw", "--rate=16000", "--channels=1", "--format=s16le", "--latency-msec=60"};
    return {"aplay", "-q", "-D", device ? *device : std::string("default"), "-t", "raw", "-f", "S16_LE", "-r", "16000", "-c", "1"};
}

}
