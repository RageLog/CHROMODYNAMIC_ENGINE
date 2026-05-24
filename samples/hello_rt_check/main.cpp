// =============================================================================
// CHROMODYNAMIC — samples/hello_rt_check
//
// v0.40.0 / Phase 14.G — ray-tracing API-shape probe.
//
// Phase 14.G lands the *interface* for ray tracing:
//   - cd::rhi::AccelStructureHandle (+ Tag)
//   - cd::rhi::AccelStructureDesc / AccelTriangleGeometry
//   - cd::rhi::DispatchRaysDesc
//   - IDevice::create_acceleration_structure / destroy_*
//   - ICommandBuffer::build_acceleration_structure / dispatch_rays
//
// Backend implementations (Vulkan VK_KHR_ray_tracing_pipeline +
// VK_KHR_acceleration_structure, D3D12 DXR Tier 1.1) land in a
// follow-up wave alongside the shader-binding-table surface.
//
// This sample:
//   1. Boots the Vulkan device.
//   2. Reads `features().ray_tracing` + `ray_query` + `mesh_shader`.
//   3. Attempts `create_acceleration_structure` to verify the API
//      surface is wired (expected: kNotImplemented today on every
//      backend — this is the marathon-honest exit code).
//   4. Prints which features the adapter would expose to a future
//      backend implementation.
//
// Use this to answer "is RT *possible* on this machine?" without
// linking a shader or building a BLAS yet.
// =============================================================================
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Version.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi_vulkan/VulkanDevice.hpp>

#include <cstdio>

int main()
{
    std::fprintf(stdout,
                 "CHROMODYNAMIC %u.%u.%u — hello_rt_check\n",
                 static_cast<unsigned>(cd::core::kEngineVersion.major),
                 static_cast<unsigned>(cd::core::kEngineVersion.minor),
                 static_cast<unsigned>(cd::core::kEngineVersion.patch));

    cd::rhi_vulkan::VulkanCreateInfo vci {};
    vci.enable_validation = false;
    auto dev_r = cd::rhi_vulkan::create_vulkan_device(vci);
    if (!dev_r.has_value())
    {
        std::fprintf(stderr, "[rt] device init failed: %.*s\n",
                     static_cast<int>(dev_r.error().message.size()),
                     dev_r.error().message.data());
        return 1;
    }
    auto& device = **dev_r;

    const auto adapter = device.adapter_name();
    std::fprintf(stdout, "adapter : %.*s\n",
                 static_cast<int>(adapter.size()), adapter.data());

    const auto& f = device.features();
    std::fprintf(stdout, "features (Phase 12.D / Wave 150 detection):\n");
    std::fprintf(stdout, "  ray_tracing       : %s\n", f.ray_tracing ? "YES" : "no");
    std::fprintf(stdout, "  ray_query         : %s\n", f.ray_query   ? "YES" : "no");
    std::fprintf(stdout, "  mesh_shader       : %s\n", f.mesh_shader ? "YES" : "no");

    // Phase 128 — probe BOTH BLAS and TLAS create paths. Phase 17.A
    // landed BLAS; Phase 127 landed TLAS create. The build (and
    // dispatch_rays + pipeline) are still queued for a follow-up
    // wave; the contract here is "create-AS objects succeed if the
    // adapter exposes RT".
    {
        cd::rhi::AccelStructureDesc blas_desc {};
        blas_desc.kind = cd::rhi::AccelStructureKind::kBottomLevel;
        auto blas_r = device.create_acceleration_structure(blas_desc);
        if (blas_r.has_value())
        {
            std::fprintf(stdout, "[rt] create_acceleration_structure(BLAS): OK\n");
            device.destroy_acceleration_structure(*blas_r);
        }
        else
        {
            std::fprintf(stdout, "[rt] create_acceleration_structure(BLAS): %.*s\n",
                         static_cast<int>(blas_r.error().message.size()),
                         blas_r.error().message.data());
        }
    }
    {
        // Single identity-transform instance referencing a null BLAS.
        // The Vulkan TLAS-size query (Phase 127) doesn't deref the
        // BLAS handle — it only needs instance COUNT.
        cd::rhi::AccelInstance inst {};  // identity, mask=0xFF, valid defaults
        const cd::rhi::AccelInstance one_inst[1] = { inst };
        cd::rhi::AccelStructureDesc tlas_desc {};
        tlas_desc.kind = cd::rhi::AccelStructureKind::kTopLevel;
        tlas_desc.instances = std::span<const cd::rhi::AccelInstance>(one_inst);
        auto tlas_r = device.create_acceleration_structure(tlas_desc);
        if (tlas_r.has_value())
        {
            std::fprintf(stdout, "[rt] create_acceleration_structure(TLAS): OK\n");
            device.destroy_acceleration_structure(*tlas_r);
        }
        else
        {
            std::fprintf(stdout, "[rt] create_acceleration_structure(TLAS): %.*s\n",
                         static_cast<int>(tlas_r.error().message.size()),
                         tlas_r.error().message.data());
        }
    }

    // Phase 118 — RT pipeline + SBT interface shape probe. We don't
    // actually create a pipeline (backend still kNotImplemented);
    // just verify the descriptor types compile + have the right
    // sizes so a future backend wave can fill in CreateRayTracingPipelinesKHR.
    static_assert(sizeof(cd::rhi::AccelInstance) == 64,
                  "AccelInstance must match VkAccelerationStructureInstanceKHR layout");
    static_assert(sizeof(cd::rhi::SbtRegion) >= 32,
                  "SbtRegion should fit four 8-byte members");
    std::fprintf(stdout,
                 "[rt] RT pipeline descriptors (Phase 118 shape):\n"
                 "      RtShaderEntry sz = %zu B\n"
                 "      RtPipelineDesc sz = %zu B\n"
                 "      SbtRegion sz      = %zu B\n"
                 "      AccelInstance sz  = %zu B (must == 64)\n",
                 sizeof(cd::rhi::RtShaderEntry),
                 sizeof(cd::rhi::RtPipelineDesc),
                 sizeof(cd::rhi::SbtRegion),
                 sizeof(cd::rhi::AccelInstance));

    // Probe the command-buffer surface symmetrically.
    auto cb = device.create_command_buffer(cd::rhi::QueueType::kGraphics);
    if (cb != nullptr)
    {
        cb->begin();
        cb->build_acceleration_structure(cd::rhi::AccelStructureHandle {});
        cd::rhi::DispatchRaysDesc drd {};
        drd.width = 64;
        drd.height = 64;
        cb->dispatch_rays(drd);
        cb->end();
        std::fprintf(stdout, "[rt] command-buffer RT methods callable (no-op default)\n");
    }

    std::fprintf(stdout,
                 "[rt] API surface fully present at v0.99.54; build/dispatch"
                 " pipeline lands in a follow-up wave\n");
    return 0;
}
