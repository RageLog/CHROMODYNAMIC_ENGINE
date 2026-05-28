// =============================================================================
// CHROMODYNAMIC — cd/render/PlanarShadow.hpp
//
// Phase 292 / Marathon Run 7 sub-N1D: planar (projection) shadow matrix
// builder, extracted out of hello_engine.
//
// `make_planar_shadow_matrix` flattens any caster vertex onto the plane
// y = plane_y along the directional-light ray. The returned column-major
// affine transform pre-multiplies the caster's model matrix to produce
// the "shadow model" — the geometry then renders with a flat dark tint
// (no lighting) onto the floor.
//
// Header-only, no RHI dependency — pure math. Lives in cd::render (the
// generic render lib) so samples doing offline shadow projection
// without booting cd::shadow / cd::render::shadow_atlas can grab it.
// =============================================================================
#pragma once

#include <cd/math/Matrix.hpp>
#include <cd/math/Vector.hpp>

#include <cmath>

namespace cd::render
{

// =============================================================================
// Build the affine transform that projects any world-space caster point
// onto the plane y = plane_y along the directional-light ray `sun_dir`
// (which is the direction the light TRAVELS in — sun pointing down-
// forward has sun_dir.y < 0).
//
// Derivation: caster point P, projected P' satisfies
//   P' = P + t * sun_dir,    requiring P'.y = plane_y
//   -> t   = (plane_y - P.y) / sun_dir.y
//   -> P'.x = P.x + t * sun_dir.x
//   -> P'.z = P.z + t * sun_dir.z
//
// In column-major Mat4f (cd::math convention, see ADR-017 P4):
//   S[0] = ( 1,            0,       0,            0 )
//   S[1] = ( -Lx/Ly,       0,      -Lz/Ly,        0 )
//   S[2] = ( 0,            0,       1,            0 )
//   S[3] = ( py*Lx/Ly,    py,       py*Lz/Ly,     1 )    where py = plane_y+lift
//
// `lift` is added to plane_y so projected verts sit just above the
// floor and dodge depth-fight. A near-horizontal sun has |Ly| clamped
// to kMinAbs (0.1) so the matrix doesn't collapse to a divide-by-zero.
// =============================================================================
[[nodiscard]] inline cd::math::Mat4f
make_planar_shadow_matrix(const cd::math::Vec3f& sun_dir, float plane_y, float lift) noexcept
{
    // Clamp |Ly| away from 0 so a sun coming in horizontally doesn't
    // produce an infinite-length shadow (numerically: divide-by-zero).
    constexpr float kMinAbs = 0.10F;
    float Ly = sun_dir.y;
    if (std::fabs(Ly) < kMinAbs)
        Ly = (Ly < 0.0F) ? -kMinAbs : kMinAbs;
    const float k = 1.0F / Ly;
    const float ax = sun_dir.x * k;
    const float az = sun_dir.z * k;
    const float py = plane_y + lift;
    cd::math::Mat4f m = cd::math::Mat4f::identity();
    // Column 0: x-axis untouched.
    m[0][0] = 1.0F;
    m[0][1] = 0.0F;
    m[0][2] = 0.0F;
    m[0][3] = 0.0F;
    // Column 1: y-input bleeds into x and z, y-output zeroed (plane).
    m[1][0] = -ax;
    m[1][1] = 0.0F;
    m[1][2] = -az;
    m[1][3] = 0.0F;
    // Column 2: z-axis untouched.
    m[2][0] = 0.0F;
    m[2][1] = 0.0F;
    m[2][2] = 1.0F;
    m[2][3] = 0.0F;
    // Column 3: translation pins y to plane_y+lift and adds the
    // origin-shift contribution from the (h - 0) projection offset.
    m[3][0] = py * ax;
    m[3][1] = py;
    m[3][2] = py * az;
    m[3][3] = 1.0F;
    return m;
}

}  // namespace cd::render
