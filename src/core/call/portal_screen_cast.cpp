#include "portal_screen_cast.h"

#include <QDateTime>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QPointer>

#include <algorithm>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace Zalos::Core::Call {

namespace {

constexpr quint32 kSpaTypeId = 3;
constexpr quint32 kSpaTypeInt = 4;
constexpr quint32 kSpaTypeRectangle = 10;
constexpr quint32 kSpaTypeFraction = 11;
constexpr quint32 kSpaTypeObject = 15;
constexpr quint32 kSpaTypeChoice = 19;

constexpr quint32 kSpaTypeObjectFormat = 0x40003;
constexpr quint32 kSpaTypeObjectParamBuffers = 0x40004;

constexpr quint32 kSpaChoiceRange = 1;
constexpr quint32 kSpaChoiceEnum = 3;

constexpr quint32 kSpaParamEnumFormat = 3;
constexpr quint32 kSpaParamFormat = 4;
constexpr quint32 kSpaParamBuffers = 5;

constexpr quint32 kSpaParamBuffersDataType = 6;

constexpr quint32 kSpaMediaTypeVideo = 2;
constexpr quint32 kSpaMediaSubtypeRaw = 1;

constexpr quint32 kSpaFormatMediaType = 1;
constexpr quint32 kSpaFormatMediaSubtype = 2;
constexpr quint32 kSpaFormatVideoFormat = 0x20001;
constexpr quint32 kSpaFormatVideoSize = 0x20003;
constexpr quint32 kSpaFormatVideoFramerate = 0x20004;

constexpr quint32 kSpaDataMemPtr = 1;
constexpr quint32 kSpaDataMemFd = 2;
constexpr qint32 kSpaChunkFlagCorrupted = 1;

constexpr quint32 kSpaDirectionInput = 0;
constexpr quint32 kPwVersionStreamEvents = 2;

constexpr int kPwStreamStateError = -1;
constexpr int kPwStreamStateUnconnected = 0;
constexpr int kPwStreamStateStreaming = 3;

constexpr quint32 kPwStreamFlagAutoconnect = 1u << 0;
constexpr quint32 kPwStreamFlagMapBuffers = 1u << 2;

struct SpaList {
    SpaList* next;
    SpaList* prev;
};

struct SpaCallbacks {
    const void* funcs;
    void* data;
};

struct SpaHook {
    SpaList link;
    SpaCallbacks cb;
    void (*removed)(SpaHook* hook);
    void* priv;
};

struct SpaRectangle {
    quint32 width;
    quint32 height;
};

struct SpaFraction {
    quint32 num;
    quint32 denom;
};

struct SpaPod {
    quint32 size;
    quint32 type;
};

struct SpaPodObjectBody {
    quint32 type;
    quint32 id;
};

struct SpaPodProp {
    quint32 key;
    quint32 flags;
    SpaPod value;
};

struct SpaPodChoiceBody {
    quint32 type;
    quint32 flags;
    SpaPod child;
};

struct SpaChunk {
    quint32 offset;
    quint32 size;
    qint32 stride;
    qint32 flags;
};

struct SpaData {
    quint32 type;
    quint32 flags;
    qint64 fd;
    quint32 mapoffset;
    quint32 maxsize;
    void* data;
    SpaChunk* chunk;
};

struct SpaBuffer {
    quint32 n_metas;
    quint32 n_datas;
    void* metas;
    SpaData* datas;
};

struct PwBuffer {
    SpaBuffer* buffer;
    void* user_data;
    quint64 size;
    quint64 requested;
    quint64 time;
};

struct PwStreamEvents {
    quint32 version;
    void (*destroy)(void* data);
    void (*state_changed)(void* data, int oldState, int state, const char* error);
    void (*control_info)(void* data, quint32 id, const void* control);
    void (*io_changed)(void* data, quint32 id, void* area, quint32 size);
    void (*param_changed)(void* data, quint32 id, const void* param);
    void (*add_buffer)(void* data, PwBuffer* buffer);
    void (*remove_buffer)(void* data, PwBuffer* buffer);
    void (*process)(void* data);
    void (*drained)(void* data);
    void (*command)(void* data, const void* command);
    void (*trigger_done)(void* data);
};

struct PipeWireApi {
    void* handle{nullptr};
    bool ok{false};

    void (*init)(int* argc, char*** argv){nullptr};
    void* (*threadLoopNew)(const char* name, const void* props){nullptr};
    void (*threadLoopDestroy)(void* loop){nullptr};
    int (*threadLoopStart)(void* loop){nullptr};
    void (*threadLoopStop)(void* loop){nullptr};
    void (*threadLoopLock)(void* loop){nullptr};
    void (*threadLoopUnlock)(void* loop){nullptr};
    void* (*threadLoopGetLoop)(void* loop){nullptr};
    void* (*contextNew)(void* mainLoop, void* props, size_t userDataSize){nullptr};
    void (*contextDestroy)(void* context){nullptr};
    void* (*contextConnectFd)(void* context, int fd, void* props, size_t userDataSize){nullptr};
    int (*coreDisconnect)(void* core){nullptr};
    void* (*propertiesNew)(const char* key, ...){nullptr};
    void (*propertiesFree)(void* props){nullptr};
    void* (*streamNew)(void* core, const char* name, void* props){nullptr};
    void (*streamDestroy)(void* stream){nullptr};
    void (*streamAddListener)(void* stream, SpaHook* listener, const PwStreamEvents* events, void* data){nullptr};
    int (*streamConnect)(void* stream, quint32 direction, quint32 targetId, quint32 flags,
                         const void** params, quint32 nParams){nullptr};
    int (*streamDisconnect)(void* stream){nullptr};
    int (*streamUpdateParams)(void* stream, const void** params, quint32 nParams){nullptr};
    PwBuffer* (*streamDequeueBuffer)(void* stream){nullptr};
    int (*streamQueueBuffer)(void* stream, PwBuffer* buffer){nullptr};

    static PipeWireApi& instance() {
        static PipeWireApi api;
        return api;
    }

private:
    PipeWireApi() {
        for (const char* name : {"libpipewire-0.3.so.0", "libpipewire-0.3.so"}) {
            handle = dlopen(name, RTLD_NOW | RTLD_LOCAL);
            if (handle) break;
        }
        if (!handle) return;

        auto sym = [this](const char* name) -> void* { return dlsym(handle, name); };
        init = reinterpret_cast<decltype(init)>(sym("pw_init"));
        threadLoopNew = reinterpret_cast<decltype(threadLoopNew)>(sym("pw_thread_loop_new"));
        threadLoopDestroy = reinterpret_cast<decltype(threadLoopDestroy)>(sym("pw_thread_loop_destroy"));
        threadLoopStart = reinterpret_cast<decltype(threadLoopStart)>(sym("pw_thread_loop_start"));
        threadLoopStop = reinterpret_cast<decltype(threadLoopStop)>(sym("pw_thread_loop_stop"));
        threadLoopLock = reinterpret_cast<decltype(threadLoopLock)>(sym("pw_thread_loop_lock"));
        threadLoopUnlock = reinterpret_cast<decltype(threadLoopUnlock)>(sym("pw_thread_loop_unlock"));
        threadLoopGetLoop = reinterpret_cast<decltype(threadLoopGetLoop)>(sym("pw_thread_loop_get_loop"));
        contextNew = reinterpret_cast<decltype(contextNew)>(sym("pw_context_new"));
        contextDestroy = reinterpret_cast<decltype(contextDestroy)>(sym("pw_context_destroy"));
        contextConnectFd = reinterpret_cast<decltype(contextConnectFd)>(sym("pw_context_connect_fd"));
        coreDisconnect = reinterpret_cast<decltype(coreDisconnect)>(sym("pw_core_disconnect"));
        propertiesNew = reinterpret_cast<decltype(propertiesNew)>(sym("pw_properties_new"));
        propertiesFree = reinterpret_cast<decltype(propertiesFree)>(sym("pw_properties_free"));
        streamNew = reinterpret_cast<decltype(streamNew)>(sym("pw_stream_new"));
        streamDestroy = reinterpret_cast<decltype(streamDestroy)>(sym("pw_stream_destroy"));
        streamAddListener = reinterpret_cast<decltype(streamAddListener)>(sym("pw_stream_add_listener"));
        streamConnect = reinterpret_cast<decltype(streamConnect)>(sym("pw_stream_connect"));
        streamDisconnect = reinterpret_cast<decltype(streamDisconnect)>(sym("pw_stream_disconnect"));
        streamUpdateParams = reinterpret_cast<decltype(streamUpdateParams)>(sym("pw_stream_update_params"));
        streamDequeueBuffer = reinterpret_cast<decltype(streamDequeueBuffer)>(sym("pw_stream_dequeue_buffer"));
        streamQueueBuffer = reinterpret_cast<decltype(streamQueueBuffer)>(sym("pw_stream_queue_buffer"));

        ok = init && threadLoopNew && threadLoopDestroy && threadLoopStart && threadLoopStop
             && threadLoopLock && threadLoopUnlock && threadLoopGetLoop
             && contextNew && contextDestroy && contextConnectFd && coreDisconnect
             && propertiesNew && propertiesFree && streamNew && streamDestroy
             && streamAddListener && streamConnect && streamDisconnect && streamUpdateParams
             && streamDequeueBuffer && streamQueueBuffer;
        if (!ok) {
            dlclose(handle);
            handle = nullptr;
            return;
        }
        init(nullptr, nullptr);
    }
};

constexpr quint32 alignUp8(quint32 n) {
    return (n + 7u) & ~7u;
}

void appendRaw(QByteArray& buf, const void* data, size_t len) {
    buf.append(reinterpret_cast<const char*>(data), int(len));
}

void padTo8(QByteArray& buf) {
    const int rem = buf.size() & 7;
    if (rem != 0) buf.append(QByteArray(8 - rem, '\0'));
}

void appendPropHeader(QByteArray& buf, quint32 key, quint32 valueSize, quint32 valueType) {
    const SpaPodProp prop{key, 0u, {valueSize, valueType}};
    appendRaw(buf, &prop, sizeof(prop));
}

void onPwStateChanged(void* data, int, int state, const char* error) {
    auto* self = static_cast<PortalScreenCast*>(data);
    if (self) self->handleStreamStateChanged(state, error);
}

void onPwParamChanged(void* data, quint32 id, const void* param) {
    auto* self = static_cast<PortalScreenCast*>(data);
    if (self) self->handleStreamParamChanged(id, param);
}

void onPwProcess(void* data) {
    auto* self = static_cast<PortalScreenCast*>(data);
    if (self) self->handleStreamProcess();
}

const PwStreamEvents kStreamEvents = {
    kPwVersionStreamEvents,
    nullptr,
    onPwStateChanged,
    nullptr,
    nullptr,
    onPwParamChanged,
    nullptr,
    nullptr,
    onPwProcess,
    nullptr,
    nullptr,
    nullptr,
};

QString portalSenderName(const QDBusConnection& bus) {
    QString sender = bus.baseService().mid(1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    return sender;
}

}

PortalScreenCast::PortalScreenCast(QObject* parent) : QObject(parent) {}

PortalScreenCast::~PortalScreenCast() {
    stop();
}

bool PortalScreenCast::pipeWireAvailable() {
    return PipeWireApi::instance().ok;
}

bool PortalScreenCast::available() {
    return pipeWireAvailable() && QDBusConnection::sessionBus().isConnected();
}

QByteArray PortalScreenCast::buildVideoFormatPod() {
    QByteArray buf;
    const SpaPod objPod{0u, kSpaTypeObject};
    const SpaPodObjectBody objBody{kSpaTypeObjectFormat, kSpaParamEnumFormat};
    appendRaw(buf, &objPod, sizeof(objPod));
    appendRaw(buf, &objBody, sizeof(objBody));

    const quint32 mediaType = kSpaMediaTypeVideo;
    appendPropHeader(buf, kSpaFormatMediaType, sizeof(mediaType), kSpaTypeId);
    appendRaw(buf, &mediaType, sizeof(mediaType));
    padTo8(buf);

    const quint32 mediaSubtype = kSpaMediaSubtypeRaw;
    appendPropHeader(buf, kSpaFormatMediaSubtype, sizeof(mediaSubtype), kSpaTypeId);
    appendRaw(buf, &mediaSubtype, sizeof(mediaSubtype));
    padTo8(buf);

    const quint32 formats[] = {
        kSpaVideoFormatBGRx,
        kSpaVideoFormatBGRx,
        kSpaVideoFormatBGRA,
        kSpaVideoFormatRGBx,
        kSpaVideoFormatRGBA
    };
    const quint32 fmtChoiceSize = quint32(sizeof(SpaPodChoiceBody) + sizeof(formats));
    appendPropHeader(buf, kSpaFormatVideoFormat, fmtChoiceSize, kSpaTypeChoice);
    const SpaPodChoiceBody fmtChoice{kSpaChoiceEnum, 0u, {sizeof(quint32), kSpaTypeId}};
    appendRaw(buf, &fmtChoice, sizeof(fmtChoice));
    appendRaw(buf, formats, sizeof(formats));
    padTo8(buf);

    const SpaRectangle rects[] = {
        {1280u, 720u},
        {1u, 1u},
        {8192u, 8192u}
    };
    const quint32 rectChoiceSize = quint32(sizeof(SpaPodChoiceBody) + sizeof(rects));
    appendPropHeader(buf, kSpaFormatVideoSize, rectChoiceSize, kSpaTypeChoice);
    const SpaPodChoiceBody rectChoice{kSpaChoiceRange, 0u, {sizeof(SpaRectangle), kSpaTypeRectangle}};
    appendRaw(buf, &rectChoice, sizeof(rectChoice));
    appendRaw(buf, rects, sizeof(rects));
    padTo8(buf);

    const SpaFraction rates[] = {
        {15u, 1u},
        {0u, 1u},
        {30u, 1u}
    };
    const quint32 rateChoiceSize = quint32(sizeof(SpaPodChoiceBody) + sizeof(rates));
    appendPropHeader(buf, kSpaFormatVideoFramerate, rateChoiceSize, kSpaTypeChoice);
    const SpaPodChoiceBody rateChoice{kSpaChoiceRange, 0u, {sizeof(SpaFraction), kSpaTypeFraction}};
    appendRaw(buf, &rateChoice, sizeof(rateChoice));
    appendRaw(buf, rates, sizeof(rates));
    padTo8(buf);

    auto* hdr = reinterpret_cast<SpaPod*>(buf.data());
    hdr->size = quint32(buf.size() - int(sizeof(SpaPod)));
    return buf;
}

QByteArray PortalScreenCast::buildBuffersParamPod() {
    QByteArray buf;
    const SpaPod objPod{0u, kSpaTypeObject};
    const SpaPodObjectBody objBody{kSpaTypeObjectParamBuffers, kSpaParamBuffers};
    appendRaw(buf, &objPod, sizeof(objPod));
    appendRaw(buf, &objBody, sizeof(objBody));

    const qint32 dataTypes = qint32((1u << kSpaDataMemPtr) | (1u << kSpaDataMemFd));
    appendPropHeader(buf, kSpaParamBuffersDataType, sizeof(dataTypes), kSpaTypeInt);
    appendRaw(buf, &dataTypes, sizeof(dataTypes));
    padTo8(buf);

    auto* hdr = reinterpret_cast<SpaPod*>(buf.data());
    hdr->size = quint32(buf.size() - int(sizeof(SpaPod)));
    return buf;
}

bool PortalScreenCast::parseVideoFormatPod(const void* pod, quint32* formatOut, int* widthOut, int* heightOut) {
    if (!pod) return false;
    const auto* top = static_cast<const SpaPod*>(pod);
    if (top->type != kSpaTypeObject || top->size < sizeof(SpaPodObjectBody)) return false;
    const auto* body = reinterpret_cast<const SpaPodObjectBody*>(top + 1);
    if (body->type != kSpaTypeObjectFormat) return false;

    const uchar* ptr = reinterpret_cast<const uchar*>(body + 1);
    const uchar* end = reinterpret_cast<const uchar*>(top + 1) + top->size;
    quint32 fmt = 0;
    int width = 0;
    int height = 0;

    while (ptr + sizeof(SpaPodProp) <= end) {
        const auto* prop = reinterpret_cast<const SpaPodProp*>(ptr);
        const uchar* valPtr = ptr + sizeof(SpaPodProp);
        if (valPtr + prop->value.size > end) break;

        if (prop->key == kSpaFormatVideoFormat) {
            if (prop->value.type == kSpaTypeId && prop->value.size >= sizeof(quint32)) {
                std::memcpy(&fmt, valPtr, sizeof(quint32));
            } else if (prop->value.type == kSpaTypeChoice
                       && prop->value.size >= sizeof(SpaPodChoiceBody) + sizeof(quint32)) {
                const auto* ch = reinterpret_cast<const SpaPodChoiceBody*>(valPtr);
                if (ch->child.type == kSpaTypeId && ch->child.size >= sizeof(quint32)) {
                    std::memcpy(&fmt, valPtr + sizeof(SpaPodChoiceBody), sizeof(quint32));
                }
            }
        } else if (prop->key == kSpaFormatVideoSize) {
            if (prop->value.type == kSpaTypeRectangle && prop->value.size >= sizeof(SpaRectangle)) {
                SpaRectangle r{};
                std::memcpy(&r, valPtr, sizeof(r));
                width = int(r.width);
                height = int(r.height);
            } else if (prop->value.type == kSpaTypeChoice
                       && prop->value.size >= sizeof(SpaPodChoiceBody) + sizeof(SpaRectangle)) {
                const auto* ch = reinterpret_cast<const SpaPodChoiceBody*>(valPtr);
                if (ch->child.type == kSpaTypeRectangle && ch->child.size >= sizeof(SpaRectangle)) {
                    SpaRectangle r{};
                    std::memcpy(&r, valPtr + sizeof(SpaPodChoiceBody), sizeof(r));
                    width = int(r.width);
                    height = int(r.height);
                }
            }
        }

        const quint32 step = sizeof(SpaPodProp) + alignUp8(prop->value.size);
        if (step < sizeof(SpaPodProp)) break;
        ptr += step;
    }

    if (fmt == 0 || width <= 0 || height <= 0) return false;
    if (formatOut) *formatOut = fmt;
    if (widthOut) *widthOut = width;
    if (heightOut) *heightOut = height;
    return true;
}

QImage PortalScreenCast::convertVideoBuffer(quint32 spaFormat, int width, int height,
                                            const uchar* data, int stride, int size) {
    if (!data || width <= 0 || height <= 0 || size <= 0) return {};
    QImage::Format imgFormat = QImage::Format_Invalid;
    switch (spaFormat) {
    case kSpaVideoFormatBGRx:
        imgFormat = QImage::Format_RGB32;
        break;
    case kSpaVideoFormatBGRA:
        imgFormat = QImage::Format_ARGB32;
        break;
    case kSpaVideoFormatRGBx:
        imgFormat = QImage::Format_RGBX8888;
        break;
    case kSpaVideoFormatRGBA:
        imgFormat = QImage::Format_RGBA8888;
        break;
    default:
        return {};
    }

    const int rowBytes = width * 4;
    const int srcStride = stride > 0 ? stride : rowBytes;
    if (srcStride < rowBytes) return {};
    if (qint64(srcStride) * qint64(height) > qint64(size)) return {};

    QImage out(width, height, imgFormat);
    if (out.isNull()) return {};
    for (int row = 0; row < height; ++row) {
        std::memcpy(out.scanLine(row), data + qint64(row) * srcStride, size_t(rowBytes));
    }
    return out;
}

bool PortalScreenCast::start() {
    stop();
    if (!available()) return false;
    m_active = true;
    m_streamingStarted = false;
    callCreateSession();
    return true;
}

void PortalScreenCast::stop() {
    ++m_generation;
    m_stage = Stage::Idle;
    m_active = false;
    m_streamingStarted = false;
    clearPortalRequest();
    teardownPipeWire();
    closePortalSession();
    m_videoFormat = 0;
    m_videoWidth = 0;
    m_videoHeight = 0;
    m_lastFrameNs = 0;
}

void PortalScreenCast::bindPortalRequest(const QString& path) {
    clearPortalRequest();
    m_requestPath = path;
    QDBusConnection::sessionBus().connect(
        QStringLiteral("org.freedesktop.portal.Desktop"), m_requestPath,
        QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
        this, SLOT(onPortalResponse(uint,QVariantMap)));
}

void PortalScreenCast::clearPortalRequest() {
    if (m_requestPath.isEmpty()) return;
    QDBusConnection::sessionBus().disconnect(
        QStringLiteral("org.freedesktop.portal.Desktop"), m_requestPath,
        QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
        this, SLOT(onPortalResponse(uint,QVariantMap)));
    m_requestPath.clear();
}

void PortalScreenCast::closePortalSession() {
    if (m_sessionHandle.isEmpty()) return;
    const QString handle = m_sessionHandle;
    m_sessionHandle.clear();
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"), handle,
        QStringLiteral("org.freedesktop.portal.Session"), QStringLiteral("Close"));
    QDBusConnection::sessionBus().asyncCall(msg);
}

void PortalScreenCast::callCreateSession() {
    QDBusConnection bus = QDBusConnection::sessionBus();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QString token = QStringLiteral("zalos_sc_req_%1_%2").arg(now).arg(m_generation);
    const QString sessionToken = QStringLiteral("zalos_sc_sess_%1_%2").arg(now).arg(m_generation);
    m_stage = Stage::CreateSession;
    bindPortalRequest(QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2")
                          .arg(portalSenderName(bus), token));

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.ScreenCast"),
        QStringLiteral("CreateSession"));
    msg << QVariantMap{{QStringLiteral("handle_token"), token},
                       {QStringLiteral("session_handle_token"), sessionToken}};

    const quint64 gen = m_generation;
    const QPointer<PortalScreenCast> self(this);
    auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, self, gen](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (!self || gen != m_generation || m_stage != Stage::CreateSession) return;
        if (w->isError()) {
            stop();
            emit failed(QStringLiteral("portal_create_session_failed"));
            return;
        }
        QDBusPendingReply<QDBusObjectPath> reply = *w;
        if (reply.isValid() && !reply.value().path().isEmpty() && reply.value().path() != m_requestPath) {
            bindPortalRequest(reply.value().path());
        }
    });
}

void PortalScreenCast::callSelectSources() {
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString token = QStringLiteral("zalos_sc_sel_%1_%2").arg(QDateTime::currentMSecsSinceEpoch()).arg(m_generation);
    m_stage = Stage::SelectSources;
    bindPortalRequest(QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2")
                          .arg(portalSenderName(bus), token));

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.ScreenCast"),
        QStringLiteral("SelectSources"));
    msg << QDBusObjectPath(m_sessionHandle)
        << QVariantMap{{QStringLiteral("handle_token"), token},
                       {QStringLiteral("types"), quint32(3)},
                       {QStringLiteral("multiple"), false},
                       {QStringLiteral("cursor_mode"), quint32(2)}};

    const quint64 gen = m_generation;
    const QPointer<PortalScreenCast> self(this);
    auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, self, gen](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (!self || gen != m_generation || m_stage != Stage::SelectSources) return;
        if (w->isError()) {
            stop();
            emit failed(QStringLiteral("portal_select_sources_failed"));
            return;
        }
        QDBusPendingReply<QDBusObjectPath> reply = *w;
        if (reply.isValid() && !reply.value().path().isEmpty() && reply.value().path() != m_requestPath) {
            bindPortalRequest(reply.value().path());
        }
    });
}

void PortalScreenCast::callStart() {
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString token = QStringLiteral("zalos_sc_start_%1_%2").arg(QDateTime::currentMSecsSinceEpoch()).arg(m_generation);
    m_stage = Stage::Start;
    bindPortalRequest(QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2")
                          .arg(portalSenderName(bus), token));

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.ScreenCast"),
        QStringLiteral("Start"));
    msg << QDBusObjectPath(m_sessionHandle) << QString()
        << QVariantMap{{QStringLiteral("handle_token"), token}};

    const quint64 gen = m_generation;
    const QPointer<PortalScreenCast> self(this);
    auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, self, gen](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (!self || gen != m_generation || m_stage != Stage::Start) return;
        if (w->isError()) {
            stop();
            emit failed(QStringLiteral("portal_start_failed"));
            return;
        }
        QDBusPendingReply<QDBusObjectPath> reply = *w;
        if (reply.isValid() && !reply.value().path().isEmpty() && reply.value().path() != m_requestPath) {
            bindPortalRequest(reply.value().path());
        }
    });
}

void PortalScreenCast::onPortalResponse(uint response, const QVariantMap& results) {
    clearPortalRequest();
    const Stage finishedStage = m_stage;
    if (finishedStage == Stage::Idle) return;

    if (response != 0) {
        const bool wasSelectOrStart = (finishedStage == Stage::SelectSources || finishedStage == Stage::Start);
        stop();
        if (wasSelectOrStart) emit cancelled();
        else emit failed(QStringLiteral("portal_rejected"));
        return;
    }

    if (finishedStage == Stage::CreateSession) {
        m_sessionHandle = results.value(QStringLiteral("session_handle")).toString();
        if (m_sessionHandle.isEmpty()) {
            stop();
            emit failed(QStringLiteral("portal_missing_session"));
            return;
        }
        callSelectSources();
        return;
    }

    if (finishedStage == Stage::SelectSources) {
        callStart();
        return;
    }

    if (finishedStage == Stage::Start) {
        quint32 nodeId = 0;
        const QVariant streamsVar = results.value(QStringLiteral("streams"));
        if (streamsVar.canConvert<QDBusArgument>()) {
            const auto arg = streamsVar.value<QDBusArgument>();
            arg.beginArray();
            while (!arg.atEnd()) {
                quint32 sid = 0;
                QVariantMap props;
                arg.beginStructure();
                arg >> sid >> props;
                arg.endStructure();
                if (sid != 0 && nodeId == 0) nodeId = sid;
            }
            arg.endArray();
        }
        if (nodeId == 0) {
            stop();
            emit cancelled();
            return;
        }
        callOpenPipeWireRemote(nodeId);
    }
}

void PortalScreenCast::callOpenPipeWireRemote(quint32 nodeId) {
    m_stage = Stage::OpenRemote;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.ScreenCast"),
        QStringLiteral("OpenPipeWireRemote"));
    msg << QDBusObjectPath(m_sessionHandle) << QVariantMap{};

    const quint64 gen = m_generation;
    const QPointer<PortalScreenCast> self(this);
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, self, gen, nodeId](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (!self || gen != m_generation || m_stage != Stage::OpenRemote) return;
        if (w->isError()) {
            stop();
            emit failed(QStringLiteral("portal_open_remote_failed"));
            return;
        }
        QDBusPendingReply<QDBusUnixFileDescriptor> reply = *w;
        if (!reply.isValid() || !reply.value().isValid()) {
            stop();
            emit failed(QStringLiteral("portal_invalid_fd"));
            return;
        }
        const int dupFd = ::fcntl(reply.value().fileDescriptor(), F_DUPFD_CLOEXEC, 0);
        if (dupFd < 0) {
            stop();
            emit failed(QStringLiteral("portal_fd_dup_failed"));
            return;
        }
        startPipeWireStream(dupFd, nodeId);
    });
}

void PortalScreenCast::startPipeWireStream(int fd, quint32 nodeId) {
    auto& api = PipeWireApi::instance();
    if (!api.ok) {
        ::close(fd);
        stop();
        emit failed(QStringLiteral("pipewire_unavailable"));
        return;
    }

    m_stage = Stage::Streaming;
    m_pwLoop = api.threadLoopNew("zalos-pw-screen", nullptr);
    if (!m_pwLoop) {
        ::close(fd);
        stop();
        emit failed(QStringLiteral("pipewire_loop_failed"));
        return;
    }

    m_pwContext = api.contextNew(api.threadLoopGetLoop(m_pwLoop), nullptr, 0);
    if (!m_pwContext) {
        ::close(fd);
        stop();
        emit failed(QStringLiteral("pipewire_context_failed"));
        return;
    }

    m_pwCore = api.contextConnectFd(m_pwContext, fd, nullptr, 0);
    if (!m_pwCore) {
        ::close(fd);
        stop();
        emit failed(QStringLiteral("pipewire_connect_failed"));
        return;
    }

    void* props = api.propertiesNew("media.type", "Video",
                                    "media.category", "Capture",
                                    "media.role", "Screen",
                                    nullptr);
    m_pwStream = api.streamNew(m_pwCore, "zalos-screencast", props);
    if (!m_pwStream) {
        if (props) api.propertiesFree(props);
        stop();
        emit failed(QStringLiteral("pipewire_stream_failed"));
        return;
    }

    auto* hook = new SpaHook{};
    m_pwHook = hook;
    api.streamAddListener(m_pwStream, hook, &kStreamEvents, this);

    const QByteArray fmtPod = buildVideoFormatPod();
    const void* params[1] = {fmtPod.constData()};
    if (api.streamConnect(m_pwStream, kSpaDirectionInput, nodeId,
                          kPwStreamFlagAutoconnect | kPwStreamFlagMapBuffers,
                          params, 1) < 0) {
        stop();
        emit failed(QStringLiteral("pipewire_stream_connect_failed"));
        return;
    }

    if (api.threadLoopStart(m_pwLoop) != 0) {
        stop();
        emit failed(QStringLiteral("pipewire_loop_start_failed"));
        return;
    }
}

void PortalScreenCast::teardownPipeWire() {
    auto& api = PipeWireApi::instance();
    if (m_pwLoop && api.ok) {
        api.threadLoopStop(m_pwLoop);
    }
    if (m_pwStream && api.ok) {
        api.streamDisconnect(m_pwStream);
        api.streamDestroy(m_pwStream);
        m_pwStream = nullptr;
    }
    delete static_cast<SpaHook*>(m_pwHook);
    m_pwHook = nullptr;
    if (m_pwCore && api.ok) {
        api.coreDisconnect(m_pwCore);
        m_pwCore = nullptr;
    }
    if (m_pwContext && api.ok) {
        api.contextDestroy(m_pwContext);
        m_pwContext = nullptr;
    }
    if (m_pwLoop && api.ok) {
        api.threadLoopDestroy(m_pwLoop);
        m_pwLoop = nullptr;
    }
}

void PortalScreenCast::handleStreamParamChanged(quint32 id, const void* param) {
    if (id != kSpaParamFormat || !param || !m_pwStream) return;
    quint32 fmt = 0;
    int width = 0;
    int height = 0;
    if (!parseVideoFormatPod(param, &fmt, &width, &height)) return;
    m_videoFormat = fmt;
    m_videoWidth = width;
    m_videoHeight = height;

    const QByteArray bufPod = buildBuffersParamPod();
    const void* params[1] = {bufPod.constData()};
    PipeWireApi::instance().streamUpdateParams(m_pwStream, params, 1);
}

void PortalScreenCast::handleStreamStateChanged(int state, const char* error) {
    const quint64 gen = m_generation;
    const QPointer<PortalScreenCast> self(this);
    if (state == kPwStreamStateStreaming) {
        if (!m_streamingStarted) {
            m_streamingStarted = true;
            QMetaObject::invokeMethod(this, [this, self, gen]() {
                if (!self || gen != m_generation) return;
                emit started();
            }, Qt::QueuedConnection);
        }
    } else if (state == kPwStreamStateError) {
        const QString msg = error ? QString::fromUtf8(error) : QStringLiteral("pipewire_stream_error");
        QMetaObject::invokeMethod(this, [this, self, gen, msg]() {
            if (!self || gen != m_generation) return;
            stop();
            emit failed(msg);
        }, Qt::QueuedConnection);
    } else if (state == kPwStreamStateUnconnected && m_streamingStarted) {
        QMetaObject::invokeMethod(this, [this, self, gen]() {
            if (!self || gen != m_generation) return;
            stop();
            emit stoppedBySystem();
        }, Qt::QueuedConnection);
    }
}

void PortalScreenCast::handleStreamProcess() {
    auto& api = PipeWireApi::instance();
    if (!m_pwStream || !api.ok) return;

    PwBuffer* newest = nullptr;
    for (;;) {
        PwBuffer* b = api.streamDequeueBuffer(m_pwStream);
        if (!b) break;
        if (newest) api.streamQueueBuffer(m_pwStream, newest);
        newest = b;
    }
    if (!newest) return;

    QImage frame;
    if (newest->buffer && newest->buffer->n_datas > 0 && newest->buffer->datas
        && m_videoFormat != 0 && m_videoWidth > 0 && m_videoHeight > 0) {
        const SpaData& d = newest->buffer->datas[0];
        const SpaChunk* chunk = d.chunk;
        const bool corrupted = chunk && (chunk->flags & kSpaChunkFlagCorrupted);
        if (!corrupted) {
            const qint64 nowNs = QDateTime::currentMSecsSinceEpoch() * 1000000LL;
            if (m_lastFrameNs == 0 || nowNs - m_lastFrameNs >= 60000000LL) {
                const uchar* base = static_cast<const uchar*>(d.data);
                void* mapped = nullptr;
                size_t mappedLen = 0;
                if (!base && d.type == kSpaDataMemFd && d.fd >= 0 && d.maxsize > 0) {
                    mappedLen = size_t(d.maxsize) + size_t(d.mapoffset);
                    mapped = ::mmap(nullptr, mappedLen, PROT_READ, MAP_SHARED, int(d.fd), 0);
                    if (mapped != MAP_FAILED && mapped) {
                        base = static_cast<const uchar*>(mapped) + d.mapoffset;
                    } else {
                        mapped = nullptr;
                    }
                }
                if (base) {
                    const quint32 offset = chunk ? chunk->offset : 0u;
                    const quint32 availMax = d.maxsize > offset ? (d.maxsize - offset) : 0u;
                    const quint32 dataSize = (chunk && chunk->size > 0u) ? std::min(chunk->size, availMax) : availMax;
                    const int stride = chunk ? int(chunk->stride) : (m_videoWidth * 4);
                    frame = convertVideoBuffer(m_videoFormat, m_videoWidth, m_videoHeight,
                                               base + offset, stride, int(dataSize));
                    if (!frame.isNull()) m_lastFrameNs = nowNs;
                }
                if (mapped) ::munmap(mapped, mappedLen);
            }
        }
    }

    api.streamQueueBuffer(m_pwStream, newest);

    if (!frame.isNull()) {
        const quint64 gen = m_generation;
        const QPointer<PortalScreenCast> self(this);
        QMetaObject::invokeMethod(this, [this, self, gen, frame]() {
            if (!self || gen != m_generation || m_stage != Stage::Streaming) return;
            emit frameReady(frame);
        }, Qt::QueuedConnection);
    }
}

}
