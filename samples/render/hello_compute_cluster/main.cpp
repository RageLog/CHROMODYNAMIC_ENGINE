// =============================================================================
// CHROMODYNAMIC — samples/hello_compute_cluster
//
// Companion to hello_clustered_lights. Same 128-light scene, but
// driven through `cd::render::cluster::run_reference_compute` —
// the CPU simulator that mirrors `cluster_assign.comp` exactly.
//
// Output of this sample is the parity buffer the GPU compute pipeline
// (Wave 89+) is expected to produce. A cross-check against the
// ClusterGrid output prints "PARITY OK" / "PARITY MISMATCH" so a
// developer can confirm the algorithm is consistent before wiring
// the Vulkan side.
// =============================================================================
#include <cd/render/cluster/ClusterGrid.hpp>
#include <cd/render/cluster/ReferenceCompute.hpp>

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
    std::printf("=== hello_compute_cluster — GPU compute parity demo ===\n");

    cd::render::cluster::ClusterConfig cfg {
        16, 9, 24,
        1.0472F, 16.0F / 9.0F,
        0.1F, 100.0F
    };

    // Same deterministic scatter as hello_clustered_lights.
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

    // Drive both paths.
    cd::render::cluster::ClusterGrid grid { cfg };
    for (std::uint32_t i = 0; i < lights.size(); ++i)
        grid.assign_light(i, lights[i]);
    grid.finalize();
    auto compute = cd::render::cluster::run_reference_compute(cfg, lights);

    std::printf("\n=== Reference compute output ===\n");
    std::printf("  cluster_counts.size   = %zu\n", compute.cluster_counts.size());
    std::printf("  cluster_offsets.size  = %zu\n", compute.cluster_offsets.size());
    std::printf("  light_indices.size    = %zu  (== total assignments)\n",
                compute.light_indices.size());
    std::printf("  ClusterGrid total     = %zu  (must match)\n",
                grid.total_light_assignments());

    // Per-cluster set-equality check (same as the parity test, in-line).
    std::uint32_t mismatches = 0;
    std::uint32_t max_count = 0;
    std::uint32_t max_x = 0, max_y = 0, max_z = 0;
    for (std::uint32_t z = 0; z < cfg.cells_z; ++z)
        for (std::uint32_t y = 0; y < cfg.cells_y; ++y)
            for (std::uint32_t x = 0; x < cfg.cells_x; ++x)
            {
                const std::size_t cid =
                    (static_cast<std::size_t>(z) * cfg.cells_y + y) * cfg.cells_x + x;
                const auto from_grid = grid.lights_in_cluster(x, y, z);
                const auto begin = compute.cluster_offsets[cid];
                const auto end = compute.cluster_offsets[cid + 1];
                std::vector<std::uint32_t> g_sorted(from_grid.begin(), from_grid.end());
                std::vector<std::uint32_t> c_sorted(
                    compute.light_indices.data() + begin,
                    compute.light_indices.data() + end);
                std::sort(g_sorted.begin(), g_sorted.end());
                std::sort(c_sorted.begin(), c_sorted.end());
                if (g_sorted != c_sorted)
                    ++mismatches;
                const auto count = end - begin;
                if (count > max_count)
                {
                    max_count = count;
                    max_x = x;
                    max_y = y;
                    max_z = z;
                }
            }

    std::printf("\n=== GPU-parity verdict ===\n");
    if (mismatches == 0)
        std::printf("  PARITY OK — ClusterGrid ↔ ReferenceCompute identical "
                    "on all %zu clusters\n", grid.total_cluster_count());
    else
        std::printf("  PARITY MISMATCH on %u / %zu clusters\n",
                    mismatches, grid.total_cluster_count());

    std::printf("\n=== Hottest cluster ===\n");
    std::printf("  (%u, %u, %u) holds %u lights\n", max_x, max_y, max_z, max_count);

    std::printf("[hello_compute_cluster] done\n");
    return mismatches == 0 ? 0 : 1;
}
