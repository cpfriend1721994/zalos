// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "subprocess.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace zcall::audio {

class Sink {
public:
    virtual ~Sink() = default;
    virtual bool write(const int16_t* pcm, int samples, std::string* error) = 0;
    virtual void close() = 0;
    virtual bool paced() const { return false; }
    virtual std::optional<double> latencyMs() { return std::nullopt; }
    std::atomic<bool> silent{false};
};

class PulseOut : public Sink {
public:
    static constexpr int kLatencyMs = 60;
    static std::unique_ptr<PulseOut> open(const std::optional<std::string>& device, std::string* error);
    ~PulseOut() override;
    bool write(const int16_t* pcm, int samples, std::string* error) override;
    void close() override;
    bool paced() const override { return true; }
    std::optional<double> latencyMs() override;
    bool switchTo(const std::optional<std::string>& device, std::string* error);

private:
    struct Lib;
    PulseOut() = default;
    void* openStream(const std::optional<std::string>& device, std::string* error);
    std::shared_ptr<Lib> m_lib;
    std::mutex m_lock;
    void* m_stream{nullptr};
    bool m_failed{false};
    std::vector<int16_t> m_zero;
};

class PipeSink : public Sink {
public:
    static std::unique_ptr<PipeSink> start(const std::vector<std::string>& cmd, std::string* error);
    bool write(const int16_t* pcm, int samples, std::string* error) override;
    void close() override;
    bool replace(const std::vector<std::string>& cmd, std::string* error);

private:
    std::mutex m_lock;
    std::unique_ptr<Process> m_proc;
    std::vector<int16_t> m_zero;
};

class FileSink : public Sink {
public:
    static std::unique_ptr<FileSink> open(const std::string& path, std::string* error);
    ~FileSink() override;
    bool write(const int16_t* pcm, int samples, std::string* error) override;
    void close() override;

private:
    std::FILE* m_file{nullptr};
    bool m_wav{false};
    uint32_t m_bytes{0};
    std::vector<int16_t> m_zero;
};

std::vector<std::string> speakerCommand(const std::optional<std::string>& sink);
std::vector<std::string> fallbackPlayerCommand(const std::optional<std::string>& device, bool pulse);

}
