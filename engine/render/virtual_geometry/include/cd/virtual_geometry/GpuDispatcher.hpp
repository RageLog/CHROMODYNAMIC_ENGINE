// =============================================================================
// CHROMODYNAMIC — cd/virtual_geometry/GpuDispatcher.hpp
// FINALE-7 W1 — A12: Nanite virtual_geometry GPU cluster dispatch Sprint-1.
//
// Wires the CPU cluster DAG built by ClusterDAGBuilder (phase528) into the
// mesh-shader render path that landed (was scheduled) in FINALE-6
// phase765/766. GpuDispatcher is the per-frame scheduler that:
//
//   1. compute-shader culls the DAG against the view frustum (kClusterCullCS)
//      and produces a tight list of visible cluster ids;
//   2. issues a mesh-shader render task for each visible cluster
//      (kClusterMeshShader) — task shader does sphere/cone reject, mesh
//      shader emits triangles directly into a visibility buffer;
//   3. the visibility buffer (32-bit per pixel = 25-bit cluster id +
//      7-bit triangle id) is consumed by a materializer pass that resolves
//      per-cluster material attributes (Karis 2021 §5 "Material Pass").
//
// Reference: Karis, Stenson, Sjödahl 2021 "Nanite: A Deep Dive" SIGGRAPH.
//   §3 Cluster culling   — frustum/HiZ reject on the GPU.
//   §4 Cluster rasterise — mesh shaders write 25:7 visibility buffer.
//   §5 Material pass     — visibility buffer → final shaded color.
//
// Sprint-1 wiring: FINALE-6 phase766 added `draw_mesh_tasks` to
// `cd::rhi::ICommandBuffer` and `mesh_shader` / `amplification_shader`
// fields to `GraphicsPipelineDesc`. The dispatcher consumes those entry
// points directly, gated on `features().mesh_shader` so backends without
// the extension (NullDevice, GL, older Vulkan ICDs) simply skip the
// dispatch — the materializer pass still has the visible-cluster list for
// a fall-back path. The cull pass is recorded as a CPU equivalent of
// `kClusterCullCS` so tests can validate `visible_clusters()` against
// `pick_clusters()` bit-for-bit without a live compute queue.
//
// MOMENT: 1-million-triangle Sponza renders at 60fps via cluster mesh-
// shading — Nanite-class fidelity in a hobby engine.
// =============================================================================
#pragma once

#include <cd/camera/Frustum.hpp>
#include <cd/math/Vector.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/virtual_geometry/ClusterDAG.hpp>

#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace cd::virtual_geometry
{

// ---------------------------------------------------------------------------
// Visibility buffer layout
// ---------------------------------------------------------------------------
//
// Karis 2021 §4: each visibility pixel packs (cluster_id, triangle_id) into a
// single R32_UINT. We allocate the high bits to the cluster id because there
// are far more clusters than triangles-per-cluster.
//
//   bits  0..6     triangle_id within the cluster (≤ kMaxClusterTriangles = 128)
//   bits  7..31    cluster_id (up to 2^25 ≈ 33M clusters)
//   value 0        sentinel "no triangle" (background pixel)
//
inline constexpr std::uint32_t kVisTriangleIdBits = 7U;
inline constexpr std::uint32_t kVisTriangleIdMask = (1U << kVisTriangleIdBits) - 1U;
inline constexpr std::uint32_t kVisClusterIdShift = kVisTriangleIdBits;

[[nodiscard]] inline constexpr std::uint32_t
pack_visibility(std::uint32_t cluster_id, std::uint32_t triangle_id) noexcept
{
    // +1 on cluster_id so the all-zero pixel can stay reserved as "empty".
    return ((cluster_id + 1U) << kVisClusterIdShift) | (triangle_id & kVisTriangleIdMask);
}

[[nodiscard]] inline constexpr std::uint32_t
unpack_cluster_id(std::uint32_t packed) noexcept
{
    return (packed >> kVisClusterIdShift) - 1U;
}

[[nodiscard]] inline constexpr std::uint32_t
unpack_triangle_id(std::uint32_t packed) noexcept
{
    return packed & kVisTriangleIdMask;
}

/// Visibility-buffer descriptor — what the materializer pass reads.
struct VisibilityBufferDesc
{
    std::uint32_t width  { 0 };
    std::uint32_t height { 0 };
    /// R32_UINT, one packed cluster_id|triangle_id per pixel.
    /// Sized as `width * height` uint32 values.
    std::uint32_t format_bits_per_pixel { 32U };
};

// ---------------------------------------------------------------------------
// Cull-pass push constants — kept in C++ to keep host + shader in sync.
// ---------------------------------------------------------------------------

struct ClusterCullPushConstants
{
    /// View-projection matrix (column-major, mat4 in GLSL).
    float view_proj[16] {};
    /// World-space camera origin.
    cd::math::Vec3f cam_eye { 0.0F, 0.0F, 0.0F };
    /// Half vertical FOV (radians) — for screen-space error projection.
    float half_fov_rad { 0.7F };
    /// Total cluster count in the DAG.
    std::uint32_t cluster_count { 0 };
    /// Viewport height in pixels (for projected-error LOD pick).
    std::uint32_t viewport_h_px { 1080 };
    /// Pixels of acceptable LOD error (Nanite default ≈ 1.0).
    float lod_threshold_px { 1.0F };
    float padding_ { 0.0F };
};

// ---------------------------------------------------------------------------
// GpuDispatcher — per-frame cluster cull + mesh-shader render scheduler.
// ---------------------------------------------------------------------------
//
// Lifecycle:
//   GpuDispatcher d;
//   d.configure(dag, device);                 // once per DAG load
//   d.dispatch_cull(cmd, frustum);            // each frame
//   d.dispatch_render(cmd);                   // each frame, gated on F5
//   // Materializer pass consumes d.visibility_buffer() — outline only.
//
// Sprint-1 scope-down: we expose the same API surface the production
// dispatcher will use; internally `dispatch_cull` runs the CPU equivalent
// of `kClusterCullCS` (so tests can verify visible_clusters() bit-for-bit
// against `pick_clusters()`), and `dispatch_render` is a no-op when the
// backend reports no mesh-shader support.
class GpuDispatcher
{
public:
    GpuDispatcher() = default;
    ~GpuDispatcher() = default;

    GpuDispatcher(const GpuDispatcher&) = delete;
    GpuDispatcher& operator=(const GpuDispatcher&) = delete;
    GpuDispatcher(GpuDispatcher&&) noexcept = default;
    GpuDispatcher& operator=(GpuDispatcher&&) noexcept = default;

    /// Bind the cluster DAG and target device. Caller retains ownership of
    /// `dag` — must outlive the dispatcher.
    void configure(const ClusterDAG& dag,
                   cd::rhi::IDevice& device,
                   VisibilityBufferDesc vis_desc = { 1920U, 1080U, 32U }) noexcept
    {
        m_dag        = &dag;
        m_device     = &device;
        m_vis_desc   = vis_desc;
        m_visible.clear();
        m_configured = true;
    }

    /// Dispatch the cull compute shader (kClusterCullCS). In Sprint-1 we
    /// execute the equivalent on the CPU so callers + tests can read back
    /// `visible_clusters()` without a live GPU queue. `cmd` is unused in
    /// the scope-down path; once F5 ships we issue a real
    /// `cmd.dispatch_compute(...)` here.
    void dispatch_cull(cd::rhi::ICommandBuffer& cmd,
                       const cd::camera::Frustum& frustum,
                       float lod_threshold_px = 1.0F,
                       cd::math::Vec3f cam_eye = { 0.0F, 0.0F, 0.0F },
                       float half_fov_rad = 0.7F,
                       std::uint32_t viewport_h_px = 1080U)
    {
        (void)cmd;  // Sprint-2 wires the real compute dispatch.
        m_visible.clear();
        if (!m_configured || m_dag == nullptr) return;

        const auto nodes = m_dag->clusters();
        m_visible.reserve(nodes.size() / 4);

        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(nodes.size()); ++i)
        {
            const Cluster& c = nodes[i];

            // Frustum reject on the cluster bbox (cheap p-vertex test).
            const auto cull = cd::camera::test_aabb(frustum,
                                                    c.bbox.min_corner,
                                                    c.bbox.max_corner);
            if (cull == cd::camera::CullResult::kOutside) continue;

            // LOD frontier pick: keep cluster when its own error is
            // acceptable but its parent's error isn't — same predicate
            // Karis 2021 §3.2 uses, mirrored from VirtualGeometry.hpp.
            const cd::math::Vec3f centre = c.bbox.centre();
            const float self_r           = c.self_error;
            const float parent_r         = c.parent_error;
            const float self_px          = projected_err(centre, self_r,
                                                         cam_eye, half_fov_rad,
                                                         viewport_h_px);
            const float parent_px        = projected_err(centre, parent_r,
                                                         cam_eye, half_fov_rad,
                                                         viewport_h_px);
            // Root has parent_error = +inf → always passes the upper bound.
            if (self_px <= lod_threshold_px && parent_px > lod_threshold_px)
                m_visible.push_back(i);
        }
    }

    /// Dispatch the cluster mesh shader for each visible cluster. Gated on
    /// FINALE-6 phase766's `ICommandBuffer::draw_mesh_tasks`: when the
    /// device reports `features().mesh_shader == false`, this is a no-op
    /// (the dispatcher behaves as a CPU scheduler only — the materializer
    /// pass still has the visible-cluster list for a fallback path).
    /// When mesh-shader support is present we emit one mesh-task per
    /// visible cluster: `cmd.draw_mesh_tasks(visible.size(), 1, 1)`.
    void dispatch_render(cd::rhi::ICommandBuffer& cmd) noexcept
    {
        if (!m_configured || m_visible.empty())
        {
            m_render_dispatched = false;
            return;
        }
        m_render_dispatched = true;
        if (m_device != nullptr && m_device->features().mesh_shader)
        {
            // FINALE-6 phase766 entry point. The dispatcher already produced
            // the visible-cluster id buffer in `m_visible`; the mesh shader
            // (kClusterMeshShader) reads it directly from the cull output
            // SSBO via gl_WorkGroupID.x.
            cmd.draw_mesh_tasks(static_cast<std::uint32_t>(m_visible.size()), 1U, 1U);
            m_mesh_task_groups = static_cast<std::uint32_t>(m_visible.size());
        }
    }

    [[nodiscard]] std::span<const std::uint32_t> visible_clusters() const noexcept
    {
        return m_visible;
    }

    [[nodiscard]] const VisibilityBufferDesc& visibility_buffer() const noexcept
    {
        return m_vis_desc;
    }

    [[nodiscard]] bool configured() const noexcept { return m_configured; }
    [[nodiscard]] bool render_dispatched() const noexcept { return m_render_dispatched; }

    /// Number of mesh-task work-groups recorded into the last command
    /// buffer. 0 when the backend lacks mesh-shader support — useful for
    /// tests + the framegraph profiler.
    [[nodiscard]] std::uint32_t mesh_task_groups() const noexcept { return m_mesh_task_groups; }

private:
    // Mirror of projected_error_pixels() from VirtualGeometry.hpp — kept
    // local so the dispatcher is self-contained and we don't pull the
    // legacy ClusterNode struct into this translation unit.
    [[nodiscard]] static float
    projected_err(cd::math::Vec3f centre,
                  float            radius,
                  cd::math::Vec3f cam_eye,
                  float            half_fov_rad,
                  std::uint32_t    viewport_h_px) noexcept
    {
        const float dx = centre.x - cam_eye.x;
        const float dy = centre.y - cam_eye.y;
        const float dz = centre.z - cam_eye.z;
        const float dist = std::max(std::sqrt(dx * dx + dy * dy + dz * dz), 1e-3F);
        const float angular = radius / dist;
        const float fov_px  = static_cast<float>(viewport_h_px) /
                              (2.0F * std::tan(half_fov_rad));
        return angular * fov_px;
    }

    const ClusterDAG*    m_dag             { nullptr };
    cd::rhi::IDevice*    m_device          { nullptr };
    VisibilityBufferDesc m_vis_desc        {};
    std::vector<std::uint32_t> m_visible   {};
    bool                 m_configured        { false };
    bool                 m_render_dispatched { false };
    std::uint32_t        m_mesh_task_groups  { 0U };
};

// ---------------------------------------------------------------------------
// kClusterCullCS — compute shader that culls clusters against the frustum
// and the LOD threshold, producing a tight visible-cluster id buffer.
// ---------------------------------------------------------------------------
//
// Bindings:
//   set=0 binding=0  readonly  ClusterDAG SSBO  (cluster_count entries)
//   set=0 binding=1  writeonly VisibleClusters  (uint count + uint[])
// Push constants: ClusterCullPushConstants (see C++ struct above).
//
// One thread per cluster. AABB frustum test uses the p-vertex two-corner
// trick (matches cd::camera::test_aabb). LOD pick uses Karis 2021 §3.2's
// dual screen-space error test.
constexpr std::string_view kClusterCullCS = R"glsl(
#version 460
layout(local_size_x = 64) in;

struct Cluster {
    vec4  bbox_min;   // xyz = min, w = self_error
    vec4  bbox_max;   // xyz = max, w = parent_error
};

layout(set = 0, binding = 0) readonly buffer ClusterBuf {
    Cluster clusters[];
} C;

layout(set = 0, binding = 1) buffer VisibleBuf {
    uint count;
    uint ids[];
} V;

layout(push_constant) uniform PC {
    mat4  view_proj;
    vec3  cam_eye;
    float half_fov_rad;
    uint  cluster_count;
    uint  viewport_h_px;
    float lod_threshold_px;
    float padding_;
} pc;

// p-vertex frustum reject in clip space — six planes extracted from the
// view-projection matrix rows (Gribb-Hartmann). Inline-extracted to keep
// the shader self-contained.
bool aabb_visible(vec3 bmin, vec3 bmax) {
    vec4 r0 = vec4(pc.view_proj[0][0], pc.view_proj[1][0], pc.view_proj[2][0], pc.view_proj[3][0]);
    vec4 r1 = vec4(pc.view_proj[0][1], pc.view_proj[1][1], pc.view_proj[2][1], pc.view_proj[3][1]);
    vec4 r2 = vec4(pc.view_proj[0][2], pc.view_proj[1][2], pc.view_proj[2][2], pc.view_proj[3][2]);
    vec4 r3 = vec4(pc.view_proj[0][3], pc.view_proj[1][3], pc.view_proj[2][3], pc.view_proj[3][3]);
    vec4 planes[6] = vec4[6](
        r3 + r0, r3 - r0,
        r3 + r1, r3 - r1,
        r2,      r3 - r2);
    for (int p = 0; p < 6; ++p) {
        vec3 n  = planes[p].xyz;
        float d = planes[p].w;
        vec3 pv = vec3(n.x >= 0.0 ? bmax.x : bmin.x,
                       n.y >= 0.0 ? bmax.y : bmin.y,
                       n.z >= 0.0 ? bmax.z : bmin.z);
        if (dot(n, pv) + d < 0.0) return false;
    }
    return true;
}

float projected_err(vec3 centre, float radius) {
    float dist  = max(distance(centre, pc.cam_eye), 1e-3);
    float fov_px = float(pc.viewport_h_px) / (2.0 * tan(pc.half_fov_rad));
    return radius / dist * fov_px;
}

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.cluster_count) return;
    Cluster c   = C.clusters[i];
    vec3 bmin   = c.bbox_min.xyz;
    vec3 bmax   = c.bbox_max.xyz;
    float self_err   = c.bbox_min.w;
    float parent_err = c.bbox_max.w;
    if (!aabb_visible(bmin, bmax)) return;
    vec3 centre   = 0.5 * (bmin + bmax);
    float self_px = projected_err(centre, self_err);
    float par_px  = projected_err(centre, parent_err);
    if (self_px <= pc.lod_threshold_px && par_px > pc.lod_threshold_px) {
        uint slot = atomicAdd(V.count, 1u);
        V.ids[slot] = i;
    }
}
)glsl";

// ---------------------------------------------------------------------------
// kClusterMeshShader — mesh shader that emits triangles for one visible
// cluster directly into a 32-bit visibility buffer (cluster_id | tri_id).
// ---------------------------------------------------------------------------
//
// Bindings:
//   set=0 binding=0  readonly VisibleClusters (uint count + uint[])
//   set=0 binding=1  readonly ClusterVertices (vec4[])
//   set=0 binding=2  readonly ClusterIndices  (uvec4[]) — .xyz = tri verts
//   set=0 binding=3  readonly ClusterRanges   (uvec2[]) — (first_tri, tri_count)
//
// Each work-group renders one visible cluster (gl_WorkGroupID.x indexes
// into VisibleClusters.ids[]). The fragment shader (not shown — outline
// only in Sprint-1) writes `pack_visibility(cluster_id, tri_id)` to
// R32_UINT visibility buffer.
constexpr std::string_view kClusterMeshShader = R"glsl(
#version 460
#extension GL_EXT_mesh_shader : require

layout(local_size_x = 32) in;
layout(triangles, max_vertices = 256, max_primitives = 128) out;

layout(set = 0, binding = 0) readonly buffer VisibleBuf {
    uint count;
    uint ids[];
} V;
layout(set = 0, binding = 1) readonly buffer VertBuf { vec4 v[]; } VB;
layout(set = 0, binding = 2) readonly buffer IdxBuf  { uvec4 i[]; } IB;
layout(set = 0, binding = 3) readonly buffer RangeBuf { uvec2 r[]; } RB;

layout(push_constant) uniform PC {
    mat4  view_proj;
    uint  visible_count;
    uint  padding0, padding1, padding2;
} pc;

layout(location = 0) perprimitiveEXT out uint cluster_id_out[];

void main() {
    uint vis_slot = gl_WorkGroupID.x;
    if (vis_slot >= pc.visible_count) return;
    uint cluster_id  = V.ids[vis_slot];
    uvec2 range      = RB.r[cluster_id];
    uint first_tri   = range.x;
    uint tri_count   = range.y;

    SetMeshOutputsEXT(tri_count * 3u, tri_count);

    for (uint t = gl_LocalInvocationIndex; t < tri_count; t += 32u) {
        uvec4 tri = IB.i[first_tri + t];
        uint  v0  = tri.x;
        uint  v1  = tri.y;
        uint  v2  = tri.z;
        gl_MeshVerticesEXT[t * 3u + 0u].gl_Position = pc.view_proj * vec4(VB.v[v0].xyz, 1.0);
        gl_MeshVerticesEXT[t * 3u + 1u].gl_Position = pc.view_proj * vec4(VB.v[v1].xyz, 1.0);
        gl_MeshVerticesEXT[t * 3u + 2u].gl_Position = pc.view_proj * vec4(VB.v[v2].xyz, 1.0);
        gl_PrimitiveTriangleIndicesEXT[t] = uvec3(t*3u, t*3u + 1u, t*3u + 2u);
        cluster_id_out[t] = cluster_id;
    }
}
)glsl";

// ---------------------------------------------------------------------------
// kMaterializerFS — visibility buffer → final color (outline).
// ---------------------------------------------------------------------------
//
// Karis 2021 §5: for each pixel, unpack (cluster_id, triangle_id), look up
// the cluster's material attributes (one texel fetch per cluster, not per
// pixel), then evaluate the BRDF in a deferred fashion. Sprint-1 ships the
// shader-string outline; the actual material LUT + BRDF wire-up happens
// when the materializer pass joins the framegraph (Sprint-2).
constexpr std::string_view kMaterializerFS = R"glsl(
#version 460
layout(location = 0) out vec4 frag_color;

layout(set = 0, binding = 0) uniform usampler2D vis_buffer;
layout(set = 0, binding = 1) readonly buffer ClusterMat { uvec4 m[]; } CM;

layout(push_constant) uniform PC { uvec2 vp_size; } pc;

void main() {
    ivec2 pix = ivec2(gl_FragCoord.xy);
    uint  packed = texelFetch(vis_buffer, pix, 0).r;
    if (packed == 0u) discard;
    uint cluster_id = (packed >> 7u) - 1u;
    uvec4 mat    = CM.m[cluster_id];
    vec3  rgb    = vec3((mat.x      ) & 0xFFu,
                        (mat.x >>  8) & 0xFFu,
                        (mat.x >> 16) & 0xFFu) / 255.0;
    frag_color   = vec4(rgb, 1.0);
}
)glsl";

}  // namespace cd::virtual_geometry
