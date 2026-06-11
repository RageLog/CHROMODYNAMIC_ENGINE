// =============================================================================
// CHROMODYNAMIC — cd/asset/Primitives.hpp
// Phase 106 / Wave 273 — header-only procedural mesh builders.
//
// Cube, sphere (UV), cone, cylinder, capsule, plane, torus — all built
// from scratch on the CPU into a (vertices, indices) pair the sample +
// editor + ECS-spawner can upload straight into an RHI vertex / index
// buffer. Vertex format is fixed at pos+normal+uv+color (44 B) which
// covers every demo + first-pass shader the engine ships.
//
// Header-only because these builders are tiny and stateless; pulling
// in a translation unit just to spawn a unit cube would be silly.
// All functions return by value; copy-elision keeps it cheap.
//
// Conventions:
//   * Right-handed coordinate frame, +Y up.
//   * Cube / sphere / cone / cylinder all span [-0.5 .. +0.5] along
//     their dominant axis so a unit-scale spawn is one-unit tall.
//   * Plane is on the X/Z plane at Y=0, normal = +Y.
//   * Torus has major-radius along the X/Z plane, minor along the
//     ring tangent.
//   * UVs use the convention (u = around-axis, v = along-axis).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cd::asset
{

struct PrimitiveVertex
{
    float pos[3];
    float normal[3];
    float uv[2];
    float color[3];
};

static_assert(sizeof(PrimitiveVertex) == 44,
              "PrimitiveVertex layout must stay tightly packed for upload");

struct PrimitiveMesh
{
    std::vector<PrimitiveVertex>  vertices;
    std::vector<std::uint16_t>    indices;      ///< u16 path — valid when indices_u32 is empty
    std::vector<std::uint32_t>    indices_u32;  ///< u32 path — set when vertex count > 65535
};

namespace primitives_detail
{

inline constexpr float kPi  = 3.14159265358979F;
inline constexpr float kTau = 6.28318530717958F;

inline void push_quad_indices(std::vector<std::uint16_t>& idx,
                              std::uint16_t a, std::uint16_t b,
                              std::uint16_t c, std::uint16_t d) noexcept
{
    // CCW from front, two triangles: (a,b,c) + (a,c,d).
    idx.push_back(a); idx.push_back(b); idx.push_back(c);
    idx.push_back(a); idx.push_back(c); idx.push_back(d);
}

inline PrimitiveVertex make_v(float px, float py, float pz,
                              float nx, float ny, float nz,
                              float u, float v,
                              float r = 1.0F, float g = 1.0F, float b = 1.0F) noexcept
{
    PrimitiveVertex out {};
    out.pos[0] = px; out.pos[1] = py; out.pos[2] = pz;
    out.normal[0] = nx; out.normal[1] = ny; out.normal[2] = nz;
    out.uv[0] = u; out.uv[1] = v;
    out.color[0] = r; out.color[1] = g; out.color[2] = b;
    return out;
}

}  // namespace primitives_detail

/// Unit cube centred at origin, side length 1. 24 vertices (4 per face)
/// so each face has its own normal + UVs; 36 indices (6 faces × 2 tris).
[[nodiscard]] inline PrimitiveMesh make_cube()
{
    using primitives_detail::make_v;
    using primitives_detail::push_quad_indices;

    PrimitiveMesh m;
    m.vertices.reserve(24);
    m.indices.reserve(36);

    // Per-face quads. Order: +X, -X, +Y, -Y, +Z, -Z. Each face's CCW
    // winding looks at the face from the outside.
    struct Face { float n[3]; float t[3]; float b[3]; float color[3]; };
    constexpr Face kFaces[6] = {
        { { 1, 0, 0}, {0, 0,-1}, {0, 1, 0}, {1.00F, 0.50F, 0.50F} },  // +X red
        { {-1, 0, 0}, {0, 0, 1}, {0, 1, 0}, {0.50F, 0.00F, 0.00F} },  // -X dark red
        { { 0, 1, 0}, {1, 0, 0}, {0, 0, 1}, {0.50F, 1.00F, 0.50F} },  // +Y green
        { { 0,-1, 0}, {1, 0, 0}, {0, 0,-1}, {0.00F, 0.50F, 0.00F} },  // -Y dark green
        { { 0, 0, 1}, {1, 0, 0}, {0, 1, 0}, {0.50F, 0.50F, 1.00F} },  // +Z blue
        { { 0, 0,-1}, {-1,0, 0}, {0, 1, 0}, {0.00F, 0.00F, 0.50F} },  // -Z dark blue
    };
    for (const auto& f : kFaces)
    {
        const auto base = static_cast<std::uint16_t>(m.vertices.size());
        // Four corners: -t-b, +t-b, +t+b, -t+b
        for (int sy = -1; sy <= 1; sy += 2)
        {
            for (int sx = -1; sx <= 1; sx += 2)
            {
                const float px = 0.5F * (f.n[0] + f.t[0] * static_cast<float>(sx) + f.b[0] * static_cast<float>(sy));
                const float py = 0.5F * (f.n[1] + f.t[1] * static_cast<float>(sx) + f.b[1] * static_cast<float>(sy));
                const float pz = 0.5F * (f.n[2] + f.t[2] * static_cast<float>(sx) + f.b[2] * static_cast<float>(sy));
                const float u = (sx > 0) ? 1.0F : 0.0F;
                const float v = (sy > 0) ? 1.0F : 0.0F;
                m.vertices.push_back(make_v(px, py, pz,
                                            f.n[0], f.n[1], f.n[2],
                                            u, v,
                                            f.color[0], f.color[1], f.color[2]));
            }
        }
        // (-,-) (+,-) (+,+) (-,+) — flat-walk corner order. Indices 0,1,2,3.
        push_quad_indices(m.indices, base, static_cast<std::uint16_t>(base + 1),
                                     static_cast<std::uint16_t>(base + 3),
                                     static_cast<std::uint16_t>(base + 2));
    }
    return m;
}

/// UV-sphere, radius 0.5 (unit-diameter). `stacks` = horizontal rings,
/// `slices` = vertical wedges. Default (18, 28) yields ~530 vertices.
/// Vertex normal equals position (unit-radius sphere); UVs are
/// (theta / 2π, phi / π).
[[nodiscard]] inline PrimitiveMesh make_sphere(int stacks = 18, int slices = 28)
{
    using primitives_detail::kPi;
    using primitives_detail::kTau;
    using primitives_detail::make_v;

    if (stacks < 2) stacks = 2;
    if (slices < 3) slices = 3;

    PrimitiveMesh m;
    m.vertices.reserve((static_cast<std::size_t>(stacks) + 1U) * (static_cast<std::size_t>(slices) + 1U));
    m.indices.reserve(static_cast<std::size_t>(stacks) * static_cast<std::size_t>(slices) * 6U);

    for (int i = 0; i <= stacks; ++i)
    {
        const float v_t = static_cast<float>(i) / static_cast<float>(stacks);
        const float phi = v_t * kPi;
        const float sp = std::sin(phi);
        const float cp = std::cos(phi);
        for (int j = 0; j <= slices; ++j)
        {
            const float u_t = static_cast<float>(j) / static_cast<float>(slices);
            const float theta = u_t * kTau;
            const float st = std::sin(theta);
            const float ct = std::cos(theta);
            const float x = sp * ct;
            const float y = cp;
            const float z = sp * st;
            m.vertices.push_back(make_v(0.5F * x, 0.5F * y, 0.5F * z,
                                        x, y, z,
                                        u_t, v_t,
                                        0.5F + 0.5F * x,
                                        0.5F + 0.5F * y,
                                        0.5F + 0.5F * z));
        }
    }
    for (int i = 0; i < stacks; ++i)
    {
        for (int j = 0; j < slices; ++j)
        {
            const auto a = static_cast<std::uint16_t>(i * (slices + 1) + j);
            const auto b = static_cast<std::uint16_t>(a + slices + 1);
            m.indices.push_back(a);
            m.indices.push_back(b);
            m.indices.push_back(static_cast<std::uint16_t>(a + 1));
            m.indices.push_back(b);
            m.indices.push_back(static_cast<std::uint16_t>(b + 1));
            m.indices.push_back(static_cast<std::uint16_t>(a + 1));
        }
    }
    return m;
}

/// Cone, apex at +Y 0.5, circular base at -Y 0.5, base radius 0.5.
/// `slices` controls the smoothness of the lateral surface and the base
/// disk. Side and base use separate vertex sets so each surface can
/// hold its own normal without sharing a discontinuity.
[[nodiscard]] inline PrimitiveMesh make_cone(int slices = 32)
{
    using primitives_detail::kTau;
    using primitives_detail::make_v;

    if (slices < 3) slices = 3;
    PrimitiveMesh m;
    m.vertices.reserve(2U + 2U * static_cast<std::size_t>(slices));
    m.indices.reserve(static_cast<std::size_t>(slices) * 6U);

    const float slant = std::sqrt(0.25F + 1.0F);  // sqrt(r² + h²) with r=0.5, h=1
    const float ny = 0.5F / slant;                // side-normal Y component
    const float nr = 1.0F / slant;                // side-normal radial scale

    // Apex (shared) — index 0.
    m.vertices.push_back(make_v(0.0F, 0.5F, 0.0F,
                                0.0F, 1.0F, 0.0F,
                                0.5F, 1.0F,
                                1.00F, 1.00F, 1.00F));
    // Base centre (shared) — index 1.
    m.vertices.push_back(make_v(0.0F, -0.5F, 0.0F,
                                0.0F, -1.0F, 0.0F,
                                0.5F, 0.0F,
                                0.30F, 0.30F, 0.30F));
    // Side ring — indices 2 .. 2+slices-1.
    for (int i = 0; i < slices; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(slices) * kTau;
        const float cx = std::cos(t);
        const float cz = std::sin(t);
        m.vertices.push_back(make_v(0.5F * cx, -0.5F, 0.5F * cz,
                                    nr * cx, ny, nr * cz,
                                    static_cast<float>(i) / static_cast<float>(slices), 0.0F,
                                    0.5F + 0.5F * cx, 0.7F, 0.5F + 0.5F * cz));
    }
    // Base disk ring — indices 2+slices .. 2+2*slices-1.
    for (int i = 0; i < slices; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(slices) * kTau;
        const float cx = std::cos(t);
        const float cz = std::sin(t);
        m.vertices.push_back(make_v(0.5F * cx, -0.5F, 0.5F * cz,
                                    0.0F, -1.0F, 0.0F,
                                    0.5F + 0.5F * cx, 0.5F + 0.5F * cz,
                                    0.20F, 0.20F, 0.25F));
    }
    // Sides — fan from apex (0).
    for (int i = 0; i < slices; ++i)
    {
        const auto a = static_cast<std::uint16_t>(2 + i);
        const auto b = static_cast<std::uint16_t>(2 + (i + 1) % slices);
        m.indices.push_back(0);
        m.indices.push_back(a);
        m.indices.push_back(b);
    }
    // Base disk — fan from base centre (1), CCW seen from below.
    for (int i = 0; i < slices; ++i)
    {
        const auto a = static_cast<std::uint16_t>(2 + slices + i);
        const auto b = static_cast<std::uint16_t>(2 + slices + (i + 1) % slices);
        m.indices.push_back(1);
        m.indices.push_back(b);
        m.indices.push_back(a);
    }
    return m;
}

/// Cylinder, radius 0.5, height 1 (Y from -0.5 to +0.5).
/// Side has its own ring; top + bottom caps are fan-triangulated.
[[nodiscard]] inline PrimitiveMesh make_cylinder(int slices = 32)
{
    using primitives_detail::kTau;
    using primitives_detail::make_v;

    if (slices < 3) slices = 3;
    PrimitiveMesh m;
    m.vertices.reserve(2U + 4U * static_cast<std::size_t>(slices));
    m.indices.reserve(static_cast<std::size_t>(slices) * 12U);

    // Top + bottom centre vertices — indices 0, 1.
    m.vertices.push_back(make_v(0.0F,  0.5F, 0.0F, 0.0F, 1.0F, 0.0F,
                                0.5F, 0.5F, 0.95F, 0.95F, 0.95F));
    m.vertices.push_back(make_v(0.0F, -0.5F, 0.0F, 0.0F,-1.0F, 0.0F,
                                0.5F, 0.5F, 0.30F, 0.30F, 0.30F));

    const auto base_side_top = static_cast<std::uint16_t>(m.vertices.size());
    // Side ring top.
    for (int i = 0; i <= slices; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(slices) * kTau;
        const float cx = std::cos(t);
        const float cz = std::sin(t);
        m.vertices.push_back(make_v(0.5F * cx, 0.5F, 0.5F * cz,
                                    cx, 0.0F, cz,
                                    static_cast<float>(i) / static_cast<float>(slices), 0.0F,
                                    0.5F + 0.5F * cx, 0.8F, 0.5F + 0.5F * cz));
    }
    const auto base_side_bot = static_cast<std::uint16_t>(m.vertices.size());
    for (int i = 0; i <= slices; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(slices) * kTau;
        const float cx = std::cos(t);
        const float cz = std::sin(t);
        m.vertices.push_back(make_v(0.5F * cx, -0.5F, 0.5F * cz,
                                    cx, 0.0F, cz,
                                    static_cast<float>(i) / static_cast<float>(slices), 1.0F,
                                    0.5F + 0.5F * cx, 0.4F, 0.5F + 0.5F * cz));
    }
    // Side quads.
    for (int i = 0; i < slices; ++i)
    {
        const auto a = static_cast<std::uint16_t>(base_side_top + i);
        const auto b = static_cast<std::uint16_t>(base_side_top + i + 1);
        const auto c = static_cast<std::uint16_t>(base_side_bot + i + 1);
        const auto d = static_cast<std::uint16_t>(base_side_bot + i);
        primitives_detail::push_quad_indices(m.indices, a, b, c, d);
    }
    // Cap rings (separate, with up/down normals).
    const auto base_cap_top = static_cast<std::uint16_t>(m.vertices.size());
    for (int i = 0; i < slices; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(slices) * kTau;
        const float cx = std::cos(t);
        const float cz = std::sin(t);
        m.vertices.push_back(make_v(0.5F * cx, 0.5F, 0.5F * cz, 0.0F, 1.0F, 0.0F,
                                    0.5F + 0.5F * cx, 0.5F + 0.5F * cz,
                                    0.95F, 0.95F, 0.95F));
    }
    const auto base_cap_bot = static_cast<std::uint16_t>(m.vertices.size());
    for (int i = 0; i < slices; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(slices) * kTau;
        const float cx = std::cos(t);
        const float cz = std::sin(t);
        m.vertices.push_back(make_v(0.5F * cx, -0.5F, 0.5F * cz, 0.0F,-1.0F, 0.0F,
                                    0.5F + 0.5F * cx, 0.5F + 0.5F * cz,
                                    0.30F, 0.30F, 0.30F));
    }
    for (int i = 0; i < slices; ++i)
    {
        const auto a = static_cast<std::uint16_t>(base_cap_top + i);
        const auto b = static_cast<std::uint16_t>(base_cap_top + (i + 1) % slices);
        m.indices.push_back(0);
        m.indices.push_back(a);
        m.indices.push_back(b);

        const auto c = static_cast<std::uint16_t>(base_cap_bot + i);
        const auto d = static_cast<std::uint16_t>(base_cap_bot + (i + 1) % slices);
        m.indices.push_back(1);
        m.indices.push_back(d);
        m.indices.push_back(c);
    }
    return m;
}

/// Plane on X/Z, side length 1, normal = +Y.
[[nodiscard]] inline PrimitiveMesh make_plane(float size = 1.0F)
{
    using primitives_detail::make_v;
    using primitives_detail::push_quad_indices;
    const float h = 0.5F * size;
    PrimitiveMesh m;
    m.vertices.push_back(make_v(-h, 0.0F, -h, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.7F, 0.7F, 0.7F));
    m.vertices.push_back(make_v( h, 0.0F, -h, 0.0F, 1.0F, 0.0F, 1.0F, 0.0F, 0.7F, 0.7F, 0.7F));
    m.vertices.push_back(make_v( h, 0.0F,  h, 0.0F, 1.0F, 0.0F, 1.0F, 1.0F, 0.7F, 0.7F, 0.7F));
    m.vertices.push_back(make_v(-h, 0.0F,  h, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.7F, 0.7F, 0.7F));
    push_quad_indices(m.indices, 0, 1, 2, 3);
    return m;
}

/// Torus — major radius along X/Z, minor radius around the ring tangent.
/// Default (16 rings × 24 sides) gives a smooth donut at ~400 verts.
[[nodiscard]] inline PrimitiveMesh make_torus(float major_radius = 0.5F,
                                              float minor_radius = 0.15F,
                                              int rings = 16,
                                              int sides = 24)
{
    using primitives_detail::kTau;
    using primitives_detail::make_v;

    if (rings < 3) rings = 3;
    if (sides < 3) sides = 3;
    PrimitiveMesh m;
    m.vertices.reserve((static_cast<std::size_t>(rings) + 1U) * (static_cast<std::size_t>(sides) + 1U));
    m.indices.reserve(static_cast<std::size_t>(rings) * static_cast<std::size_t>(sides) * 6U);

    for (int i = 0; i <= rings; ++i)
    {
        const float u = static_cast<float>(i) / static_cast<float>(rings);
        const float phi = u * kTau;
        const float cphi = std::cos(phi);
        const float sphi = std::sin(phi);
        for (int j = 0; j <= sides; ++j)
        {
            const float v = static_cast<float>(j) / static_cast<float>(sides);
            const float theta = v * kTau;
            const float ctheta = std::cos(theta);
            const float stheta = std::sin(theta);
            const float x = (major_radius + minor_radius * ctheta) * cphi;
            const float y = minor_radius * stheta;
            const float z = (major_radius + minor_radius * ctheta) * sphi;
            const float nx = ctheta * cphi;
            const float ny = stheta;
            const float nz = ctheta * sphi;
            m.vertices.push_back(make_v(x, y, z, nx, ny, nz, u, v,
                                        0.5F + 0.5F * nx,
                                        0.5F + 0.5F * ny,
                                        0.5F + 0.5F * nz));
        }
    }
    for (int i = 0; i < rings; ++i)
    {
        for (int j = 0; j < sides; ++j)
        {
            const auto a = static_cast<std::uint16_t>(i * (sides + 1) + j);
            const auto b = static_cast<std::uint16_t>(a + sides + 1);
            primitives_detail::push_quad_indices(
                m.indices, a, b,
                static_cast<std::uint16_t>(b + 1),
                static_cast<std::uint16_t>(a + 1));
        }
    }
    return m;
}

/// Capsule — two hemispheres of radius `r` joined by a cylinder of
/// height `h` along Y. Total height = h + 2r. Defaults r=0.25, h=0.5
/// gives a unit-height capsule.
[[nodiscard]] inline PrimitiveMesh make_capsule(float radius = 0.25F,
                                                float height = 0.5F,
                                                int rings = 8,
                                                int slices = 24)
{
    using primitives_detail::kPi;
    using primitives_detail::kTau;
    using primitives_detail::make_v;

    if (rings < 1) rings = 1;
    if (slices < 3) slices = 3;
    const float half_h = 0.5F * height;
    PrimitiveMesh m;
    m.vertices.reserve((2U * static_cast<std::size_t>(rings) + 2U) * (static_cast<std::size_t>(slices) + 1U));
    m.indices.reserve((2U * static_cast<std::size_t>(rings) + 1U) * static_cast<std::size_t>(slices) * 6U);

    // Top hemisphere: phi ∈ [0, π/2], rings+1 latitude lines.
    for (int i = 0; i <= rings; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(rings);
        const float phi = t * (0.5F * kPi);
        const float sp = std::sin(phi);
        const float cp = std::cos(phi);
        for (int j = 0; j <= slices; ++j)
        {
            const float theta = static_cast<float>(j) / static_cast<float>(slices) * kTau;
            const float st = std::sin(theta);
            const float ct = std::cos(theta);
            const float x = sp * ct;
            const float y = cp;
            const float z = sp * st;
            m.vertices.push_back(make_v(radius * x, half_h + radius * y, radius * z,
                                        x, y, z,
                                        static_cast<float>(j) / static_cast<float>(slices),
                                        0.5F + 0.5F * (1.0F - t),
                                        0.6F + 0.4F * y,
                                        0.6F + 0.4F * y,
                                        0.7F));
        }
    }
    // Bottom hemisphere: phi ∈ [π/2, π], same ring count.
    for (int i = 0; i <= rings; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(rings);
        const float phi = (0.5F + 0.5F * t) * kPi;
        const float sp = std::sin(phi);
        const float cp = std::cos(phi);
        for (int j = 0; j <= slices; ++j)
        {
            const float theta = static_cast<float>(j) / static_cast<float>(slices) * kTau;
            const float st = std::sin(theta);
            const float ct = std::cos(theta);
            const float x = sp * ct;
            const float y = cp;
            const float z = sp * st;
            m.vertices.push_back(make_v(radius * x, -half_h + radius * y, radius * z,
                                        x, y, z,
                                        static_cast<float>(j) / static_cast<float>(slices),
                                        0.5F * (1.0F - t),
                                        0.5F + 0.4F * y,
                                        0.5F + 0.4F * y,
                                        0.7F));
        }
    }
    // Indices — single sweep across all 2*(rings+1) latitudes. Note
    // the gap between top-last and bottom-first stitches the cylinder
    // middle (rectangular strip) into the mesh.
    const int total_lats = 2 * (rings + 1);
    const int stride = slices + 1;
    for (int i = 0; i < total_lats - 1; ++i)
    {
        for (int j = 0; j < slices; ++j)
        {
            const auto a = static_cast<std::uint16_t>(i * stride + j);
            const auto b = static_cast<std::uint16_t>(a + stride);
            m.indices.push_back(a);
            m.indices.push_back(b);
            m.indices.push_back(static_cast<std::uint16_t>(a + 1));
            m.indices.push_back(b);
            m.indices.push_back(static_cast<std::uint16_t>(b + 1));
            m.indices.push_back(static_cast<std::uint16_t>(a + 1));
        }
    }
    return m;
}

// =============================================================================
// Procedural compound + parametric shapes (extracted from hello_engine).
// =============================================================================

/// Trefoil-like (p, q) torus knot. p=2, q=3 is the classic trefoil;
/// (3, 2) / (3, 5) / (5, 2) produce other named torus-knot
/// topologies. The output is a UV-mapped, smooth-shaded tube swept
/// along the knot curve with a Frenet-style frame.
[[nodiscard]] inline PrimitiveMesh
make_torus_knot(float radius = 0.6F,
                float tube_radius = 0.18F,
                [[maybe_unused]] int p = 2, int q = 3,
                int curve_segments = 256,
                int tube_segments = 24)
{
    using primitives_detail::kTau;
    using primitives_detail::make_v;

    PrimitiveMesh m;
    if (curve_segments < 4 || tube_segments < 3) return m;

    auto curve = [&](float t) {
        const float ct  = std::cos(t);
        const float st  = std::sin(t);
        const float cqt = std::cos(static_cast<float>(q) * t);
        const float r   = radius * (2.0F + cqt);
        return cd::math::Vec3f {
            0.5F * r * ct,
            0.5F * r * st,
            0.5F * radius * std::sin(static_cast<float>(q) * t) };
    };

    for (int i = 0; i < curve_segments; ++i)
    {
        const float t  = (static_cast<float>(i) / static_cast<float>(curve_segments)) * kTau;
        const float dt = kTau / static_cast<float>(curve_segments) * 0.5F;
        const auto pt   = curve(t);
        const auto next = curve(t + dt);
        cd::math::Vec3f T { next.x - pt.x, next.y - pt.y, next.z - pt.z };
        const float tlen = std::sqrt(T.x*T.x + T.y*T.y + T.z*T.z);
        if (tlen > 1e-6F) { T.x/=tlen; T.y/=tlen; T.z/=tlen; }
        cd::math::Vec3f up { 0.0F, 1.0F, 0.0F };
        if (std::abs(T.y) > 0.95F) up = { 1.0F, 0.0F, 0.0F };
        cd::math::Vec3f N {
            up.y*T.z - up.z*T.y,
            up.z*T.x - up.x*T.z,
            up.x*T.y - up.y*T.x };
        const float nlen = std::sqrt(N.x*N.x + N.y*N.y + N.z*N.z);
        if (nlen > 1e-6F) { N.x/=nlen; N.y/=nlen; N.z/=nlen; }
        cd::math::Vec3f B {
            T.y*N.z - T.z*N.y,
            T.z*N.x - T.x*N.z,
            T.x*N.y - T.y*N.x };
        for (int j = 0; j <= tube_segments; ++j)
        {
            const float v = static_cast<float>(j) / static_cast<float>(tube_segments);
            const float a = v * kTau;
            const float ca = std::cos(a);
            const float sa = std::sin(a);
            cd::math::Vec3f offset {
                tube_radius * (ca * N.x + sa * B.x),
                tube_radius * (ca * N.y + sa * B.y),
                tube_radius * (ca * N.z + sa * B.z) };
            cd::math::Vec3f vert {
                pt.x + offset.x,
                pt.y + offset.y,
                pt.z + offset.z };
            cd::math::Vec3f normal {
                ca * N.x + sa * B.x,
                ca * N.y + sa * B.y,
                ca * N.z + sa * B.z };
            const float u = static_cast<float>(i) / static_cast<float>(curve_segments);
            m.vertices.push_back(make_v(
                vert.x, vert.y, vert.z,
                normal.x, normal.y, normal.z,
                u, v,
                1.0F, 1.0F, 1.0F));
        }
    }

    const auto ring = static_cast<std::uint16_t>(tube_segments + 1);
    for (int i = 0; i < curve_segments; ++i)
    {
        const int i_next = (i + 1) % curve_segments;
        for (int j = 0; j < tube_segments; ++j)
        {
            const auto a = static_cast<std::uint16_t>(i      * ring + j);
            const auto b = static_cast<std::uint16_t>(i_next * ring + j);
            const auto c = static_cast<std::uint16_t>(i_next * ring + (j + 1));
            const auto d = static_cast<std::uint16_t>(i      * ring + (j + 1));
            m.indices.push_back(a); m.indices.push_back(b); m.indices.push_back(c);
            m.indices.push_back(a); m.indices.push_back(c); m.indices.push_back(d);
        }
    }
    return m;
}

/// Procedural blocky humanoid: head + torso + arms + legs + hands +
/// feet merged into a single PrimitiveMesh. Useful as a no-network
/// fallback when no glTF character asset is available.
[[nodiscard]] inline PrimitiveMesh make_humanoid()
{
    PrimitiveMesh out;
    auto merge = [&](const PrimitiveMesh& src,
                     cd::math::Vec3f offset,
                     cd::math::Vec3f scale,
                     cd::math::Vec2f uv_offset)
    {
        const auto base = static_cast<std::uint16_t>(out.vertices.size());
        for (const auto& v : src.vertices) {
            PrimitiveVertex p = v;
            p.pos[0] = v.pos[0] * scale.x + offset.x;
            p.pos[1] = v.pos[1] * scale.y + offset.y;
            p.pos[2] = v.pos[2] * scale.z + offset.z;
            const float nx = v.normal[0] / std::max(scale.x, 1e-4F);
            const float ny = v.normal[1] / std::max(scale.y, 1e-4F);
            const float nz = v.normal[2] / std::max(scale.z, 1e-4F);
            const float nl = std::sqrt(nx*nx + ny*ny + nz*nz);
            if (nl > 1e-6F) {
                p.normal[0] = nx / nl;
                p.normal[1] = ny / nl;
                p.normal[2] = nz / nl;
            }
            p.uv[0] = v.uv[0] * 0.25F + uv_offset.x;
            p.uv[1] = v.uv[1] * 0.25F + uv_offset.y;
            p.color[0] = 1.0F; p.color[1] = 1.0F; p.color[2] = 1.0F;
            out.vertices.push_back(p);
        }
        for (auto idx : src.indices) {
            if (static_cast<std::uint32_t>(base) + idx > 0xFFFFU) continue;
            out.indices.push_back(static_cast<std::uint16_t>(base + idx));
        }
    };

    const auto sphere_u = make_sphere(20, 28);
    const auto cyl_u    = make_cylinder(24);

    merge(sphere_u, { 0.0F, 1.65F, 0.0F }, { 0.35F, 0.40F, 0.35F }, { 0.00F, 0.00F });
    merge(sphere_u, { 0.0F, 0.95F, 0.0F }, { 0.55F, 0.85F, 0.35F }, { 0.25F, 0.00F });
    merge(sphere_u, { 0.0F, 0.45F, 0.0F }, { 0.42F, 0.30F, 0.32F }, { 0.50F, 0.00F });
    merge(cyl_u,    {-0.55F, 0.85F, 0.0F }, { 0.16F, 0.65F, 0.16F }, { 0.75F, 0.00F });
    merge(cyl_u,    { 0.55F, 0.85F, 0.0F }, { 0.16F, 0.65F, 0.16F }, { 0.00F, 0.25F });
    merge(sphere_u, {-0.55F, 0.50F, 0.0F }, { 0.13F, 0.13F, 0.13F }, { 0.25F, 0.25F });
    merge(sphere_u, { 0.55F, 0.50F, 0.0F }, { 0.13F, 0.13F, 0.13F }, { 0.50F, 0.25F });
    merge(cyl_u,    {-0.20F, 0.00F, 0.0F }, { 0.18F, 0.85F, 0.18F }, { 0.75F, 0.25F });
    merge(cyl_u,    { 0.20F, 0.00F, 0.0F }, { 0.18F, 0.85F, 0.18F }, { 0.00F, 0.50F });
    merge(sphere_u, {-0.20F,-0.45F, 0.06F}, { 0.16F, 0.10F, 0.25F }, { 0.25F, 0.50F });
    merge(sphere_u, { 0.20F,-0.45F, 0.06F}, { 0.16F, 0.10F, 0.25F }, { 0.50F, 0.50F });
    return out;
}

}  // namespace cd::asset
