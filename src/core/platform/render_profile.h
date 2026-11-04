#pragma once

#include <QString>

namespace Zalos::Core::Platform {

struct RenderProfile {
    enum class Backend { Default, Vulkan, OpenGL, Software };
    Backend backend{Backend::Default};
    bool basicRenderLoop{false};
    QString reason;

    struct Inputs {
        bool hasRenderNode{false};
        QString gpuVendor;
        bool mesaVulkanIcd{false};
        bool qtHasVulkan{false};
        bool vulkanFailedBefore{false};
        qint64 memTotalKb{0};
        int cpuCores{0};
        QString override;
    };

    static RenderProfile decide(const Inputs& in);
    static Inputs probeSystem();
    static QString backendName(Backend b);

    void apply() const;
};

namespace RenderGuard {
bool vulkanFailedBefore();
void markAttempt(RenderProfile::Backend backend);
void markFirstFrame();
}

}
