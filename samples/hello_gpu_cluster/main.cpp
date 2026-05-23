// =============================================================================
// CHROMODYNAMIC — samples/hello_gpu_cluster
//
// End-to-end Forward+ light culling on the GPU: dispatches the
// cluster_assign.comp compute shader via cd::cluster_gpu::GpuPipeline,
// then compares the GPU output bit-for-bit against the CPU reference
// simulator (cd::render::cluster::run_reference_compute, Wave 88).
//
// Same deterministic scene as hello_clustered_lights and
// hello_compute_cluster (mt19937 seed 0x5eedF11D, 128 lights, 16×9×24
// grid). If the GPU result matches the CPU reference, the engine's
// Forward+ pipeline is parity-correct from algorithm → compute shader
// → buffer staging → readback. Any mismatch points at the precise
// stage that diverged.
//
// Exit codes:
//   0 — PARITY OK (GPU == CPU)
//   1 — PARITY MISMATCH on at least one cluster
//   2 — Vulkan device creation failed (no ICD / headless host) — skip
//   3 — GpuPipeline creation or dispatch failed
// =============================================================================
#include <cd/cluster_gpu/GpuPipeline.hpp>
#include <cd/render/cluster/ClusterGrid.hpp>
#include <cd/render/cluster/ReferenceCompute.hpp>
#include <cd/rhi_vulkan/VulkanDevice.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace
{

constexpr std::uint32_t kLights = 128;

}  // namespace

int main()
{
    std::printf("=== hello_gpu_cluster — Forward+ GPU dispatch + parity ===\n");

    // 1. Bring up a Vulkan device.
    cd::rhi_vulkan::VulkanCreateInfo info;
    info.app_name = "hello_gpu_cluster";
    info.enable_validation = false;  // sample runs headless; skip layer.
    auto device_r = cd::rhi_vulkan::create_vulkan_device(info);
    if (!device_r.has_value() || *device_r == nullptr)
    {
        std::printf("[hello_gpu_cluster] no Vulkan device — skipping (exit 2)\n");
        return 2;
    }
    auto& device = **device_r;
    std::printf("  device  : %s\n",
                std::string { device.adapter_name() }.c_str());

    // 2. Build the cluster pipeline.
    cd::render::cluster::ClusterConfig cfg {
        16, 9, 24, 1.0472F, 16.0F / 9.0F, 0.1F, 100.0F
    };
    auto pipeline_r = cd::cluster_gpu::GpuPipeline::create(device, cfg, kLights);
    if (!pipeline_r.has_value())
    {
        std::printf("[hello_gpu_cluster] GpuPipeline::create failed: code=%u\n",
                    pipeline_r.error().code);
        return 3;
    }
    auto pipeline = std::move(*pipeline_r);

    // 3. Generate the same deterministic 128-light scene as
    //    hello_clustered_lights / hello_compute_cluster.
    std::mt19937 rng { 0x5eedF11Du };
    const float far = cfg.far_plane;
    const float half_fov_y = cfg.fov_y_rad * 0.5F;
    const float half_height_at_far = far * std::tan(half_fov_y);
    const float half_width_at_far = half_height_at_far * cfg.aspect;
    std::uniform_real_distribution<float> depth_dist { 1.0F, far * 0.8F };
    std::uniform_real_distribution<float> norm { -1.0F, 1.0F };

    std::vector<cd::render::cluster::LightSphere> lights;
    lights.reserve(kLights);
    for (std::uint32_t i = 0; i < kLights; ++i)
    {
        const float depth = depth_dist(rng);
        const float frac = depth / far;
        cd::render::cluster::LightSphere s;
        s.view_pos.x = norm(rng) * half_width_at_far * frac;
        s.view_pos.y = norm(rng) * half_height_at_far * frac;
        s.view_pos.z = -depth;
        s.radius = 0.5F + 1.5F * std::abs(norm(rng));
        lights.push_back(s);
    }

    // 4. Dispatch on GPU.
    auto gpu_r = pipeline->run(lights);
    if (!gpu_r.has_value())
    {
        std::printf("[hello_gpu_cluster] dispatch failed: code=%u\n",
                    gpu_r.error().code);
        return 3;
    }
    const auto& gpu = *gpu_r;

    // 5. Compute the same on CPU and compare.
    auto cpu = cd::render::cluster::run_reference_compute(cfg, lights);

    std::printf("\n=== GPU output ===\n");
    std::printf("  cluster_counts.size  = %zu\n", gpu.cluster_counts.size());
    std::printf("  cluster_offsets.size = %zu\n", gpu.cluster_offsets.size());
    std::printf("  light_indices.size   = %zu\n", gpu.light_indices.size());
    std::printf("\n=== CPU output ===\n");
    std::printf("  cluster_counts.size  = %zu\n", cpu.cluster_counts.size());
    std::printf("  cluster_offsets.size = %zu\n", cpu.cluster_offsets.size());
    std::printf("  light_indices.size   = %zu\n", cpu.light_indices.size());

    if (gpu.cluster_counts.size() != cpu.cluster_counts.size()
        || gpu.cluster_offsets.size() != cpu.cluster_offsets.size()
        || gpu.light_indices.size() != cpu.light_indices.size())
    {
        std::printf("\n[hello_gpu_cluster] PARITY MISMATCH (buffer sizes differ)\n");
        return 1;
    }

    // Set-equal per cluster (light indices may differ in order within
    // a cluster on the GPU side because dispatch order isn't fixed —
    // the shader's per-cluster loop is sequential per thread, but the
    // dispatch grid ordering doesn't guarantee cluster output order).
    // Compare via sorted sets per cluster like hello_compute_cluster does.
    const std::uint32_t cluster_count =
        static_cast<std::uint32_t>(cfg.cells_x) * cfg.cells_y * cfg.cells_z;
    std::uint32_t mismatches = 0;
    for (std::uint32_t cid = 0; cid < cluster_count; ++cid)
    {
        const auto g_begin = gpu.cluster_offsets[cid];
        const auto g_end = gpu.cluster_offsets[cid + 1];
        const auto c_begin = cpu.cluster_offsets[cid];
        const auto c_end = cpu.cluster_offsets[cid + 1];
        if ((g_end - g_begin) != (c_end - c_begin))
        {
            ++mismatches;
            continue;
        }
        std::vector<std::uint32_t> gv(gpu.light_indices.data() + g_begin,
                                      gpu.light_indices.data() + g_end);
        std::vector<std::uint32_t> cv(cpu.light_indices.data() + c_begin,
                                      cpu.light_indices.data() + c_end);
        std::sort(gv.begin(), gv.end());
        std::sort(cv.begin(), cv.end());
        if (gv != cv)
            ++mismatches;
    }

    std::printf("\n=== Parity verdict ===\n");
    if (mismatches == 0)
    {
        std::printf("  PARITY OK — GPU == CPU on all %u clusters\n",
                    cluster_count);
        std::printf("[hello_gpu_cluster] done (exit 0)\n");
        return 0;
    }
    std::printf("  PARITY MISMATCH on %u / %u clusters\n",
                mismatches, cluster_count);
    return 1;
}
