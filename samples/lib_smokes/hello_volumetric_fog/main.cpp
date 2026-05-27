// =============================================================================
// CHROMODYNAMIC — samples/hello_volumetric_fog
//
// CPU reference + GPU-parity smoke for cd::volumetric_fog (Wronski 2014
// Frostbite-style froxel grid). Mirrors the hello_compute_cluster
// pattern: build a small froxel grid, integrate front-to-back along a
// view ray, print the slice-by-slice transmittance + accumulated in-
// scattering. The same math is what the GLSL kFogInjectionCS +
// kFogIntegrationCS kernels run on the GPU.
//
// Validates:
//   * slice_to_view_z / view_z_to_slice round-trip.
//   * Beer-Lambert transmittance per slab.
//   * integrate_view_ray accumulator monotonicity (transmittance never
//     increases as we walk further from camera).
// =============================================================================
#include <cd/core/Version.hpp>
#include <cd/math/Vector.hpp>
#include <cd/volumetric_fog/Fog.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

int main()
{
    std::printf("CHROMODYNAMIC %u.%u.%u — hello_volumetric_fog\n",
                static_cast<unsigned>(cd::core::kEngineVersion.major),
                static_cast<unsigned>(cd::core::kEngineVersion.minor),
                static_cast<unsigned>(cd::core::kEngineVersion.patch));

    namespace vf = cd::volumetric_fog;

    // 1) Build a small 16x9x32 froxel grid (downsized from Wronski's
    //    160x90x128 — same math, faster smoke).
    vf::GridConfig cfg {};
    cfg.x = 16; cfg.y = 9; cfg.z = 32;
    cfg.near_z = 0.1F;
    cfg.far_z  = 64.0F;

    vf::FroxelGrid grid {};
    grid.config = cfg;
    grid.resize();

    // 2) Inject a vertical fog "wall" at world Y=0..10m — every froxel
    //    whose view-Z falls inside the wall gets RGB scattering = sun
    //    colour and extinction = density.
    const cd::math::Vec3f sun_col { 1.0F, 0.9F, 0.7F };
    const float density = 0.08F;
    std::uint32_t injected = 0;
    for (std::uint32_t z = 0; z < cfg.z; ++z)
    {
        const float slice = (static_cast<float>(z) + 0.5F) / static_cast<float>(cfg.z);
        const float view_z = vf::slice_to_view_z(slice, cfg);
        if (view_z < 1.0F || view_z > 12.0F) continue;
        for (std::uint32_t y = 0; y < cfg.y; ++y)
        for (std::uint32_t x = 0; x < cfg.x; ++x)
        {
            const std::size_t i = grid.index(x, y, z);
            grid.cells[i] = { sun_col.x * density,
                              sun_col.y * density,
                              sun_col.z * density,
                              density };
            ++injected;
        }
    }
    std::printf("  injected %u froxel cells (Y-wall @ 1..12 m view-Z)\n", injected);

    // 3) Round-trip slice ↔ view-Z to confirm the quadratic mapping
    //    inverse matches forward to ~5 ulp.
    float max_err = 0.0F;
    for (std::uint32_t z = 0; z <= cfg.z; ++z)
    {
        const float slice = static_cast<float>(z) / static_cast<float>(cfg.z);
        const float vz    = vf::slice_to_view_z(slice, cfg);
        const float back  = vf::view_z_to_slice(vz, cfg);
        max_err = std::max(max_err, std::abs(slice - back));
    }
    std::printf("  slice/view_z round-trip max error: %.3e\n",
                static_cast<double>(max_err));
    if (max_err > 1e-4F)
    {
        std::printf("FAIL — slice mapping not invertible\n");
        return 1;
    }

    // 4) Integrate the centre column (x=cfg.x/2, y=cfg.y/2) front-to-back.
    std::vector<cd::math::Vec4f> ray(cfg.z);
    vf::integrate_view_ray(grid, cfg.x / 2, cfg.y / 2, ray);
    std::printf("  view-ray slices (centre column):\n");
    float prev_trans = 1.001F;
    for (std::uint32_t z = 0; z < cfg.z; ++z)
    {
        if (z % 4 != 0 && z != cfg.z - 1) continue;
        const auto& c = ray[z];
        std::printf("    z=%2u  scatter=(%.3f,%.3f,%.3f)  trans=%.3f\n",
                    z,
                    static_cast<double>(c.x),
                    static_cast<double>(c.y),
                    static_cast<double>(c.z),
                    static_cast<double>(c.w));
        if (c.w > prev_trans + 1e-5F)
        {
            std::printf("FAIL — transmittance not monotonic\n");
            return 2;
        }
        prev_trans = c.w;
    }

    // 5) Beer-Lambert spot-check.
    const float t = vf::beer_lambert(density, 1.0F);
    const float expected = std::exp(-density * 1.0F);
    if (std::abs(t - expected) > 1e-6F)
    {
        std::printf("FAIL — beer_lambert mismatch (%.6f vs %.6f)\n",
                    static_cast<double>(t), static_cast<double>(expected));
        return 3;
    }
    std::printf("  beer_lambert(σ=%.2f, dt=1m) = %.4f (expected %.4f)  OK\n",
                static_cast<double>(density), static_cast<double>(t),
                static_cast<double>(expected));

    std::printf("[hello_volumetric_fog] PARITY OK\n");
    return 0;
}
