// =============================================================================
// CHROMODYNAMIC — samples/hello_decals
//
// CPU-reference smoke for cd::decal. Projects 8 test world-space points
// against a decal OBB at the origin and verifies inside/outside +
// UV-mapping. Same math as the GLSL projector pass (Persson 2009).
// =============================================================================
#include <cd/core/Version.hpp>
#include <cd/decal/Decal.hpp>

#include <array>
#include <cmath>
#include <cstdio>

int main()
{
    std::printf("CHROMODYNAMIC %u.%u.%u — hello_decals\n",
                static_cast<unsigned>(cd::core::kEngineVersion.major),
                static_cast<unsigned>(cd::core::kEngineVersion.minor),
                static_cast<unsigned>(cd::core::kEngineVersion.patch));

    namespace dc = cd::decal;

    // Decal at origin, half-extent 1.0 m along each axis, identity basis.
    dc::Decal d {};
    d.position     = { 0.0F, 0.0F, 0.0F };
    d.right        = { 1.0F, 0.0F, 0.0F };
    d.up           = { 0.0F, 1.0F, 0.0F };
    d.forward      = { 0.0F, 0.0F, 1.0F };
    d.half_extents = { 1.0F, 1.0F, 1.0F };
    d.atlas_uv_rect = { 0.25F, 0.25F, 0.75F, 0.75F };  // mid-quadrant atlas tile

    struct TestPoint
    {
        cd::math::Vec3f p;
        bool expected_inside;
        const char* name;
    };
    const std::array<TestPoint, 8> tests {{
        {{ 0.0F, 0.0F, 0.0F},  true,  "centre"        },
        {{ 0.5F, 0.5F, 0.5F},  true,  "interior"      },
        {{ 0.9F, 0.9F, 0.9F},  true,  "near corner"   },
        {{ 1.5F, 0.0F, 0.0F},  false, "outside +X"    },
        {{ 0.0F, 1.5F, 0.0F},  false, "outside +Y"    },
        {{ 0.0F, 0.0F, 1.5F},  false, "outside +Z"    },
        {{-1.1F, 0.0F, 0.0F},  false, "outside -X"    },
        {{ 0.0F,-1.1F, 0.0F},  false, "outside -Y"    },
    }};

    int fails = 0;
    for (const auto& t : tests)
    {
        cd::math::Vec3f local {};
        std::array<float, 2> uv {};
        const bool in = dc::project_world_to_decal(d, t.p, local, uv);
        const bool ok = (in == t.expected_inside);
        std::printf("  %-12s p=(%+5.2f,%+5.2f,%+5.2f) → %s   uv=(%5.3f, %5.3f)  %s\n",
                    t.name,
                    static_cast<double>(t.p.x), static_cast<double>(t.p.y), static_cast<double>(t.p.z),
                    in ? "INSIDE " : "OUTSIDE",
                    static_cast<double>(uv[0]), static_cast<double>(uv[1]),
                    ok ? "OK" : "FAIL");
        if (!ok) ++fails;
    }

    if (fails != 0)
    {
        std::printf("[hello_decals] %d FAILURES\n", fails);
        return 1;
    }

    // UV bounds — centre point should map to atlas midpoint.
    cd::math::Vec3f l {};
    std::array<float, 2> uv {};
    (void)dc::project_world_to_decal(d, { 0.0F, 0.0F, 0.0F }, l, uv);
    const float expected_u = 0.5F * (0.25F + 0.75F);
    const float expected_v = 0.5F * (0.25F + 0.75F);
    if (std::abs(uv[0] - expected_u) > 1e-5F ||
        std::abs(uv[1] - expected_v) > 1e-5F)
    {
        std::printf("FAIL — centre UV mismatch (got %.4f,%.4f expected %.4f,%.4f)\n",
                    static_cast<double>(uv[0]), static_cast<double>(uv[1]),
                    static_cast<double>(expected_u), static_cast<double>(expected_v));
        return 2;
    }

    std::printf("[hello_decals] PARITY OK\n");
    return 0;
}
