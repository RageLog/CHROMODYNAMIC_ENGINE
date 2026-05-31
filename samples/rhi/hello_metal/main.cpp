// =============================================================================
// CHROMODYNAMIC — samples/rhi/hello_metal/main.cpp
// phase531 — Metal backend MVP boot smoke.
//
// Compiled only when CD_RHI_METAL_ENABLED=ON (macOS / iOS host).
// On Win11 this whole subdirectory is excluded from the build by CMake.
//
// What this sample proves (phase531):
//   1. cd::rhi::metal::create_metal_device() is reachable.
//   2. The returned IDevice reports Backend::kMetal.
//   3. create_buffer / create_texture hand back valid-looking handles.
//   4. wait_idle() does not crash.
//   5. The process exits 0.
//
// No swapchain, no draw, no window — those land in Phase 9 Sprint 1.
// If this sample exits 0, the Metal .mm compilation pipeline and the
// factory entry-point are alive.
// =============================================================================
#include <cd/core/Version.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/metal/MetalDevice.hpp>
#include <cd/rhi/Descriptors.hpp>

#include <cstdio>
#include <cstdlib>

int main()
{
    std::fprintf(stdout,
                 "CHROMODYNAMIC %u.%u.%u — hello_metal\n",
                 static_cast<unsigned>(cd::core::kEngineVersion.major),
                 static_cast<unsigned>(cd::core::kEngineVersion.minor),
                 static_cast<unsigned>(cd::core::kEngineVersion.patch));

    // ---- 1. Create the Metal device ----------------------------------------
    cd::rhi::metal::MetalCreateInfo info {};
    info.app_name = "hello_metal";
    info.prefer_discrete_gpu = true;
    // Validation on for smoke runs so validation-layer errors surface early.
    info.enable_validation = true;

    auto dev_r = cd::rhi::metal::create_metal_device(info);
    if (!dev_r.has_value())
    {
        std::fprintf(stderr,
                     "[hello_metal] create_metal_device failed: %.*s\n",
                     static_cast<int>(dev_r.error().message.size()),
                     dev_r.error().message.data());
        return EXIT_FAILURE;
    }
    auto& device = **dev_r;

    // ---- 2. Verify backend + adapter identity --------------------------------
    const auto bk      = device.backend();
    const auto adapter = device.adapter_name();
    std::fprintf(stdout,
                 "[hello_metal] backend=%u  adapter=%.*s\n",
                 static_cast<unsigned>(bk),
                 static_cast<int>(adapter.size()),
                 adapter.data());

    if (bk != cd::rhi::Backend::kMetal)
    {
        std::fprintf(stderr,
                     "[hello_metal] FAIL — backend is not kMetal (%u)\n",
                     static_cast<unsigned>(bk));
        return EXIT_FAILURE;
    }

    // ---- 3. Exercise create_buffer (phase531 returns stub handle) -----------
    cd::rhi::BufferDesc buf_desc {};
    buf_desc.size   = 256;
    buf_desc.memory = cd::rhi::MemoryUsage::kGpuOnly;

    auto buf_r = device.create_buffer(buf_desc);
    if (!buf_r.has_value())
    {
        std::fprintf(stderr,
                     "[hello_metal] FAIL — create_buffer: %.*s\n",
                     static_cast<int>(buf_r.error().message.size()),
                     buf_r.error().message.data());
        return EXIT_FAILURE;
    }
    std::fprintf(stdout,
                 "[hello_metal] create_buffer OK  handle.index=%u\n",
                 buf_r.value().index());
    device.destroy_buffer(buf_r.value());

    // ---- 4. Exercise create_texture (phase531 returns stub handle) ----------
    cd::rhi::TextureDesc tex_desc {};
    tex_desc.extent = { 64, 64, 1 };
    tex_desc.format = cd::rhi::Format::kRGBA8Unorm;

    auto tex_r = device.create_texture(tex_desc);
    if (!tex_r.has_value())
    {
        std::fprintf(stderr,
                     "[hello_metal] FAIL — create_texture: %.*s\n",
                     static_cast<int>(tex_r.error().message.size()),
                     tex_r.error().message.data());
        return EXIT_FAILURE;
    }
    std::fprintf(stdout,
                 "[hello_metal] create_texture OK  handle.index=%u\n",
                 tex_r.value().index());
    device.destroy_texture(tex_r.value());

    // ---- 5. wait_idle — stub path, must not crash ---------------------------
    device.wait_idle();
    std::fprintf(stdout, "[hello_metal] wait_idle OK\n");

    std::fprintf(stdout, "[hello_metal] EXIT 0 — Metal MVP skeleton alive\n");
    return EXIT_SUCCESS;
}
