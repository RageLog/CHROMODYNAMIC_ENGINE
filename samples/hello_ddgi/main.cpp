// =============================================================================
// CHROMODYNAMIC — samples/hello_ddgi
//
// CPU-reference smoke for cd::ddgi (Majercik 2019). Allocates a small
// 4x2x4 probe grid, samples world-space points + computes trilinear
// probe weights, verifies the weights sum to 1 inside the grid + sum
// to less than 1 (fallback) outside.
// =============================================================================
#include <cd/core/Version.hpp>
#include <cd/ddgi/Ddgi.hpp>

#include <array>
#include <cmath>
#include <cstdio>

int main()
{
    std::printf("CHROMODYNAMIC %u.%u.%u — hello_ddgi\n",
                static_cast<unsigned>(cd::core::kEngineVersion.major),
                static_cast<unsigned>(cd::core::kEngineVersion.minor),
                static_cast<unsigned>(cd::core::kEngineVersion.patch));

    namespace gi = cd::ddgi;

    gi::GridConfig g {};
    g.origin   = { 0.0F, 0.0F, 0.0F };
    g.spacing  = { 2.0F, 2.0F, 2.0F };
    g.probes_x = 4;
    g.probes_y = 2;
    g.probes_z = 4;
    std::printf("  grid: %u × %u × %u probes, spacing %.1fm\n",
                g.probes_x, g.probes_y, g.probes_z,
                static_cast<double>(g.spacing.x));

    // probe_world_pos round-trip.
    const auto p123 = gi::probe_world_pos(g, 1, 0, 2);
    if (std::abs(p123.x - 2.0F) > 1e-6F ||
        std::abs(p123.y - 0.0F) > 1e-6F ||
        std::abs(p123.z - 4.0F) > 1e-6F)
    {
        std::printf("FAIL — probe_world_pos\n");
        return 1;
    }
    std::printf("  probe(1,0,2) at (%.1f, %.1f, %.1f) — OK\n",
                static_cast<double>(p123.x),
                static_cast<double>(p123.y),
                static_cast<double>(p123.z));

    struct TestPoint
    {
        cd::math::Vec3f p;
        bool expect_full_sum;
        const char* name;
    };
    const std::array<TestPoint, 5> tests {{
        { {  3.0F, 1.0F, 3.0F }, true,  "deep interior"   },
        { {  1.0F, 0.5F, 1.0F }, true,  "near probe"      },
        { {  5.0F, 1.0F, 5.0F }, true,  "interior edge"   },
        { { -1.0F, 1.0F, 1.0F }, false, "outside -X"      },
        { {  8.0F, 1.0F, 1.0F }, false, "outside +X"      },
    }};

    int fails = 0;
    for (const auto& t : tests)
    {
        std::array<float, 8> w {};
        std::array<std::array<std::uint32_t, 3>, 8> corners {};
        gi::trilinear_probe_weights(g, t.p, w, corners);
        float sum = 0.0F;
        for (float wi : w) sum += wi;
        const bool full = std::abs(sum - 1.0F) < 1e-4F;
        const bool ok = (full == t.expect_full_sum);
        std::printf("  %-16s p=(%+5.2f,%+5.2f,%+5.2f)  Σw=%.4f  %s\n",
                    t.name,
                    static_cast<double>(t.p.x), static_cast<double>(t.p.y), static_cast<double>(t.p.z),
                    static_cast<double>(sum),
                    ok ? "OK" : "FAIL");
        if (!ok) ++fails;
    }
    if (fails != 0)
    {
        std::printf("[hello_ddgi] %d FAILURES\n", fails);
        return 2;
    }

    std::printf("[hello_ddgi] PARITY OK\n");
    return 0;
}
