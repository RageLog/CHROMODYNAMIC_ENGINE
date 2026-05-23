// =============================================================================
// CHROMODYNAMIC — samples/hello_clustered_lights
//
// Demos cd::render::cluster::ClusterGrid by:
//   * Creating a 16×9×24 cluster grid over a typical 60° / 16:9 frustum.
//   * Scattering 128 point lights randomly inside the frustum.
//   * Assigning every light, finalising, then reporting:
//      - total assignments (how many (cluster, light) pairs the build
//        produced — a measure of average light spread)
//      - average lights-per-cluster on populated clusters
//      - histogram of per-cluster light counts
//      - max-light-count cluster's (x, y, z) coordinates and the
//        light indices it holds
//
// All-CPU; no GPU dep. The same data layout (cluster_offsets +
// light_indices) is what the Phase 7+ compute shader port will upload
// to a Vulkan storage buffer.
// =============================================================================
#include <cd/render/cluster/ClusterGrid.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace
{

constexpr std::uint32_t kLights = 128;
constexpr std::uint32_t kHistBuckets = 8;

}  // namespace

int main()
{
    std::printf("=== hello_clustered_lights — Forward+ CPU baseline demo ===\n");

    cd::render::cluster::ClusterConfig cfg {
        16, 9, 24,         // X × Y × Z
        1.0472F,           // 60° vertical FOV
        16.0F / 9.0F,      // aspect
        0.1F, 100.0F       // near / far
    };
    cd::render::cluster::ClusterGrid grid { cfg };

    std::printf("  config: %u×%u×%u clusters = %zu total\n",
                cfg.cells_x, cfg.cells_y, cfg.cells_z,
                grid.total_cluster_count());
    std::printf("  fov_y = %.2f rad  aspect = %.2f  near/far = %.2f / %.2f m\n",
                static_cast<double>(cfg.fov_y_rad),
                static_cast<double>(cfg.aspect),
                static_cast<double>(cfg.near_plane),
                static_cast<double>(cfg.far_plane));

    // Scatter 128 lights randomly within the frustum.
    // Random but seeded → deterministic across runs.
    std::mt19937 rng { 0x5eedF11Du };
    const float far = cfg.far_plane;
    const float half_fov_y = cfg.fov_y_rad * 0.5F;
    const float half_height_at_far = far * std::tan(half_fov_y);
    const float half_width_at_far = half_height_at_far * cfg.aspect;
    std::uniform_real_distribution<float> depth_dist { 1.0F, far * 0.8F };
    std::uniform_real_distribution<float> norm { -1.0F, 1.0F };

    for (std::uint32_t i = 0; i < kLights; ++i)
    {
        const float depth = depth_dist(rng);
        const float frac = depth / far;
        const float x_extent = half_width_at_far * frac;
        const float y_extent = half_height_at_far * frac;
        cd::render::cluster::LightSphere s;
        s.view_pos.x = norm(rng) * x_extent;
        s.view_pos.y = norm(rng) * y_extent;
        s.view_pos.z = -depth;
        s.radius = 0.5F + 1.5F * std::abs(norm(rng));  // 0.5..2.0 m
        grid.assign_light(i, s);
    }
    grid.finalize();

    const auto total_assignments = grid.total_light_assignments();
    std::printf("\n=== Assignment Summary ===\n");
    std::printf("  lights              = %u\n", kLights);
    std::printf("  total assignments   = %zu\n", total_assignments);
    std::printf("  avg light-spread    = %.2f clusters/light\n",
                static_cast<double>(total_assignments) / kLights);

    // Per-cluster light-count histogram + max-cluster tracking.
    std::vector<std::uint32_t> hist(kHistBuckets, 0);
    std::uint32_t max_count = 0;
    std::uint32_t max_x = 0, max_y = 0, max_z = 0;
    std::uint32_t populated = 0;
    std::size_t sum_populated_counts = 0;
    for (std::uint32_t z = 0; z < cfg.cells_z; ++z)
        for (std::uint32_t y = 0; y < cfg.cells_y; ++y)
            for (std::uint32_t x = 0; x < cfg.cells_x; ++x)
            {
                const auto n = static_cast<std::uint32_t>(
                    grid.lights_in_cluster(x, y, z).size());
                if (n > 0)
                {
                    ++populated;
                    sum_populated_counts += n;
                }
                if (n > max_count)
                {
                    max_count = n;
                    max_x = x;
                    max_y = y;
                    max_z = z;
                }
                const auto bucket = std::min<std::uint32_t>(n, kHistBuckets - 1U);
                ++hist[bucket];
            }

    std::printf("  clusters populated  = %u / %zu (%.1f%%)\n",
                populated, grid.total_cluster_count(),
                100.0 * static_cast<double>(populated)
                  / static_cast<double>(grid.total_cluster_count()));
    std::printf("  avg lights / populated cluster = %.2f\n",
                populated > 0
                  ? static_cast<double>(sum_populated_counts)
                      / static_cast<double>(populated)
                  : 0.0);

    std::printf("\n=== Per-cluster light-count histogram ===\n");
    for (std::uint32_t b = 0; b < kHistBuckets; ++b)
    {
        const char* label = (b + 1U == kHistBuckets) ? ">=" : "==";
        std::printf("  %s %u : %u clusters\n", label, b, hist[b]);
    }

    std::printf("\n=== Hottest cluster ===\n");
    std::printf("  (x, y, z) = (%u, %u, %u) with %u lights\n",
                max_x, max_y, max_z, max_count);
    const auto hottest = grid.lights_in_cluster(max_x, max_y, max_z);
    std::printf("  light indices: ");
    for (std::size_t i = 0; i < std::min<std::size_t>(hottest.size(), 16U); ++i)
        std::printf("%u ", hottest[i]);
    if (hottest.size() > 16)
        std::printf("... (+%zu more)", hottest.size() - 16);
    std::printf("\n");

    std::printf("[hello_clustered_lights] done\n");
    return 0;
}
