// =============================================================================
// CHROMODYNAMIC — samples/hello_volumetric_clouds
//
// CPU reference for cd::volumetric_clouds (Schneider 2017 cloud march).
// Walks the cloud layer profile + noise → density mapping over a small
// 32-step altitude sweep and prints the density curve. Validates the
// height-fraction bell-shape (zero outside layer, peak at midpoint) and
// the coverage-thresholded density mapping that the GLSL kCloudsMarchCS
// kernel uses to march per-pixel.
// =============================================================================
#include <cd/core/Version.hpp>
#include <cd/volumetric_clouds/Clouds.hpp>

#include <cmath>
#include <cstdio>

int main()
{
    std::printf("CHROMODYNAMIC %u.%u.%u — hello_volumetric_clouds\n",
                static_cast<unsigned>(cd::core::kEngineVersion.major),
                static_cast<unsigned>(cd::core::kEngineVersion.minor),
                static_cast<unsigned>(cd::core::kEngineVersion.patch));

    namespace vc = cd::volumetric_clouds;

    vc::Settings s {};
    s.layer_bottom_km = 1.5F;
    s.layer_top_km    = 5.0F;
    s.coverage        = 0.5F;
    s.density_scale   = 0.05F;

    // height_fraction bell-shape: zero at layer boundaries, peak at
    // midpoint. Sample 32 altitudes from below to above layer.
    std::printf("  height_fraction profile (32 samples, 0..6 km):\n");
    float peak = 0.0F;
    float peak_alt = 0.0F;
    for (int i = 0; i <= 32; ++i)
    {
        const float alt = 6.0F * static_cast<float>(i) / 32.0F;
        const float hf  = vc::height_fraction(alt, s);
        if (hf > peak) { peak = hf; peak_alt = alt; }
        if (i % 4 == 0)
            std::printf("    alt=%4.2f km  hf=%.3f\n",
                        static_cast<double>(alt), static_cast<double>(hf));
    }
    const float expected_peak_alt = 0.5F * (s.layer_bottom_km + s.layer_top_km);
    if (std::abs(peak_alt - expected_peak_alt) > 0.5F || peak < 0.9F)
    {
        std::printf("FAIL — height_fraction bell-shape wrong (peak %.2f km @ %.3f)\n",
                    static_cast<double>(peak_alt), static_cast<double>(peak));
        return 1;
    }
    std::printf("  bell-shape OK (peak %.3f @ %.2f km, expected ~%.2f)\n",
                static_cast<double>(peak), static_cast<double>(peak_alt),
                static_cast<double>(expected_peak_alt));

    // remap monotonicity
    if (vc::remap(0.5F, 0.0F, 1.0F, 10.0F, 20.0F) != 15.0F)
    {
        std::printf("FAIL — remap math\n");
        return 2;
    }
    std::printf("  remap(0.5, [0,1] → [10,20]) = %.2f  OK\n",
                static_cast<double>(vc::remap(0.5F, 0.0F, 1.0F, 10.0F, 20.0F)));

    // density gate: low coverage → only high-noise cells survive.
    s.coverage = 0.2F;
    const float d_low_noise  = vc::density(3.25F, 0.4F, s);
    const float d_high_noise = vc::density(3.25F, 0.95F, s);
    std::printf("  density(alt=3.25 km, noise=0.4, cov=0.2) = %.4f (gated)\n",
                static_cast<double>(d_low_noise));
    std::printf("  density(alt=3.25 km, noise=0.95, cov=0.2) = %.4f (pass)\n",
                static_cast<double>(d_high_noise));
    if (d_low_noise > 0.001F || d_high_noise <= d_low_noise)
    {
        std::printf("FAIL — coverage gate inverted\n");
        return 3;
    }

    // density(out-of-layer) must be zero.
    if (vc::density(0.5F, 1.0F, s) != 0.0F ||
        vc::density(6.0F, 1.0F, s) != 0.0F)
    {
        std::printf("FAIL — out-of-layer density non-zero\n");
        return 4;
    }
    std::printf("  out-of-layer density correctly zero  OK\n");

    std::printf("[hello_volumetric_clouds] PARITY OK\n");
    return 0;
}
