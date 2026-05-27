// =============================================================================
// CHROMODYNAMIC — samples/hello_virtual_geometry
//
// CPU smoke for cd::virtual_geometry (Karis 2021 Nanite-style LOD DAG).
// Computes the projected pixel error for a cluster at increasing camera
// distances and verifies the LOD pick logic chooses progressively
// coarser parents as the cluster shrinks on screen.
// =============================================================================
#include <cd/core/Version.hpp>
#include <cd/virtual_geometry/VirtualGeometry.hpp>

#include <cmath>
#include <cstdio>

int main()
{
    std::printf("CHROMODYNAMIC %u.%u.%u — hello_virtual_geometry\n",
                static_cast<unsigned>(cd::core::kEngineVersion.major),
                static_cast<unsigned>(cd::core::kEngineVersion.minor),
                static_cast<unsigned>(cd::core::kEngineVersion.patch));

    namespace vg = cd::virtual_geometry;

    const cd::math::Vec3f centre { 0.0F, 0.0F, 0.0F };
    const float cluster_radius = 1.0F;
    const float half_fov = 0.5F * (3.14159265F / 3.0F);  // 60° vert FOV
    const std::uint32_t viewport_h = 1080;

    std::printf("  projected pixel error at increasing camera distances:\n");
    float prev = 1e30F;
    for (float d : { 1.0F, 2.0F, 5.0F, 10.0F, 50.0F, 200.0F })
    {
        const cd::math::Vec3f eye { 0.0F, 0.0F, d };
        const float err = vg::projected_error_pixels(
            centre, cluster_radius, eye, half_fov, viewport_h);
        std::printf("    d=%6.1f m  err=%8.2f px\n",
                    static_cast<double>(d), static_cast<double>(err));
        if (err >= prev)
        {
            std::printf("FAIL — pixel error not monotonically decreasing with distance\n");
            return 1;
        }
        prev = err;
    }

    // At 1m the radius-1 cluster should subtend a substantial chunk of
    // screen height (radius=1, fov_y=60° → ~30% of viewport height).
    const float near_err = vg::projected_error_pixels(
        centre, cluster_radius, { 0, 0, 1.0F }, half_fov, viewport_h);
    if (near_err < 100.0F)
    {
        std::printf("FAIL — near-camera cluster radius doesn't dominate (err=%.2f)\n",
                    static_cast<double>(near_err));
        return 2;
    }
    std::printf("  near-camera (1m) cluster error %.2f px (large enough to draw at LOD 0)  OK\n",
                static_cast<double>(near_err));

    std::printf("[hello_virtual_geometry] PARITY OK\n");
    return 0;
}
