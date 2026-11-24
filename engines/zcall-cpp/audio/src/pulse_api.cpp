// SPDX-License-Identifier: BSD-3-Clause
#include "pulse_api.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <dlfcn.h>
#include <mutex>

namespace zcall::audio {

namespace {

constexpr int kPaContextReady = 4;
constexpr int kPaContextFailed = 5;
constexpr int kPaContextTerminated = 6;
constexpr int kPaContextNoAutospawn = 1;
constexpr int kPaOperationRunning = 0;
constexpr uint32_t kPaVolumeNorm = 65536u;

struct PaSampleSpec {
    int format;
    uint32_t rate;
    uint8_t channels;
};

struct PaChannelMap {
    uint8_t channels;
    int map[32];
};

struct PaCVolume {
    uint8_t channels;
    uint32_t values[32];
};

struct PaServerInfo {
    const char* user_name;
    const char* host_name;
    const char* server_version;
    const char* server_name;
    PaSampleSpec sample_spec;
    const char* default_sink_name;
    const char* default_source_name;
    uint32_t cookie;
    PaChannelMap channel_map;
};

struct PaDeviceInfoPrefix {
    const char* name;
    uint32_t index;
    const char* description;
    PaSampleSpec sample_spec;
    PaChannelMap channel_map;
    uint32_t owner_module;
    PaCVolume volume;
};

struct PaLib {
    void* handle{nullptr};
    void* (*mainloopNew)(){nullptr};
    void (*mainloopFree)(void*){nullptr};
    void* (*mainloopGetApi)(void*){nullptr};
    int (*mainloopPrepare)(void*, int){nullptr};
    int (*mainloopPoll)(void*){nullptr};
    int (*mainloopDispatch)(void*){nullptr};
    void* (*contextNew)(void*, const char*){nullptr};
    void (*contextUnref)(void*){nullptr};
    int (*contextConnect)(void*, const char*, int, const void*){nullptr};
    void (*contextDisconnect)(void*){nullptr};
    int (*contextGetState)(const void*){nullptr};
    void* (*contextGetServerInfo)(void*, void (*)(void*, const PaServerInfo*, void*), void*){nullptr};
    void* (*contextGetSourceInfoList)(void*, void (*)(void*, const PaDeviceInfoPrefix*, int, void*), void*){nullptr};
    void* (*contextGetSinkInfoList)(void*, void (*)(void*, const PaDeviceInfoPrefix*, int, void*), void*){nullptr};
    void* (*contextGetSourceInfoByName)(void*, const char*, void (*)(void*, const PaDeviceInfoPrefix*, int, void*),
                                        void*){nullptr};
    void* (*contextSetSourceVolumeByName)(void*, const char*, const PaCVolume*, void (*)(void*, int, void*), void*){nullptr};
    void (*operationUnref)(void*){nullptr};
    int (*operationGetState)(const void*){nullptr};

    static const PaLib* get() {
        static const PaLib lib = load();
        return lib.handle ? &lib : nullptr;
    }

private:
    template <typename Fn>
    static bool bind(void* h, const char* name, Fn& out) {
        out = reinterpret_cast<Fn>(::dlsym(h, name));
        return out != nullptr;
    }

    static PaLib load() {
        PaLib l;
        void* h = ::dlopen("libpulse.so.0", RTLD_NOW | RTLD_LOCAL);
        if (!h) return l;
        const bool ok = bind(h, "pa_mainloop_new", l.mainloopNew) && bind(h, "pa_mainloop_free", l.mainloopFree)
            && bind(h, "pa_mainloop_get_api", l.mainloopGetApi)
            && bind(h, "pa_mainloop_prepare", l.mainloopPrepare)
            && bind(h, "pa_mainloop_poll", l.mainloopPoll)
            && bind(h, "pa_mainloop_dispatch", l.mainloopDispatch)
            && bind(h, "pa_context_new", l.contextNew) && bind(h, "pa_context_unref", l.contextUnref)
            && bind(h, "pa_context_connect", l.contextConnect) && bind(h, "pa_context_disconnect", l.contextDisconnect)
            && bind(h, "pa_context_get_state", l.contextGetState)
            && bind(h, "pa_context_get_server_info", l.contextGetServerInfo)
            && bind(h, "pa_context_get_source_info_list", l.contextGetSourceInfoList)
            && bind(h, "pa_context_get_sink_info_list", l.contextGetSinkInfoList)
            && bind(h, "pa_context_get_source_info_by_name", l.contextGetSourceInfoByName)
            && bind(h, "pa_context_set_source_volume_by_name", l.contextSetSourceVolumeByName)
            && bind(h, "pa_operation_unref", l.operationUnref)
            && bind(h, "pa_operation_get_state", l.operationGetState);
        if (!ok) {
            ::dlclose(h);
            return PaLib{};
        }
        l.handle = h;
        return l;
    }
};

struct PaSession {
    const PaLib* lib{nullptr};
    void* ml{nullptr};
    void* ctx{nullptr};
    bool connected{false};

    ~PaSession() {
        if (ctx) {
            if (connected) lib->contextDisconnect(ctx);
            lib->contextUnref(ctx);
        }
        if (ml) lib->mainloopFree(ml);
    }

    bool stepUntil(std::chrono::steady_clock::time_point deadline) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return false;
        const auto remUs = std::chrono::duration_cast<std::chrono::microseconds>(deadline - now).count();
        const int sliceUs = static_cast<int>(std::min<int64_t>(50000, std::max<int64_t>(1, remUs)));
        if (lib->mainloopPrepare(ml, sliceUs) < 0) return false;
        if (lib->mainloopPoll(ml) < 0) return false;
        return lib->mainloopDispatch(ml) >= 0;
    }

    bool connect(int timeoutMs = 1500) {
        lib = PaLib::get();
        if (!lib) return false;
        ml = lib->mainloopNew();
        if (!ml) return false;
        void* api = lib->mainloopGetApi(ml);
        ctx = lib->contextNew(api, "Zalo");
        if (!ctx) return false;
        if (lib->contextConnect(ctx, nullptr, kPaContextNoAutospawn, nullptr) < 0) return false;
        connected = true;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            const int st = lib->contextGetState(ctx);
            if (st == kPaContextReady) return true;
            if (st == kPaContextFailed || st == kPaContextTerminated) return false;
            if (!stepUntil(deadline)) return false;
        }
        return false;
    }

    bool waitOp(void* op, const bool& done, int timeoutMs = 2000) {
        if (!op) return false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        bool ok = false;
        while (std::chrono::steady_clock::now() < deadline) {
            if (done || lib->operationGetState(op) != kPaOperationRunning) {
                ok = true;
                break;
            }
            const int st = lib->contextGetState(ctx);
            if (st == kPaContextFailed || st == kPaContextTerminated) break;
            if (!stepUntil(deadline)) break;
        }
        lib->operationUnref(op);
        return ok;
    }
};

}

std::shared_ptr<PulseSimpleApi> PulseSimpleApi::load(std::string* error) {
    static std::mutex s_mutex;
    static std::weak_ptr<PulseSimpleApi> s_cached;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (auto existing = s_cached.lock()) return existing;

    void* h = ::dlopen("libpulse-simple.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        const char* e = ::dlerror();
        if (error) *error = e ? e : "libpulse-simple.so.0: cannot open shared object file";
        return nullptr;
    }
    auto deleter = [](PulseSimpleApi* p) {
        if (p->handle) ::dlclose(p->handle);
        delete p;
    };
    std::shared_ptr<PulseSimpleApi> api(new PulseSimpleApi, deleter);
    api->handle = h;
    api->simpleNew = reinterpret_cast<decltype(api->simpleNew)>(::dlsym(h, "pa_simple_new"));
    api->simpleWrite = reinterpret_cast<decltype(api->simpleWrite)>(::dlsym(h, "pa_simple_write"));
    api->simpleRead = reinterpret_cast<decltype(api->simpleRead)>(::dlsym(h, "pa_simple_read"));
    api->simpleFree = reinterpret_cast<decltype(api->simpleFree)>(::dlsym(h, "pa_simple_free"));
    api->simpleGetLatency = reinterpret_cast<decltype(api->simpleGetLatency)>(::dlsym(h, "pa_simple_get_latency"));
    if (!api->simpleNew || !api->simpleWrite || !api->simpleRead || !api->simpleFree || !api->simpleGetLatency) {
        if (error) *error = "libpulse-simple.so.0 lacks pa_simple_*";
        return nullptr;
    }
    s_cached = api;
    return api;
}

bool PulseApi::available() { return PaLib::get() != nullptr; }

bool PulseApi::isRunning() {
    PaSession s;
    return s.connect();
}

std::optional<std::string> PulseApi::defaultSource() {
    PaSession s;
    if (!s.connect()) return std::nullopt;
    struct State {
        bool done{false};
        std::optional<std::string> src;
    } state;
    auto cb = [](void*, const PaServerInfo* info, void* ud) {
        auto* st = static_cast<State*>(ud);
        if (info && info->default_source_name && *info->default_source_name)
            st->src = std::string(info->default_source_name);
        st->done = true;
    };
    void* op = s.lib->contextGetServerInfo(s.ctx, cb, &state);
    s.waitOp(op, state.done);
    return state.src;
}

std::vector<std::string> PulseApi::listNames(bool sources) {
    PaSession s;
    if (!s.connect()) return {};
    struct State {
        bool done{false};
        std::vector<std::string> names;
    } state;
    auto cb = [](void*, const PaDeviceInfoPrefix* info, int eol, void* ud) {
        auto* st = static_cast<State*>(ud);
        if (eol) {
            st->done = true;
            return;
        }
        if (info && info->name && *info->name) st->names.emplace_back(info->name);
    };
    void* op = sources ? s.lib->contextGetSourceInfoList(s.ctx, cb, &state)
                       : s.lib->contextGetSinkInfoList(s.ctx, cb, &state);
    s.waitOp(op, state.done);
    return state.names;
}

bool PulseApi::hasSourceAndSink(const std::string& source, const std::string& sink) {
    PaSession s;
    if (!s.connect()) return false;
    struct MatchState {
        std::string target;
        bool found{false};
        bool done{false};
    };
    auto matchCb = [](void*, const PaDeviceInfoPrefix* info, int eol, void* ud) {
        auto* st = static_cast<MatchState*>(ud);
        if (eol) {
            st->done = true;
            return;
        }
        if (info && info->name && st->target == info->name) st->found = true;
    };
    MatchState src{source};
    s.waitOp(s.lib->contextGetSourceInfoList(s.ctx, matchCb, &src), src.done);
    if (!src.found) return false;
    MatchState snk{sink};
    s.waitOp(s.lib->contextGetSinkInfoList(s.ctx, matchCb, &snk), snk.done);
    return snk.found;
}

std::optional<int> PulseApi::getSourceVolumePercent(const std::string& source) {
    PaSession s;
    if (!s.connect()) return std::nullopt;
    struct State {
        bool done{false};
        std::optional<int> pct;
    } state;
    auto cb = [](void*, const PaDeviceInfoPrefix* info, int eol, void* ud) {
        auto* st = static_cast<State*>(ud);
        if (eol) {
            st->done = true;
            return;
        }
        if (info && info->volume.channels > 0 && !st->pct) {
            const uint32_t v = info->volume.values[0];
            st->pct = static_cast<int>(std::llround(100.0 * static_cast<double>(v) / kPaVolumeNorm));
        }
    };
    void* op = s.lib->contextGetSourceInfoByName(s.ctx, source.c_str(), cb, &state);
    s.waitOp(op, state.done);
    return state.pct;
}

bool PulseApi::setSourceVolumeRaw(const std::string& source, uint32_t rawVolume) {
    PaSession s;
    if (!s.connect()) return false;
    struct State {
        bool done{false};
        uint8_t channels{0};
    } infoState;
    auto infoCb = [](void*, const PaDeviceInfoPrefix* info, int eol, void* ud) {
        auto* st = static_cast<State*>(ud);
        if (eol) {
            st->done = true;
            return;
        }
        if (info && info->volume.channels > 0 && st->channels == 0)
            st->channels = std::min<uint8_t>(info->volume.channels, 32);
    };
    void* op1 = s.lib->contextGetSourceInfoByName(s.ctx, source.c_str(), infoCb, &infoState);
    if (!s.waitOp(op1, infoState.done) || infoState.channels == 0) return false;

    PaCVolume vol{};
    vol.channels = infoState.channels;
    for (uint8_t i = 0; i < vol.channels; ++i) vol.values[i] = rawVolume;

    struct SetState {
        bool done{false};
        bool ok{false};
    } setState;
    auto setCb = [](void*, int success, void* ud) {
        auto* st = static_cast<SetState*>(ud);
        st->ok = success != 0;
        st->done = true;
    };
    void* op2 = s.lib->contextSetSourceVolumeByName(s.ctx, source.c_str(), &vol, setCb, &setState);
    return s.waitOp(op2, setState.done) && setState.ok;
}

}
