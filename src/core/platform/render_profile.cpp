#include "render_profile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QThread>
#include <QtGui/qtguiglobal.h>

#include <unistd.h>

namespace Zalos::Core::Platform {

namespace {

QString readTrimmed(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromLatin1(f.readAll()).trimmed() : QString();
}

bool envSet(const char* name) {
    return !qEnvironmentVariableIsEmpty(name);
}

}

QString RenderProfile::backendName(Backend b) {
    switch (b) {
    case Backend::Vulkan: return QStringLiteral("vulkan");
    case Backend::OpenGL: return QStringLiteral("opengl");
    case Backend::Software: return QStringLiteral("software");
    default: return QStringLiteral("default");
    }
}

RenderProfile RenderProfile::decide(const Inputs& in) {
    RenderProfile p;
    const QString o = in.override.toLower();
    if (o == "vulkan" || o == "opengl" || o == "software") {
        p.backend = o == "vulkan" ? Backend::Vulkan : o == "opengl" ? Backend::OpenGL : Backend::Software;
        p.reason = QStringLiteral("ZALOS_RENDERER=%1").arg(o);
    } else if (!in.hasRenderNode) {
        p.backend = Backend::Software;
        p.reason = QStringLiteral("no GPU render node: software renderer instead of llvmpipe");
    } else if ((in.gpuVendor == "0x1002" || in.gpuVendor == "0x8086") && in.mesaVulkanIcd && in.qtHasVulkan
               && !in.vulkanFailedBefore) {
        p.backend = Backend::Vulkan;
        p.reason = QStringLiteral("GPU %1 with Mesa Vulkan driver").arg(in.gpuVendor);
    } else {
        p.backend = Backend::Default;
        p.reason = in.vulkanFailedBefore ? QStringLiteral("Vulkan failed in an earlier launch: OpenGL")
                                         : QStringLiteral("GPU %1: Qt default").arg(in.gpuVendor);
    }
    const bool smallMachine = (in.memTotalKb > 0 && in.memTotalKb < 4LL * 1024 * 1024)
                              || (in.cpuCores > 0 && in.cpuCores < 4);
    p.basicRenderLoop = smallMachine && p.backend != Backend::Software;
    if (p.basicRenderLoop) p.reason += QStringLiteral(", small machine: basic render loop");
    return p;
}

RenderProfile::Inputs RenderProfile::probeSystem() {
    Inputs in;
    in.override = qEnvironmentVariable("ZALOS_RENDERER");
    const QStringList nodes = QDir(QStringLiteral("/dev/dri")).entryList({QStringLiteral("renderD*")}, QDir::System);
    for (const QString& node : nodes) {
        const QString path = QStringLiteral("/dev/dri/") + node;
        if (::access(path.toLocal8Bit().constData(), R_OK | W_OK) != 0) continue;
        in.hasRenderNode = true;
        in.gpuVendor = readTrimmed(QStringLiteral("/sys/class/drm/%1/device/vendor").arg(node));
        break;
    }
    const QString icdPrefix = in.gpuVendor == "0x1002" ? QStringLiteral("radeon_icd")
                            : in.gpuVendor == "0x8086" ? QStringLiteral("intel_icd") : QString();
    if (!icdPrefix.isEmpty()) {
        for (const QString& dir : {QStringLiteral("/usr/share/vulkan/icd.d"), QStringLiteral("/etc/vulkan/icd.d"),
                                   QStringLiteral("/usr/local/share/vulkan/icd.d")}) {
            if (!QDir(dir).entryList({icdPrefix + "*.json"}, QDir::Files).isEmpty()) {
                in.mesaVulkanIcd = true;
                break;
            }
        }
    }
#if QT_CONFIG(vulkan)
    in.qtHasVulkan = true;
#endif
    in.vulkanFailedBefore = RenderGuard::vulkanFailedBefore();
    const QString meminfo = readTrimmed(QStringLiteral("/proc/meminfo"));
    for (const QString& line : meminfo.split('\n')) {
        if (line.startsWith(QLatin1String("MemTotal:"))) {
            in.memTotalKb = line.section(':', 1).trimmed().section(' ', 0, 0).toLongLong();
            break;
        }
    }
    in.cpuCores = QThread::idealThreadCount();
    return in;
}

void RenderProfile::apply() const {
    const bool userChoseBackend = envSet("QSG_RHI_BACKEND") || envSet("QT_QUICK_BACKEND");
    if (!userChoseBackend) {
        if (backend == Backend::Software) qputenv("QT_QUICK_BACKEND", "software");
        else if (backend == Backend::Vulkan) qputenv("QSG_RHI_BACKEND", "vulkan");
        else if (backend == Backend::OpenGL) qputenv("QSG_RHI_BACKEND", "opengl");
    }
    if (backend == Backend::Vulkan && !envSet("VK_LOADER_DRIVERS_DISABLE") && !envSet("VK_ICD_FILENAMES")
        && !envSet("VK_DRIVER_FILES")) {
        qputenv("VK_LOADER_DRIVERS_DISABLE", "*lvp_icd*,*dzn_icd*");
    }
    if (basicRenderLoop && !envSet("QSG_RENDER_LOOP")) qputenv("QSG_RENDER_LOOP", "basic");
}

namespace RenderGuard {

bool vulkanFailedBefore() {
    QSettings s(QStringLiteral("Zalos"), QStringLiteral("ZalosApp"));
    if (s.value("render/pending").toString() == QLatin1String("vulkan")) {
        s.setValue("render/vulkanBroken", true);
        s.remove("render/pending");
    }
    return s.value("render/vulkanBroken", false).toBool();
}

void markAttempt(RenderProfile::Backend backend) {
    QSettings s(QStringLiteral("Zalos"), QStringLiteral("ZalosApp"));
    if (backend == RenderProfile::Backend::Vulkan) s.setValue("render/pending", QStringLiteral("vulkan"));
    else s.remove("render/pending");
    s.sync();
}

void markFirstFrame() {
    QSettings s(QStringLiteral("Zalos"), QStringLiteral("ZalosApp"));
    s.remove("render/pending");
    s.sync();
}

}

}
