// =============================================================================
// CHROMODYNAMIC — samples/render/hello_ddgi/main.cpp
// phase565 — Console-only DDGI dispatch demo.
//
// Goal: prove cd::ddgi is usable from an external consumer without pulling in
// hello_engine or any sample infrastructure. No window, no swapchain — just
// a headless Vulkan device, one DispatchPass::execute() (trace), then
// DispatchPass::execute_blend_irradiance() and
// DispatchPass::execute_blend_visibility(), and a printed summary.
//
// Expected output (Vulkan ICD present, glslang backend enabled):
//   [hello_ddgi] probes : 256 (8 x 4 x 8)
//   [hello_ddgi] rays/probe: 64
//   [hello_ddgi] trace       : dispatched OK
//   [hello_ddgi] blend_irr   : dispatched OK
//   [hello_ddgi] blend_vis   : dispatched OK
//   [hello_ddgi] DONE
//
// Expected output (no Vulkan ICD / no glslang — CI headless):
//   [hello_ddgi] no Vulkan device available — skipping GPU dispatch
//   [hello_ddgi] DONE (headless)
// =============================================================================

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#endif

#include <cd/core/Result.hpp>
#include <cd/ddgi/Ddgi.hpp>
#include <cd/ddgi/DispatchPass.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> try_make_device()
{
    cd::rhi::vulkan::VulkanCreateInfo info {};
    info.app_name         = "hello_ddgi";
    info.enable_validation = true;
    auto r = cd::rhi::vulkan::create_vulkan_device(info);
    if (!r.has_value())
        return nullptr;
    return std::move(*r);
}

/// Transition `handles` from `from` to kUnorderedAccess in a single barrier.
template <std::size_t N>
void transition_to_uav(cd::rhi::ICommandBuffer&                      cmd,
                       const std::array<cd::rhi::TextureHandle, N>& handles,
                       cd::rhi::ResourceState                         from)
{
    std::array<cd::rhi::TextureBarrier, N> barriers {};
    for (std::size_t i = 0; i < N; ++i)
    {
        barriers[i].texture = handles[i];
        barriers[i].from    = from;
        barriers[i].to      = cd::rhi::ResourceState::kUnorderedAccess;
        barriers[i].range   = { 0U, 1U, 0U, 1U };
    }
    cmd.barrier({}, barriers);
}

/// Issue a UAV → UAV memory barrier for a pair of ray images to flush L1
/// between the trace pass and the blend passes.
void memory_barrier_ray_images(cd::rhi::ICommandBuffer&  cmd,
                               cd::rhi::TextureHandle     ray_radiance,
                               cd::rhi::TextureHandle     ray_dir_dist)
{
    const std::array<cd::rhi::TextureBarrier, 2> barriers {
        cd::rhi::TextureBarrier {
            .texture = ray_radiance,
            .from    = cd::rhi::ResourceState::kUnorderedAccess,
            .to      = cd::rhi::ResourceState::kUnorderedAccess,
            .range   = { 0U, 1U, 0U, 1U },
        },
        cd::rhi::TextureBarrier {
            .texture = ray_dir_dist,
            .from    = cd::rhi::ResourceState::kUnorderedAccess,
            .to      = cd::rhi::ResourceState::kUnorderedAccess,
            .range   = { 0U, 1U, 0U, 1U },
        },
    };
    cmd.barrier({}, barriers);
}

}  // namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    // ------------------------------------------------------------------
    // 1. Probe grid — 8 x 4 x 8 = 256 probes (Majercik 2019 starter).
    // ------------------------------------------------------------------
    cd::ddgi::DispatchPassDesc desc {};
    desc.grid.probes_x            = 8;
    desc.grid.probes_y            = 4;
    desc.grid.probes_z            = 8;
    desc.grid.origin              = { -4.0F, 0.0F, -4.0F };
    desc.grid.spacing             = { 1.0F, 1.0F, 1.0F };
    desc.settings.rays_per_probe  = 64;
    desc.settings.hysteresis      = 0.97F;
    desc.settings.max_distance    = 20.0F;
    desc.sky_color[0]             = 0.2F;
    desc.sky_color[1]             = 0.25F;
    desc.sky_color[2]             = 0.4F;
    desc.needs_tlas               = false;   // smoke variant — no ray-query required
    desc.probe_face_size          = 8;

    const std::uint32_t probe_count = desc.grid.probe_count();
    const std::uint32_t rays_count  = desc.settings.rays_per_probe;

    std::printf("[hello_ddgi] probes    : %u (%u x %u x %u)\n",
                probe_count,
                desc.grid.probes_x,
                desc.grid.probes_y,
                desc.grid.probes_z);
    std::printf("[hello_ddgi] rays/probe: %u\n", rays_count);

    // ------------------------------------------------------------------
    // 2. Create Vulkan device (headless — no surface / swapchain).
    // ------------------------------------------------------------------
    auto dev = try_make_device();
    if (!dev)
    {
        std::printf("[hello_ddgi] no Vulkan device available — skipping GPU dispatch\n");
        std::printf("[hello_ddgi] DONE (headless)\n");
        return 0;
    }

    // ------------------------------------------------------------------
    // 3. Initialise DispatchPass — compiles the three compute shaders,
    //    allocates the four storage images (ray_radiance, ray_dir_dist,
    //    irradiance_atlas, visibility_atlas), and builds the descriptor sets.
    // ------------------------------------------------------------------
    cd::ddgi::DispatchPass pass;
    {
        auto r = pass.init(*dev, desc);
        if (!r.has_value())
        {
            const auto& msg = r.error().message;
            std::printf("[hello_ddgi] DispatchPass::init failed: %.*s\n",
                        static_cast<int>(msg.size()), msg.data());
            std::printf("[hello_ddgi] DONE (init-failed)\n");
            return 1;
        }
    }

    // ------------------------------------------------------------------
    // 4. Record one command buffer: transition images, trace, sync, blend x2.
    //    All three dispatches share a single command buffer + submit to keep
    //    the sample minimal.
    // ------------------------------------------------------------------
    auto cmd = dev->create_command_buffer(cd::rhi::QueueType::kCompute);
    if (!cmd)
    {
        std::printf("[hello_ddgi] create_command_buffer failed\n");
        pass.shutdown(*dev);
        return 1;
    }

    cmd->begin();

    // Transition all four storage images UNDEFINED → kUnorderedAccess.
    const std::array<cd::rhi::TextureHandle, 4> all_images {
        pass.ray_radiance(),
        pass.ray_dir_dist(),
        pass.irradiance_atlas(),
        pass.visibility_atlas(),
    };
    transition_to_uav(*cmd, all_images, cd::rhi::ResourceState::kUndefined);

    // -- Trace pass: kDdgiTraceCS (smoke variant, no ray-query) --
    pass.dispatch(*cmd, /*frame_index=*/0U);

    // Memory barrier: flush trace writes before blend reads.
    memory_barrier_ray_images(*cmd, pass.ray_radiance(), pass.ray_dir_dist());

    // -- Blend irradiance: kDdgiBlendIrradianceCS --
    pass.execute_blend_irradiance(*cmd, /*frame_index=*/0U);

    // Memory barrier between the two blend passes (they share ray_dir_dist).
    memory_barrier_ray_images(*cmd, pass.ray_radiance(), pass.ray_dir_dist());

    // -- Blend visibility: kDdgiBlendVisibilityCS --
    pass.execute_blend_visibility(*cmd, /*frame_index=*/0U);

    cmd->end();

    // ------------------------------------------------------------------
    // 5. Submit + wait for the GPU to finish.
    // ------------------------------------------------------------------
    dev->submit(*cmd);
    dev->wait_idle();

    // ------------------------------------------------------------------
    // 6. Report results.
    //    Ray hits are not readback (no staging buffer in this minimal demo);
    //    the smoke shader writes sky-colour radiance and -1 distance for
    //    every "miss" ray, so we report the total ray count dispatched.
    // ------------------------------------------------------------------
    const std::uint32_t total_rays = probe_count * rays_count;

    std::printf("[hello_ddgi] trace       : dispatched OK  (%u probes x %u rays = %u total, all reported as miss/sky)\n",
                probe_count, rays_count, total_rays);
    std::printf("[hello_ddgi] blend_irr   : dispatched OK\n");
    std::printf("[hello_ddgi] blend_vis   : dispatched OK\n");

    // ------------------------------------------------------------------
    // 7. Cleanup.
    // ------------------------------------------------------------------
    pass.shutdown(*dev);

    std::printf("[hello_ddgi] DONE\n");
    return 0;
}
