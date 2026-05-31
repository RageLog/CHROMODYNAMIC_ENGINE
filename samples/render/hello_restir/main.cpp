// =============================================================================
// CHROMODYNAMIC — samples/render/hello_restir
//
// Console-only ReSTIR DI dispatch demo (Phase 566).
//
// Creates a Vulkan device, allocates a small reservoir buffer, and runs the
// three-stage ReSTIR DI pipeline in sequence:
//   1. execute_sample  (DispatchPass::record)       — initial candidate WRS
//   2. execute_temporal_reuse                        — temporal blending
//   3. execute_spatial_reuse                         — 5-tap disc kernel
//
// Prints "dispatched OK" on success. Gracefully skips with exit 2 when no
// Vulkan ICD is present (headless CI machines stay green).
//
// Exit codes:
//   0 — dispatched OK (all three passes submitted + wait_idle returned)
//   2 — no Vulkan device available — skip
//   3 — prepare() or command-buffer creation failed
// =============================================================================
#include <cd/restir_di/DispatchPass.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>

#include <cstdint>
#include <cstdio>

int main()
{
    std::printf("=== hello_restir — ReSTIR DI dispatch demo ===\n");

    // 1. Bring up a Vulkan device.
    cd::rhi::vulkan::VulkanCreateInfo info {};
    info.app_name          = "hello_restir";
    info.enable_validation = false;  // sample runs headless; skip layer overhead.
    auto device_r = cd::rhi::vulkan::create_vulkan_device(info);
    if (!device_r.has_value() || *device_r == nullptr)
    {
        std::printf("[hello_restir] no Vulkan device available — skipping (exit 2)\n");
        return 2;
    }
    auto& device = **device_r;
    std::printf("  device : %s\n",
                std::string { device.adapter_name() }.c_str());

    // 2. Configure a small viewport (128x96 = 12 288 reservoirs).
    cd::restir_di::DispatchConfig cfg {};
    cfg.viewport_width  = 128U;
    cfg.viewport_height = 96U;
    cfg.light_count     = 8U;
    cfg.candidates      = 32U;

    std::printf("  viewport : %u x %u  lights : %u  candidates : %u\n",
                cfg.viewport_width, cfg.viewport_height,
                cfg.light_count, cfg.candidates);
    std::printf("  reservoir buffer : %llu bytes\n",
                static_cast<unsigned long long>(
                    cd::restir_di::DispatchPass::reservoir_buffer_size(
                        cfg.viewport_width, cfg.viewport_height)));

    // 3. Prepare the pass (compile GLSL -> SPIR-V, create pipelines,
    //    allocate all four reservoir SSBOs).
    cd::restir_di::DispatchPass pass;
    auto prepare_r = pass.prepare(device, cfg);
    if (!prepare_r.has_value())
    {
        std::printf("[hello_restir] prepare() failed: %.*s\n",
                    static_cast<int>(prepare_r.error().message.size()),
                    prepare_r.error().message.data());
        return 3;
    }
    std::printf("  is_ready          : %s\n", pass.is_ready() ? "true" : "false");
    std::printf("  reservoir_buffer  valid : %s\n",
                pass.reservoir_buffer().is_valid()          ? "yes" : "no");
    std::printf("  previous_buf      valid : %s\n",
                pass.previous_reservoir_buffer().is_valid() ? "yes" : "no");
    std::printf("  temporal_buf      valid : %s\n",
                pass.temporal_reservoir_buffer().is_valid() ? "yes" : "no");
    std::printf("  spatial_buf       valid : %s\n",
                pass.spatial_reservoir_buffer().is_valid()  ? "yes" : "no");

    // 4. Allocate a compute command buffer and record the full three-pass
    //    chain: sample -> temporal_reuse -> spatial_reuse.
    auto cb = device.create_command_buffer(cd::rhi::QueueType::kCompute);
    if (!cb)
    {
        std::printf("[hello_restir] create_command_buffer() returned null\n");
        return 3;
    }

    constexpr std::uint32_t kFrameIndex = 0U;

    cb->begin();
    // Pass 1: initial candidate WRS — writes reservoir_buffer().
    pass.record(*cb);
    // Pass 2: temporal reuse — blends reservoir_buffer() + previous_reservoir_buffer()
    //         into temporal_reservoir_buffer().
    pass.execute_temporal_reuse(*cb, kFrameIndex);
    // Pass 3: spatial reuse (5-tap disc) — reads temporal_reservoir_buffer(),
    //         writes spatial_reservoir_buffer().
    pass.execute_spatial_reuse(*cb, kFrameIndex);
    cb->end();

    // 5. Submit and synchronise. A clean wait_idle() means the driver /
    //    validation layer found no descriptor / push-constant / layout issues.
    device.submit(*cb);
    device.wait_idle();

    std::printf("\ndispatched OK\n");
    std::printf("[hello_restir] done (exit 0)\n");
    return 0;
}
