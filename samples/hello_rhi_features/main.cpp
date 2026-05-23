// =============================================================================
// CHROMODYNAMIC — samples/hello_rhi_features
//
// v0.29.0 / Phase 12.D — capability detection sample.
//
// Creates a Vulkan device against the host's adapter, enumerates the
// device extensions, and prints which advanced capabilities the
// engine's RHI exposes for callers. Ships the ray-tracing /
// mesh-shader feature bits that Phase 12.D wired into
// cd::rhi::DeviceFeatures; the actual RT pipeline + mesh shader
// dispatch_rays / draw_mesh_tasks surface lands in Phase 13.
//
// Use this sample to answer the practical question "can I use RT on
// this machine?" before writing RT-dependent code that would
// otherwise fail at runtime.
//
// CD_VULKAN_DEVICE_INDEX env var picks the adapter when multiple are
// visible (same plumbing the golden harness uses for cross-vendor
// validation).
// =============================================================================
#include <cd/core/Version.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi_vulkan/VulkanDevice.hpp>

#include <cstdio>

namespace
{

void print_feature(const char* name, bool on)
{
    std::fprintf(stdout, "  %-26s %s\n", name, on ? "yes" : "no");
}

}  // namespace

int main()
{
    std::fprintf(stdout,
                 "CHROMODYNAMIC %u.%u.%u — hello_rhi_features\n",
                 static_cast<unsigned>(cd::core::kEngineVersion.major),
                 static_cast<unsigned>(cd::core::kEngineVersion.minor),
                 static_cast<unsigned>(cd::core::kEngineVersion.patch));

    cd::rhi_vulkan::VulkanCreateInfo vci {};
    vci.enable_validation = false;
    auto dev_r = cd::rhi_vulkan::create_vulkan_device(vci);
    if (!dev_r.has_value())
    {
        std::fprintf(stderr,
                     "[rhi] device create failed: %.*s\n",
                     static_cast<int>(dev_r.error().message.size()),
                     dev_r.error().message.data());
        return 1;
    }
    auto& device = **dev_r;

    const auto adapter = device.adapter_name();
    std::fprintf(stdout,
                 "adapter : %.*s\n",
                 static_cast<int>(adapter.size()),
                 adapter.data());

    const auto& f = device.features();
    std::fprintf(stdout, "features:\n");
    print_feature("ray_tracing",         f.ray_tracing);
    print_feature("ray_query",           f.ray_query);
    print_feature("mesh_shader",         f.mesh_shader);
    print_feature("variable_rate_shading", f.variable_rate_shading);
    print_feature("bindless_resources",  f.bindless_resources);
    print_feature("timestamp_queries",   f.timestamp_queries);
    print_feature("pipeline_stats_queries", f.pipeline_statistics_queries);
    print_feature("tessellation_shader", f.tessellation_shader);
    print_feature("geometry_shader",     f.geometry_shader);
    print_feature("sampler_anisotropy",  f.sampler_anisotropy);
    print_feature("depth_clamp",         f.depth_clamp);
    print_feature("dual_source_blend",   f.dual_source_blend);

    const auto& lim = device.limits();
    std::fprintf(stdout,
                 "limits  : tex2d=%u  pushconst=%u  uniform_range=%u\n",
                 lim.max_texture_dimension_2d,
                 lim.max_push_constants_size,
                 lim.max_uniform_buffer_range);

    std::fprintf(stdout, "[rhi_features] OK\n");
    return 0;
}
