// =============================================================================
// CHROMODYNAMIC — cd/math/QuadraticSolver.hpp
// Phase 83.B / Wave 251 — ax² + bx + c = 0 with stable formulation.
//
// The textbook `(-b ± √Δ) / (2a)` suffers from catastrophic
// cancellation when `b` dominates `4ac`. The stable form picks the
// root that *adds* rather than subtracts, then computes the other
// via Vieta's `x1*x2 = c/a`:
//
//   q  = -0.5 * (b + sign(b)*√Δ)
//   x1 = q / a
//   x2 = c / q
//
// `solve_quadratic(a, b, c)` returns `{ root_count, x1, x2 }`. Linear
// edge (a == 0) handled. Negative discriminant returns count = 0.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cmath>

namespace cd::math
{

struct QuadraticRoots
{
    int   count { 0 };
    float x1    { 0.0F };
    float x2    { 0.0F };
};

[[nodiscard]] inline QuadraticRoots solve_quadratic(float a, float b, float c) noexcept
{
    QuadraticRoots r;
    if (std::fabs(a) < 1e-12F)
    {
        // Linear: bx + c = 0 → x = -c/b.
        if (std::fabs(b) < 1e-12F) return r;
        r.count = 1;
        r.x1 = -c / b;
        return r;
    }
    const float disc = b * b - 4.0F * a * c;
    if (disc < 0.0F) return r;
    if (disc == 0.0F)
    {
        r.count = 1;
        r.x1 = -b / (2.0F * a);
        return r;
    }
    const float sq = std::sqrt(disc);
    const float sign_b = (b >= 0.0F) ? 1.0F : -1.0F;
    const float q = -0.5F * (b + sign_b * sq);
    r.count = 2;
    r.x1 = q / a;
    r.x2 = c / q;
    if (r.x1 > r.x2) { const float t = r.x1; r.x1 = r.x2; r.x2 = t; }
    return r;
}

}  // namespace cd::math
