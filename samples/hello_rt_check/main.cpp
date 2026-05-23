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

    // Probe the API surface. v0.40.0 ships the shape with every backend
    // returning kNotImplemented; this is the marathon-honest contract
    // until the SBT + pipeline wave lands.
    cd::rhi::AccelStructureDesc as_desc {};
    as_desc.kind = cd::rhi::AccelStructureKind::kBottomLevel;
    auto as_r = device.create_acceleration_structure(as_desc);
    if (as_r.has_value())
    {
        std::fprintf(stdout, "[rt] create_acceleration_structure: OK (handle valid)\n");
        device.destroy_acceleration_structure(*as_r);
    }
    else
    {
        std::fprintf(stdout, "[rt] create_acceleration_structure: %.*s\n",
                     static_cast<int>(as_r.error().message.size()),
                     as_r.error().message.data());
    }

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
                 "[rt] API surface present; backend implementation queued for"
                 " a follow-up wave\n");
    return 0;
}
