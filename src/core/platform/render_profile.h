#pragma once

#include <QString>

namespace Zalos::Core::Platform {

// Picks the Qt Quick rendering setup from the machine's capabilities, before QGuiApplication exists.
// Goal: lowest memory that still renders smoothly, with no question to the user.
//  - no usable GPU (VM, remote desktop, missing driver) -> Qt Quick software renderer: Mesa's llvmpipe would
//    load LLVM (~35 MB) and big buffers to emulate a GPU on the CPU anyway;
//  - AMD / Intel GPU with a Mesa Vulkan driver -> Vulkan (RADV/ANV compile shaders without LLVM);
//    a launch that died while initialising Vulkan is remembered and OpenGL is used from then on;
//  - everything else (NVIDIA, unknown) -> Qt's default (OpenGL);
//  - small machines (< 4 GiB RAM or < 4 cores) -> single-threaded render loop (one thread and buffer set less).
// ZALOS_RENDERER=auto|vulkan|opengl|software, QSG_RHI_BACKEND, QT_QUICK_BACKEND and QSG_RENDER_LOOP set by
// the user always win.
struct RenderProfile {
    enum class Backend { Default, Vulkan, OpenGL, Software };
    Backend backend{Backend::Default};
    bool basicRenderLoop{false};
    QString reason; // one line for the log

    struct Inputs {
        bool hasRenderNode{false};      // an accessible /dev/dri/renderD*
        QString gpuVendor;              // PCI vendor id of that node, e.g. "0x1002"
        bool mesaVulkanIcd{false};      // a Mesa Vulkan ICD for that vendor is installed
        bool qtHasVulkan{false};        // Qt was built with Vulkan support
        bool vulkanFailedBefore{false}; // a previous launch never got a first frame with Vulkan
        qint64 memTotalKb{0};
        int cpuCores{0};
        QString override;               // ZALOS_RENDERER
    };

    static RenderProfile decide(const Inputs& in);
    static Inputs probeSystem();
    static QString backendName(Backend b);

    // Exports the choice as the Qt environment variables (only those the user did not set).
    void apply() const;
};

// Crash guard for the Vulkan choice: mark before the window is created, clear after the first frame.
namespace RenderGuard {
bool vulkanFailedBefore();
void markAttempt(RenderProfile::Backend backend);
void markFirstFrame();
} // namespace RenderGuard

} // namespace Zalos::Core::Platform
