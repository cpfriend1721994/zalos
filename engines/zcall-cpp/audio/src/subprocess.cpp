// SPDX-License-Identifier: BSD-3-Clause
#include "subprocess.h"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;

namespace zcall::audio {

std::optional<std::string> which(const std::string& name) {
    if (name.find('/') != std::string::npos) {
        if (::access(name.c_str(), X_OK) == 0) return name;
        return std::nullopt;
    }
    const char* path = std::getenv("PATH");
    std::string p = path ? path : "/usr/local/bin:/usr/bin:/bin";
    size_t start = 0;
    while (start <= p.size()) {
        size_t end = p.find(':', start);
        if (end == std::string::npos) end = p.size();
        std::string dir = p.substr(start, end - start);
        if (dir.empty()) dir = ".";
        std::string full = dir + "/" + name;
        struct stat st {};
        if (::stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode) && ::access(full.c_str(), X_OK) == 0) return full;
        start = end + 1;
    }
    return std::nullopt;
}

namespace {

struct Exec {
    std::string path;
    std::vector<std::string> args;
    std::vector<std::string> env;
    std::vector<char*> argv;
    std::vector<char*> envp;

    bool prepare(const std::vector<std::string>& in, bool cLocale, std::string* error) {
        if (in.empty()) {
            if (error) *error = "empty command";
            return false;
        }
        auto found = which(in[0]);
        if (!found) {
            if (error) *error = "[Errno 2] No such file or directory: '" + in[0] + "'";
            return false;
        }
        path = *found;
        args = in;
        for (auto& a : args) argv.push_back(a.data());
        argv.push_back(nullptr);
        for (char** e = environ; e && *e; ++e) {
            if (cLocale && std::strncmp(*e, "LC_ALL=", 7) == 0) continue;
            env.emplace_back(*e);
        }
        if (cLocale) env.emplace_back("LC_ALL=C");
        for (auto& e : env) envp.push_back(e.data());
        envp.push_back(nullptr);
        return true;
    }
};

void closeFd(int& fd) {
    if (fd >= 0) ::close(fd);
    fd = -1;
}

}

bool readExact(int fd, void* buffer, size_t n) {
    auto* p = static_cast<char*>(buffer);
    size_t got = 0;
    while (got < n) {
        const ssize_t r = ::read(fd, p + got, n - got);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return false;
        got += static_cast<size_t>(r);
    }
    return true;
}

bool writeAll(int fd, const void* data, size_t n) {
    const auto* p = static_cast<const char*>(data);
    size_t done = 0;
    while (done < n) {
        const ssize_t w = ::write(fd, p + done, n - done);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return false;
        done += static_cast<size_t>(w);
    }
    return true;
}

std::unique_ptr<Process> Process::spawn(const std::vector<std::string>& argv, const Options& options, std::string* error) {
    Exec exec;
    if (!exec.prepare(argv, false, error)) return nullptr;
    int inPipe[2] = {-1, -1};
    int outPipe[2] = {-1, -1};
    if (options.in == Io::Pipe && ::pipe2(inPipe, O_CLOEXEC) != 0) {
        if (error) *error = std::strerror(errno);
        return nullptr;
    }
    if (options.out == Io::Pipe && ::pipe2(outPipe, O_CLOEXEC) != 0) {
        if (error) *error = std::strerror(errno);
        closeFd(inPipe[0]);
        closeFd(inPipe[1]);
        return nullptr;
    }
    const int devnull = ::open("/dev/null", O_RDWR | O_CLOEXEC);
    const pid_t parent = ::getpid();
    const pid_t pid = ::fork();
    if (pid < 0) {
        if (error) *error = std::strerror(errno);
        closeFd(inPipe[0]);
        closeFd(inPipe[1]);
        closeFd(outPipe[0]);
        closeFd(outPipe[1]);
        if (devnull >= 0) ::close(devnull);
        return nullptr;
    }
    if (pid == 0) {
        if (options.dieWithParent) {
            ::prctl(PR_SET_PDEATHSIG, SIGTERM);
            if (::getppid() != parent) ::_exit(127);
        }
        ::signal(SIGPIPE, SIG_DFL);
        if (options.in == Io::Pipe) ::dup2(inPipe[0], 0);
        else if (options.in == Io::Null) ::dup2(devnull, 0);
        if (options.out == Io::Pipe) ::dup2(outPipe[1], 1);
        else if (options.out == Io::Null) ::dup2(devnull, 1);
        if (options.err == Io::Null) ::dup2(devnull, 2);
        ::execve(exec.path.c_str(), exec.argv.data(), exec.envp.data());
        ::_exit(127);
    }
    if (devnull >= 0) ::close(devnull);
    std::unique_ptr<Process> p(new Process);
    p->m_pid = pid;
    if (options.in == Io::Pipe) {
        ::close(inPipe[0]);
        p->m_in = inPipe[1];
    }
    if (options.out == Io::Pipe) {
        ::close(outPipe[1]);
        p->m_out = outPipe[0];
    }
    return p;
}

Process::~Process() {
    if (!m_reaped && m_pid > 0) {
        kill();
        wait(1000);
    }
    closeFd(m_in);
    closeFd(m_out);
}

bool Process::running() {
    if (m_reaped || m_pid <= 0) return false;
    int status = 0;
    const pid_t r = ::waitpid(m_pid, &status, WNOHANG);
    if (r == m_pid) {
        m_reaped = true;
        return false;
    }
    return r == 0;
}

void Process::closeStdin() { closeFd(m_in); }

void Process::terminate() {
    if (!m_reaped && m_pid > 0) ::kill(m_pid, SIGTERM);
}

void Process::kill() {
    if (!m_reaped && m_pid > 0) ::kill(m_pid, SIGKILL);
}

bool Process::wait(int timeoutMs) {
    if (m_reaped || m_pid <= 0) return true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        int status = 0;
        const pid_t r = ::waitpid(m_pid, &status, timeoutMs < 0 ? 0 : WNOHANG);
        if (r == m_pid || (r < 0 && errno == ECHILD)) {
            m_reaped = true;
            return true;
        }
        if (timeoutMs >= 0 && std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void Process::stop(int graceMs) {
    if (m_reaped) return;
    terminate();
    if (!wait(graceMs)) {
        kill();
        wait(-1);
    }
}

RunResult run(const std::vector<std::string>& argv, int timeoutMs, bool cLocale) {
    RunResult result;
    Exec exec;
    if (!exec.prepare(argv, cLocale, &result.error)) return result;
    int outPipe[2];
    int errPipe[2];
    if (::pipe2(outPipe, O_CLOEXEC) != 0) {
        result.error = std::strerror(errno);
        return result;
    }
    if (::pipe2(errPipe, O_CLOEXEC) != 0) {
        result.error = std::strerror(errno);
        ::close(outPipe[0]);
        ::close(outPipe[1]);
        return result;
    }
    const int devnull = ::open("/dev/null", O_RDWR | O_CLOEXEC);
    const pid_t pid = ::fork();
    if (pid < 0) {
        result.error = std::strerror(errno);
        for (int fd : {outPipe[0], outPipe[1], errPipe[0], errPipe[1], devnull})
            if (fd >= 0) ::close(fd);
        return result;
    }
    if (pid == 0) {
        ::signal(SIGPIPE, SIG_DFL);
        ::dup2(devnull, 0);
        ::dup2(outPipe[1], 1);
        ::dup2(errPipe[1], 2);
        ::execve(exec.path.c_str(), exec.argv.data(), exec.envp.data());
        ::_exit(127);
    }
    if (devnull >= 0) ::close(devnull);
    ::close(outPipe[1]);
    ::close(errPipe[1]);
    result.started = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    char buf[4096];
    pollfd pfd[2] = {{outPipe[0], POLLIN, 0}, {errPipe[0], POLLIN, 0}};
    int live = 2;
    while (live > 0) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) {
            result.timedOut = true;
            break;
        }
        const int pr = ::poll(pfd, 2, static_cast<int>(left));
        if (pr < 0 && errno == EINTR) continue;
        if (pr <= 0) {
            result.timedOut = pr == 0;
            break;
        }
        for (int i = 0; i < 2; ++i) {
            if (pfd[i].fd < 0 || !(pfd[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            const ssize_t n = ::read(pfd[i].fd, buf, sizeof buf);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) {
                ::close(pfd[i].fd);
                pfd[i].fd = -1;
                --live;
                continue;
            }
            (i == 0 ? result.out : result.err).append(buf, static_cast<size_t>(n));
        }
    }
    for (auto& p : pfd)
        if (p.fd >= 0) ::close(p.fd);
    int status = 0;
    if (result.timedOut) {
        ::kill(pid, SIGKILL);
        ::waitpid(pid, &status, 0);
        return result;
    }
    ::waitpid(pid, &status, 0);
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

}
