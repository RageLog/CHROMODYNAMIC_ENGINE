// =============================================================================
// CHROMODYNAMIC — samples/hello_bezier
// Phase 24.E — headless cubic-Bezier evaluator smoke. Builds a control-
// point set, samples N points, prints arc-length estimate.
// =============================================================================
#include <cd/core/Version.hpp>
#include <cd/math/CubicBezier.hpp>

#include <cstdio>

int main()
{
    std::fprintf(stdout, "CHROMODYNAMIC %u.%u.%u — hello_bezier\n",
                 static_cast<unsigned>(cd::core::kEngineVersion.major),
                 static_cast<unsigned>(cd::core::kEngineVersion.minor),
                 static_cast<unsigned>(cd::core::kEngineVersion.patch));

    cd::math::CubicBezier b;
    b.p0 = cd::math::Vec3f { 0.0F, 0.0F, 0.0F };
    b.p1 = cd::math::Vec3f { 2.0F, 4.0F, 0.0F };
    b.p2 = cd::math::Vec3f { 6.0F, 4.0F, 0.0F };
    b.p3 = cd::math::Vec3f { 8.0F, 0.0F, 0.0F };

    std::fprintf(stdout, "sampling 11 points along the curve:\n");
    for (int i = 0; i <= 10; ++i)
    {
        const float t = static_cast<float>(i) * 0.1F;
        const auto p = b.at(t);
        std::fprintf(stdout, "  t=%.2f → (%.3f, %.3f, %.3f)\n",
                     static_cast<double>(t),
                     static_cast<double>(p.x),
                     static_cast<double>(p.y),
                     static_cast<double>(p.z));
    }
    std::fprintf(stdout, "arc length (256 samples) = %.4f units\n",
                 static_cast<double>(b.arc_length(256)));
    std::fprintf(stdout, "[hello_bezier] OK\n");
    return 0;
}
