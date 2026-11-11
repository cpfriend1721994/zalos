// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace zcall::audio {

std::optional<std::string> which(const std::string& name);

struct RunResult {
    bool started{false};
    bool timedOut{false};
    int exitCode{-1};
    std::string out;
    std::string err;
    std::string error;
    bool ok() const { return started && !timedOut; }
};

RunResult run(const std::vector<std::string>& argv, int timeoutMs, bool cLocale = false);

class Process {
public:
    enum class Io { Inherit, Pipe, Null };
    struct Options {
        Io in{Io::Inherit};
        Io out{Io::Inherit};
        Io err{Io::Inherit};
        bool dieWithParent{false};
    };

    static std::unique_ptr<Process> spawn(const std::vector<std::string>& argv, const Options& options, std::string* error);
    ~Process();

    int stdinFd() const { return m_in; }
    int stdoutFd() const { return m_out; }
    int pid() const { return m_pid; }

    bool running();
    void closeStdin();
    void terminate();
    void kill();
    bool wait(int timeoutMs);
    void stop(int graceMs);

private:
    Process() = default;
    int m_pid{-1};
    int m_in{-1};
    int m_out{-1};
    bool m_reaped{false};
};

bool readExact(int fd, void* buffer, size_t n);
bool writeAll(int fd, const void* data, size_t n);

}
