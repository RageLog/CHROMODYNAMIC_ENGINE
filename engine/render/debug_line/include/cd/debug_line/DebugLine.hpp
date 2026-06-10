// =============================================================================
// CHROMODYNAMIC — cd/debug_line/DebugLine.hpp
// phase1030 — Debug line batch builder (CPU side).
//
// Run 27-29 shipped 19 3D-viewport debug demos on a sphere-proxy
// template because the engine had no line-list path; the research
// survey (docs/RESEARCH_3D_VIEWPORT_DEBUG_VIZ.md §12 Tier-2) calls
// out a dedicated line renderer as the canonical upgrade. This
// library is the RHI-INDEPENDENT half of that upgrade: a CPU batch
// accumulator that turns debug shapes (AABB / OBB / frustum / circle
// / polyline / cross) into a flat kLineList vertex stream.
//
// Design notes:
//   * No RHI dependency — the consumer (sample or a future
//     cd::debug_draw GPU layer) owns the pipeline + vertex buffer
//     and just uploads `vertices()` each frame. This keeps the
//     library standalone-testable per the project modularity rule.
//   * Vertex = position + RGBA colour. 2 vertices per line segment,
//     matching cd::rhi::PrimitiveTopology::kLineList.
//   * All shape helpers append; `clear()` resets for the next frame
//     (capacity is retained to avoid per-frame reallocation).
//
// References:
//   * bgfx DebugDrawEncoder — immediate-mode debug shape API.
//   * Bevy Gizmos — per-frame retained line batch.
// =============================================================================
#pragma once

#include <cd/math/Matrix.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

namespace cd::debug_line
{

/// One line-list vertex. Tightly packed: 28 bytes (3 + 4 floats).
/// The colour rides per-vertex so a single batch can mix shapes of
/// different colours in one draw.
struct LineVertex
{
    cd::math::Vec3f position { 0.0F, 0.0F, 0.0F };
    cd::math::Vec4f color    { 1.0F, 1.0F, 1.0F, 1.0F };
};

/// CPU-side accumulator for kLineList debug geometry.
///
/// Usage per frame:
///   batch.clear();
///   batch.add_aabb(bmin, bmax, {0, 1, 0, 1});
///   upload(batch.vertices());            // consumer-owned VB
///   cmd.draw(batch.vertex_count(), ...); // kLineList pipeline
class LineBatch
{
public:
    /// Reset the batch for a new frame. Keeps the allocation.
    void clear() noexcept { verts_.clear(); }

    /// Append one segment from `a` to `b`.
    void add_line(const cd::math::Vec3f& a,
                  const cd::math::Vec3f& b,
                  const cd::math::Vec4f& color)
    {
        verts_.push_back({ a, color });
        verts_.push_back({ b, color });
    }

    /// Append the 12 edges of an axis-aligned box. Degenerate boxes
    /// (min == max on an axis) are allowed and draw flat rectangles
    /// or single lines; min/max are normalised per component so a
    /// swapped pair is not a caller error.
    void add_aabb(const cd::math::Vec3f& a,
                  const cd::math::Vec3f& b,
                  const cd::math::Vec4f& color)
    {
        const cd::math::Vec3f mn { std::min(a.x, b.x),
                                   std::min(a.y, b.y),
                                   std::min(a.z, b.z) };
        const cd::math::Vec3f mx { std::max(a.x, b.x),
                                   std::max(a.y, b.y),
                                   std::max(a.z, b.z) };
        const std::array<cd::math::Vec3f, 8> c {{
            { mn.x, mn.y, mn.z }, { mx.x, mn.y, mn.z },
            { mx.x, mx.y, mn.z }, { mn.x, mx.y, mn.z },
            { mn.x, mn.y, mx.z }, { mx.x, mn.y, mx.z },
            { mx.x, mx.y, mx.z }, { mn.x, mx.y, mx.z } }};
        add_box_edges_(c, color);
    }

    /// Append the 12 edges of an oriented box given centre, the
    /// three (assumed orthonormal) axes and per-axis half extents.
    /// Axis convention matches cd::decal::Decal (right/up/forward).
    void add_obb(const cd::math::Vec3f& centre,
                 const cd::math::Vec3f& right,
                 const cd::math::Vec3f& up,
                 const cd::math::Vec3f& forward,
                 const cd::math::Vec3f& half_extents,
                 const cd::math::Vec4f& color)
    {
        std::array<cd::math::Vec3f, 8> c {};
        std::size_t i = 0;
        // Corner order mirrors add_aabb's (z-major, then y, then x)
        // so add_box_edges_ shares the same edge index table.
        for (int sz = -1; sz <= 1; sz += 2)
        for (int sy = -1; sy <= 1; sy += 2)
        for (int sx = -1; sx <= 1; sx += 2)
        {
            const float fx = static_cast<float>(sx) * half_extents.x;
            const float fy = static_cast<float>(sy) * half_extents.y;
            const float fz = static_cast<float>(sz) * half_extents.z;
            c[i++] = {
                centre.x + right.x * fx + up.x * fy + forward.x * fz,
                centre.y + right.y * fx + up.y * fy + forward.y * fz,
                centre.z + right.z * fx + up.z * fy + forward.z * fz };
        }
        // Remap from the loop's (-,-,-),(+,-,-),(-,+,-),(+,+,-),...
        // ordering to add_box_edges_'s ring ordering.
        const std::array<cd::math::Vec3f, 8> ring {{
            c[0], c[1], c[3], c[2],   // near ring  (z = -1)
            c[4], c[5], c[7], c[6] }};// far ring   (z = +1)
        add_box_edges_(ring, color);
    }

    /// Append the 12 edges of a view frustum recovered from an
    /// inverse view-projection matrix. `ndc_z_near` / `ndc_z_far`
    /// select the depth convention: Vulkan/D3D = (0, 1) — the
    /// default — GL-style = (-1, 1).
    void add_frustum(const cd::math::Mat4f& inv_view_proj,
                     const cd::math::Vec4f& color,
                     float ndc_z_near = 0.0F,
                     float ndc_z_far  = 1.0F)
    {
        const auto unproject = [&](float x, float y, float z) {
            const cd::math::Vec4f ndc { x, y, z, 1.0F };
            const cd::math::Vec4f w = inv_view_proj * ndc;
            const float inv_w = (std::abs(w.w) > 1e-6F) ? 1.0F / w.w : 0.0F;
            return cd::math::Vec3f { w.x * inv_w, w.y * inv_w, w.z * inv_w };
        };
        const std::array<cd::math::Vec3f, 8> ring {{
            unproject(-1.0F, -1.0F, ndc_z_near),
            unproject( 1.0F, -1.0F, ndc_z_near),
            unproject( 1.0F,  1.0F, ndc_z_near),
            unproject(-1.0F,  1.0F, ndc_z_near),
            unproject(-1.0F, -1.0F, ndc_z_far),
            unproject( 1.0F, -1.0F, ndc_z_far),
            unproject( 1.0F,  1.0F, ndc_z_far),
            unproject(-1.0F,  1.0F, ndc_z_far) }};
        add_box_edges_(ring, color);
    }

    /// Append an N-segment circle of `radius` around `centre`, lying
    /// in the plane perpendicular to `axis`. Segments are clamped to
    /// a minimum of 3. A zero axis falls back to +Y.
    void add_circle(const cd::math::Vec3f& centre,
                    const cd::math::Vec3f& axis,
                    float radius,
                    int segments,
                    const cd::math::Vec4f& color)
    {
        segments = std::max(segments, 3);
        // Orthonormal basis around the axis (branch-free enough for
        // a debug path; production ONB lives in the shading code).
        cd::math::Vec3f n = axis;
        const float nl = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (nl < 1e-6F) { n = { 0.0F, 1.0F, 0.0F }; }
        else            { n = { n.x / nl, n.y / nl, n.z / nl }; }
        const cd::math::Vec3f helper =
            (std::abs(n.y) < 0.99F) ? cd::math::Vec3f { 0.0F, 1.0F, 0.0F }
                                    : cd::math::Vec3f { 1.0F, 0.0F, 0.0F };
        cd::math::Vec3f t {
            n.y * helper.z - n.z * helper.y,
            n.z * helper.x - n.x * helper.z,
            n.x * helper.y - n.y * helper.x };
        const float tl = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z);
        t = { t.x / tl, t.y / tl, t.z / tl };
        const cd::math::Vec3f b {
            n.y * t.z - n.z * t.y,
            n.z * t.x - n.x * t.z,
            n.x * t.y - n.y * t.x };
        constexpr float kTwoPi = 6.28318530717958647692F;
        cd::math::Vec3f prev {
            centre.x + t.x * radius,
            centre.y + t.y * radius,
            centre.z + t.z * radius };
        for (int i = 1; i <= segments; ++i)
        {
            const float ang = kTwoPi * static_cast<float>(i) /
                              static_cast<float>(segments);
            const float ca = std::cos(ang);
            const float sa = std::sin(ang);
            const cd::math::Vec3f cur {
                centre.x + (t.x * ca + b.x * sa) * radius,
                centre.y + (t.y * ca + b.y * sa) * radius,
                centre.z + (t.z * ca + b.z * sa) * radius };
            add_line(prev, cur, color);
            prev = cur;
        }
    }

    /// Append a connected polyline through `points` (size < 2 is a
    /// no-op). N points produce N-1 segments.
    void add_polyline(std::span<const cd::math::Vec3f> points,
                      const cd::math::Vec4f& color)
    {
        if (points.size() < 2) return;
        for (std::size_t i = 1; i < points.size(); ++i)
            add_line(points[i - 1], points[i], color);
    }

    /// Append a 3-axis cross of total width `2 * half_size` at
    /// `centre` — the classic point marker.
    void add_cross(const cd::math::Vec3f& centre,
                   float half_size,
                   const cd::math::Vec4f& color)
    {
        add_line({ centre.x - half_size, centre.y, centre.z },
                 { centre.x + half_size, centre.y, centre.z }, color);
        add_line({ centre.x, centre.y - half_size, centre.z },
                 { centre.x, centre.y + half_size, centre.z }, color);
        add_line({ centre.x, centre.y, centre.z - half_size },
                 { centre.x, centre.y, centre.z + half_size }, color);
    }

    [[nodiscard]] std::span<const LineVertex> vertices() const noexcept
    {
        return { verts_.data(), verts_.size() };
    }

    [[nodiscard]] std::size_t vertex_count() const noexcept
    {
        return verts_.size();
    }

    [[nodiscard]] std::size_t line_count() const noexcept
    {
        return verts_.size() / 2U;
    }

    [[nodiscard]] bool empty() const noexcept { return verts_.empty(); }

private:
    /// Shared 12-edge emitter. `ring` is 2 quads: indices 0-3 the
    /// near ring (counter-clockwise), 4-7 the far ring.
    void add_box_edges_(const std::array<cd::math::Vec3f, 8>& ring,
                        const cd::math::Vec4f& color)
    {
        constexpr std::array<std::array<std::size_t, 2>, 12> kEdges {{
            { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },   // near ring
            { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },   // far ring
            { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } }};// connectors
        for (const auto& e : kEdges)
            add_line(ring[e[0]], ring[e[1]], color);
    }

    std::vector<LineVertex> verts_;
};

}  // namespace cd::debug_line
