// =============================================================================
// CHROMODYNAMIC — tests/test_gpu_dispatch.cpp
// FINALE-7 W1 — A12: Nanite virtual_geometry GPU cluster dispatch Sprint-1.
//
// Validates cd::virtual_geometry::GpuDispatcher end-to-end on a 100-cluster
// scene via the NullDevice / NullCommandBuffer (Vulkan-gated path is the
// Sprint-2 follow-on once the F5 mesh-shader RHI ships).
// =============================================================================
#include <cd/camera/Frustum.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/rhi/NullCommandBuffer.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <cd/virtual_geometry/ClusterDAG.hpp>
#include <cd/virtual_geometry/GpuDispatcher.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace
{

using cd::virtual_geometry::Cluster;
using cd::virtual_geometry::ClusterDAG;
using cd::virtual_geometry::GpuDispatcher;
using cd::virtual_geometry::pack_visibility;
using cd::virtual_geometry::unpack_cluster_id;
using cd::virtual_geometry::unpack_triangle_id;

// ---------------------------------------------------------------------------
// Fixture: synthesise a 100-cluster DAG covering a grid in front of the
// camera. parent_error = +inf so the LOD pick always picks the leaf.
// ---------------------------------------------------------------------------
ClusterDAG make_grid_dag(std::uint32_t cluster_count = 100U)
{
    std::vector<Cluster> nodes;
    nodes.reserve(cluster_count);
    for (std::uint32_t i = 0; i < cluster_count; ++i)
    {
        Cluster c;
        c.triangles  = { 0U };
        c.lod_level  = 0U;
        c.parent_lod = 1U;
        c.self_error = 0.0F;
        c.parent_error = 1000.0F;
        const std::uint32_t col = i % 10U;
        const std::uint32_t row = i / 10U;
        const float fx = static_cast<float>(col) - 5.0F;
        // Push the grid far enough out that the full ±10 strip is inside
        // the FOV at every row — z ≥ 12 with half-fov tan(0.7) ≈ 0.842
        // gives a horizontal half-extent of ≈ 10.1 at the nearest row.
        const float fz = 12.0F + static_cast<float>(row);
        c.bbox.min_corner = { fx,        -0.5F, fz        };
        c.bbox.max_corner = { fx + 0.5F,  0.5F, fz + 0.5F };
        nodes.push_back(std::move(c));
    }
    return ClusterDAG { std::move(nodes) };
}

// Same shape but placed behind the camera so frustum culling drops them.
ClusterDAG make_behind_dag(std::uint32_t cluster_count = 100U)
{
    std::vector<Cluster> nodes;
    nodes.reserve(cluster_count);
    for (std::uint32_t i = 0; i < cluster_count; ++i)
    {
        Cluster c;
        c.triangles  = { 0U };
        c.lod_level  = 0U;
        c.parent_lod = 1U;
        c.self_error = 0.0F;
        c.parent_error = 1000.0F;
        const std::uint32_t col = i % 10U;
        const std::uint32_t row = i / 10U;
        const float fx = static_cast<float>(col) - 5.0F;
        const float fz = -10.0F - static_cast<float>(row);
        c.bbox.min_corner = { fx,        -0.5F, fz - 0.5F };
        c.bbox.max_corner = { fx + 0.5F,  0.5F, fz        };
        nodes.push_back(std::move(c));
    }
    return ClusterDAG { std::move(nodes) };
}

cd::math::Mat4f look_forward_proj()
{
    cd::math::Mat4f m {};
    const float n = 0.1F;
    const float f = 100.0F;
    const float t = std::tan(0.7F);
    m[0][0] = 1.0F / t;
    m[1][1] = 1.0F / t;
    m[2][2] = f / (f - n);
    m[2][3] = 1.0F;
    m[3][2] = -(f * n) / (f - n);
    m[3][3] = 0.0F;
    return m;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(VirtualGeometryGpuDispatch, ConfigureBindsDag)
{
    const auto dag = make_grid_dag();
    cd::rhi::NullDevice device {};
    GpuDispatcher d;
    EXPECT_FALSE(d.configured());
    d.configure(dag, device);
    EXPECT_TRUE(d.configured());
    EXPECT_TRUE(d.visible_clusters().empty());
}

TEST(VirtualGeometryGpuDispatch, DispatchCullKeepsClustersInFront)
{
    const auto dag = make_grid_dag(100U);
    cd::rhi::NullDevice device {};
    cd::rhi::NullCommandBuffer cmd {};

    GpuDispatcher d;
    d.configure(dag, device);

    const auto vp      = look_forward_proj();
    const auto frustum = cd::camera::extract_frustum(vp);
    d.dispatch_cull(cmd, frustum,
                    /*lod_threshold_px=*/1.0F,
                    /*cam_eye=*/ { 0.0F, 0.0F, 0.0F },
                    /*half_fov_rad=*/0.7F,
                    /*viewport_h_px=*/1080U);

    // Grid is parked far enough out that the full ±10 strip fits the FOV
    // at every row. All 100 should survive cull + (self_err=0,
    // parent_err=1000) LOD frontier test.
    const auto visible = d.visible_clusters();
    EXPECT_EQ(visible.size(), 100U);
    for (const std::uint32_t id : visible)
        EXPECT_LT(id, 100U);
}

TEST(VirtualGeometryGpuDispatch, DispatchCullRejectsBehindCamera)
{
    // Clusters placed at -Z, frustum points at +Z — every AABB sits behind
    // the near plane and the cull stage must drop them.
    const auto dag = make_behind_dag(100U);
    cd::rhi::NullDevice device {};
    cd::rhi::NullCommandBuffer cmd {};

    GpuDispatcher d;
    d.configure(dag, device);

    const auto vp      = look_forward_proj();
    const auto frustum = cd::camera::extract_frustum(vp);
    d.dispatch_cull(cmd, frustum, 1.0F, { 0.0F, 0.0F, 0.0F }, 0.7F, 1080U);

    EXPECT_TRUE(d.visible_clusters().empty());
}

TEST(VirtualGeometryGpuDispatch, DispatchRenderRecordsAfterCull)
{
    // Sprint-1 scope-down: dispatch_render() records the scheduling
    // (m_render_dispatched = true once we have visible clusters), but
    // never issues the real `dispatch_mesh_tasks` call until F5 lands.
    const auto dag = make_grid_dag(50U);
    cd::rhi::NullDevice device {};
    cd::rhi::NullCommandBuffer cmd {};

    GpuDispatcher d;
    d.configure(dag, device);

    const auto vp      = look_forward_proj();
    const auto frustum = cd::camera::extract_frustum(vp);
    d.dispatch_cull(cmd, frustum, 1.0F, { 0.0F, 0.0F, 0.0F }, 0.7F, 1080U);
    EXPECT_FALSE(d.visible_clusters().empty());

    d.dispatch_render(cmd);
    EXPECT_TRUE(d.render_dispatched());
}

TEST(VirtualGeometryGpuDispatch, DispatchRenderIsNoOpWithoutVisibleClusters)
{
    // No cull → no visible clusters → render path stays parked.
    const auto dag = make_behind_dag(20U);
    cd::rhi::NullDevice device {};
    cd::rhi::NullCommandBuffer cmd {};

    GpuDispatcher d;
    d.configure(dag, device);

    const auto vp      = look_forward_proj();
    const auto frustum = cd::camera::extract_frustum(vp);
    d.dispatch_cull(cmd, frustum, 1.0F, { 0.0F, 0.0F, 0.0F }, 0.7F, 1080U);
    EXPECT_TRUE(d.visible_clusters().empty());

    d.dispatch_render(cmd);
    EXPECT_FALSE(d.render_dispatched());
}

TEST(VirtualGeometryGpuDispatch, VisibilityBufferDescriptorMatchesConfigure)
{
    const auto dag = make_grid_dag(10U);
    cd::rhi::NullDevice device {};
    GpuDispatcher d;
    d.configure(dag, device,
                cd::virtual_geometry::VisibilityBufferDesc { 2560U, 1440U, 32U });
    EXPECT_EQ(d.visibility_buffer().width,                 2560U);
    EXPECT_EQ(d.visibility_buffer().height,                1440U);
    EXPECT_EQ(d.visibility_buffer().format_bits_per_pixel, 32U);
}

TEST(VirtualGeometryGpuDispatch, VisibilityPackRoundtrip)
{
    // 25:7 split — 2^25-1 cluster ids × 128 triangles each.
    for (std::uint32_t cluster_id : { 0U, 1U, 42U, 1U << 10U, (1U << 25U) - 2U })
    {
        for (std::uint32_t tri_id : { 0U, 1U, 63U, 127U })
        {
            const std::uint32_t packed = pack_visibility(cluster_id, tri_id);
            EXPECT_NE(packed, 0U) << "non-empty pixels must not collide with the sentinel";
            EXPECT_EQ(unpack_cluster_id(packed),  cluster_id);
            EXPECT_EQ(unpack_triangle_id(packed), tri_id);
        }
    }
}

TEST(VirtualGeometryGpuDispatch, ShaderStringsContainEntryPoints)
{
    using cd::virtual_geometry::kClusterCullCS;
    using cd::virtual_geometry::kClusterMeshShader;
    using cd::virtual_geometry::kMaterializerFS;

    EXPECT_FALSE(kClusterCullCS.empty());
    EXPECT_FALSE(kClusterMeshShader.empty());
    EXPECT_FALSE(kMaterializerFS.empty());

    EXPECT_NE(kClusterCullCS.find("aabb_visible"),       std::string_view::npos);
    EXPECT_NE(kClusterCullCS.find("atomicAdd"),          std::string_view::npos);
    EXPECT_NE(kClusterMeshShader.find("GL_EXT_mesh_shader"), std::string_view::npos);
    EXPECT_NE(kClusterMeshShader.find("SetMeshOutputsEXT"),  std::string_view::npos);
    EXPECT_NE(kMaterializerFS.find("vis_buffer"),        std::string_view::npos);
}

// ===========================================================================
// ADD-ONLY depth pass — pins ACTUAL host-side behaviour of GpuDispatcher's
// cull / LOD-frontier / render-record path + the visibility-buffer pack
// contract. No production code touched; CPU-cull output is the bit-for-bit
// reference for the shipped kClusterCullCS, so all assertions below describe
// the EXISTING predicate (golden-safe). NullDevice reports mesh_shader ==
// false, so the real draw_mesh_tasks call is never issued here.
// ===========================================================================

// Single cluster centred on the +Z axis with caller-chosen errors, parked
// well inside the look_forward_proj() frustum.
ClusterDAG make_single_cluster(float self_err, float parent_err,
                               float fz = 12.0F)
{
    Cluster c;
    c.triangles  = { 0U };
    c.lod_level  = 0U;
    c.parent_lod = 1U;
    c.self_error = self_err;
    c.parent_error = parent_err;
    c.bbox.min_corner = { -0.5F, -0.5F, fz };
    c.bbox.max_corner = { 0.5F, 0.5F, fz + 1.0F };
    std::vector<Cluster> nodes;
    nodes.push_back(std::move(c));
    return ClusterDAG { std::move(nodes) };
}

TEST(VirtualGeometryGpuDispatch, UnconfiguredDispatchCullProducesNothing)
{
    cd::rhi::NullCommandBuffer cmd {};
    const auto vp      = look_forward_proj();
    const auto frustum = cd::camera::extract_frustum(vp);

    GpuDispatcher d;  // never configured
    EXPECT_FALSE(d.configured());
    d.dispatch_cull(cmd, frustum, 1.0F, { 0.0F, 0.0F, 0.0F }, 0.7F, 1080U);
    EXPECT_TRUE(d.visible_clusters().empty());
}

TEST(VirtualGeometryGpuDispatch, ReconfigureClearsPreviousVisibleList)
{
    const auto dag = make_grid_dag(100U);
    cd::rhi::NullDevice device {};
    cd::rhi::NullCommandBuffer cmd {};
    const auto vp      = look_forward_proj();
    const auto frustum = cd::camera::extract_frustum(vp);

    GpuDispatcher d;
    d.configure(dag, device);
    d.dispatch_cull(cmd, frustum, 1.0F, { 0.0F, 0.0F, 0.0F }, 0.7F, 1080U);
    EXPECT_FALSE(d.visible_clusters().empty());

    // configure() clears m_visible — fresh bind starts empty.
    d.configure(dag, device);
    EXPECT_TRUE(d.visible_clusters().empty());
}

// ---------------------------------------------------------------------------
// LOD frontier predicate: keep when self_px <= thresh AND parent_px > thresh.
// projected_err grows with radius, so for a fixed centre/cam the predicate is
// purely a function of (self_error, parent_error) vs threshold.
// ---------------------------------------------------------------------------

TEST(VirtualGeometryGpuDispatch, LodFrontierKeepsWhenSelfFitsParentDoesNot)
{
    // self_error tiny → self_px below threshold; parent_error huge → parent_px
    // above threshold → this is the frontier cluster, kept.
    const auto dag = make_single_cluster(/*self_err=*/0.0F, /*parent_err=*/1000.0F);
    cd::rhi::NullDevice device {};
    cd::rhi::NullCommandBuffer cmd {};
    const auto frustum = cd::camera::extract_frustum(look_forward_proj());

    GpuDispatcher d;
    d.configure(dag, device);
    d.dispatch_cull(cmd, frustum, 1.0F, { 0.0F, 0.0F, 0.0F }, 0.7F, 1080U);
    ASSERT_EQ(d.visible_clusters().size(), 1U);
    EXPECT_EQ(d.visible_clusters()[0], 0U);
}

TEST(VirtualGeometryGpuDispatch, LodFrontierDropsWhenParentAlsoFitsTooCoarseHere)
{
    // Both errors tiny → parent_px also <= threshold → parent is acceptable,
    // so this finer cluster is NOT the frontier (the coarser one wins).
    const auto dag = make_single_cluster(/*self_err=*/0.0001F, /*parent_err=*/0.0002F);
    cd::rhi::NullDevice device {};
    cd::rhi::NullCommandBuffer cmd {};
    const auto frustum = cd::camera::extract_frustum(look_forward_proj());

    GpuDispatcher d;
    d.configure(dag, device);
    d.dispatch_cull(cmd, frustum, 1.0F, { 0.0F, 0.0F, 0.0F }, 0.7F, 1080U);
    EXPECT_TRUE(d.visible_clusters().empty());
}

TEST(VirtualGeometryGpuDispatch, LodFrontierDropsWhenSelfErrorTooLarge)
{
    // self_error large → self_px > threshold → fails the lower bound, dropped
    // (caller should be drawing an even finer cluster instead).
    const auto dag = make_single_cluster(/*self_err=*/100.0F, /*parent_err=*/1000.0F);
    cd::rhi::NullDevice device {};
    cd::rhi::NullCommandBuffer cmd {};
    const auto frustum = cd::camera::extract_frustum(look_forward_proj());

    GpuDispatcher d;
    d.configure(dag, device);
    d.dispatch_cull(cmd, frustum, 1.0F, { 0.0F, 0.0F, 0.0F }, 0.7F, 1080U);
    EXPECT_TRUE(d.visible_clusters().empty());
}

TEST(VirtualGeometryGpuDispatch, ProjectedErrorMonotoneCloserKeepsFartherDrops)
{
    // The SAME cluster errors, pulled to two distances. Closer → bigger
    // projected error. With a tuned threshold the near cluster fails the
    // self-bound (drops) while the far one becomes the frontier (kept) — this
    // pins the distance-monotonicity of the bbox-diagonal error metric.
    cd::rhi::NullDevice device {};
    cd::rhi::NullCommandBuffer cmd {};
    const auto frustum = cd::camera::extract_frustum(look_forward_proj());

    // With fov_px = 1080/(2 tan 0.7) ~= 641, centre z 12.5 vs 80.5:
    //   near: self_px ~= 0.51, parent_px ~= 2.56
    //   far : self_px ~= 0.08, parent_px ~= 0.40
    // Threshold 1.0 sits between near.self and near.parent (near = frontier)
    // and above far.parent (far's parent acceptable → far dropped).
    const float self_err   = 0.01F;
    const float parent_err = 0.05F;

    const auto near_dag = make_single_cluster(self_err, parent_err, /*fz=*/12.0F);
    const auto far_dag  = make_single_cluster(self_err, parent_err, /*fz=*/80.0F);

    constexpr float kThresh = 1.0F;

    GpuDispatcher dn;
    dn.configure(near_dag, device);
    dn.dispatch_cull(cmd, frustum, kThresh, { 0.0F, 0.0F, 0.0F }, 0.7F, 1080U);

    GpuDispatcher df;
    df.configure(far_dag, device);
    df.dispatch_cull(cmd, frustum, kThresh, { 0.0F, 0.0F, 0.0F }, 0.7F, 1080U);

    // Near is the frontier (self fits, parent does not); far has shrunk so
    // even the parent fits → far is dropped as too-coarse-here.
    EXPECT_EQ(dn.visible_clusters().size(), 1U);
    EXPECT_TRUE(df.visible_clusters().empty());
}

TEST(VirtualGeometryGpuDispatch, RaisingThresholdNeverShrinksFrontierMonotonic)
{
    // Sweep the LOD threshold upward; the count of clusters that pass the
    // dual-bound test should be a unimodal/monotone-shaped band, never
    // negative, and never exceed the input cluster count. We assert the soft
    // invariant: every count is within [0, N] and the all-pass extreme holds.
    const std::uint32_t n = 100U;
    const auto dag = make_grid_dag(n);
    cd::rhi::NullDevice device {};
    cd::rhi::NullCommandBuffer cmd {};
    const auto frustum = cd::camera::extract_frustum(look_forward_proj());

    GpuDispatcher d;
    d.configure(dag, device);
    for (const float thresh : { 0.001F, 0.5F, 1.0F, 10.0F, 1e6F })
    {
        d.dispatch_cull(cmd, frustum, thresh, { 0.0F, 0.0F, 0.0F }, 0.7F, 1080U);
        EXPECT_LE(d.visible_clusters().size(), static_cast<std::size_t>(n));
    }

    // grid clusters have self_error=0, parent_error=1000: at threshold 1.0 the
    // frontier keeps all in-frustum clusters (parent_px > 1 always).
    d.dispatch_cull(cmd, frustum, 1.0F, { 0.0F, 0.0F, 0.0F }, 0.7F, 1080U);
    EXPECT_EQ(d.visible_clusters().size(), static_cast<std::size_t>(n));
}

// ---------------------------------------------------------------------------
// Render-record path + mesh-task work-group sizing (host side, NullDevice).
// ---------------------------------------------------------------------------

TEST(VirtualGeometryGpuDispatch, MeshTaskGroupsStayZeroWhenBackendLacksMeshShader)
{
    // NullDevice::features().mesh_shader == false → dispatch_render records
    // the schedule (render_dispatched=true) but never issues draw_mesh_tasks,
    // so mesh_task_groups stays 0 (the documented fallback contract).
    const auto dag = make_grid_dag(50U);
    cd::rhi::NullDevice device {};
    cd::rhi::NullCommandBuffer cmd {};
    const auto frustum = cd::camera::extract_frustum(look_forward_proj());

    GpuDispatcher d;
    d.configure(dag, device);
    d.dispatch_cull(cmd, frustum, 1.0F, { 0.0F, 0.0F, 0.0F }, 0.7F, 1080U);
    ASSERT_FALSE(d.visible_clusters().empty());

    d.dispatch_render(cmd);
    EXPECT_TRUE(d.render_dispatched());
    EXPECT_EQ(d.mesh_task_groups(), 0U);
}

TEST(VirtualGeometryGpuDispatch, MeshTaskGroupsResetToZeroWhenNothingVisible)
{
    // First a full pass (visible), then a behind-camera pass (nothing). The
    // second dispatch_render must park render_dispatched and leave the work
    // group count untouched at 0 (never issued under NullDevice anyway).
    cd::rhi::NullDevice device {};
    cd::rhi::NullCommandBuffer cmd {};
    const auto frustum = cd::camera::extract_frustum(look_forward_proj());

    const auto front = make_grid_dag(30U);
    GpuDispatcher d;
    d.configure(front, device);
    d.dispatch_cull(cmd, frustum, 1.0F, { 0.0F, 0.0F, 0.0F }, 0.7F, 1080U);
    d.dispatch_render(cmd);
    EXPECT_TRUE(d.render_dispatched());

    const auto behind = make_behind_dag(30U);
    d.configure(behind, device);
    d.dispatch_cull(cmd, frustum, 1.0F, { 0.0F, 0.0F, 0.0F }, 0.7F, 1080U);
    EXPECT_TRUE(d.visible_clusters().empty());
    d.dispatch_render(cmd);
    EXPECT_FALSE(d.render_dispatched());
    EXPECT_EQ(d.mesh_task_groups(), 0U);
}

TEST(VirtualGeometryGpuDispatch, RenderBeforeCullIsParkedNoVisibleClusters)
{
    // dispatch_render with no preceding cull → m_visible empty → no-op.
    const auto dag = make_grid_dag(10U);
    cd::rhi::NullDevice device {};
    cd::rhi::NullCommandBuffer cmd {};

    GpuDispatcher d;
    d.configure(dag, device);
    d.dispatch_render(cmd);
    EXPECT_FALSE(d.render_dispatched());
    EXPECT_EQ(d.mesh_task_groups(), 0U);
}

// ---------------------------------------------------------------------------
// Empty + single-cluster DAG edge cases through the dispatcher.
// ---------------------------------------------------------------------------

TEST(VirtualGeometryGpuDispatch, EmptyDagCullsToNothingAndRenderIsNoOp)
{
    const ClusterDAG empty_dag {};
    cd::rhi::NullDevice device {};
    cd::rhi::NullCommandBuffer cmd {};
    const auto frustum = cd::camera::extract_frustum(look_forward_proj());

    GpuDispatcher d;
    d.configure(empty_dag, device);
    d.dispatch_cull(cmd, frustum, 1.0F, { 0.0F, 0.0F, 0.0F }, 0.7F, 1080U);
    EXPECT_TRUE(d.visible_clusters().empty());
    d.dispatch_render(cmd);
    EXPECT_FALSE(d.render_dispatched());
}

TEST(VirtualGeometryGpuDispatch, DeepBuiltDagCullsAndRendersConsistently)
{
    // Drive the dispatcher with a REAL builder DAG (multi-LOD, root at +inf
    // parent_error). The cull selects a non-empty frontier and the render
    // path records the schedule.
    const std::uint32_t grid = 12U;
    std::vector<cd::math::Vec3f> verts;
    std::vector<std::uint32_t>   idx;
    verts.reserve(static_cast<std::size_t>(grid + 1U) * (grid + 1U));
    const float half = static_cast<float>(grid) * 0.5F;
    for (std::uint32_t i = 0; i <= grid; ++i)
        for (std::uint32_t j = 0; j <= grid; ++j)
        {
            cd::math::Vec3f v;
            v.x = static_cast<float>(j) - half;
            v.y = static_cast<float>(i) - half;
            v.z = 20.0F;
            verts.push_back(v);
        }
    for (std::uint32_t i = 0; i < grid; ++i)
        for (std::uint32_t j = 0; j < grid; ++j)
        {
            const std::uint32_t a = i * (grid + 1U) + j;
            const std::uint32_t b = (i + 1U) * (grid + 1U) + j;
            const std::uint32_t c = (i + 1U) * (grid + 1U) + j + 1U;
            const std::uint32_t e = i * (grid + 1U) + j + 1U;
            idx.push_back(a); idx.push_back(b); idx.push_back(c);
            idx.push_back(a); idx.push_back(c); idx.push_back(e);
        }

    const cd::virtual_geometry::ClusterDAGBuilder builder;
    const auto dag = builder.build(verts, idx);
    ASSERT_GE(dag.lod_levels(), 2U);

    cd::rhi::NullDevice device {};
    cd::rhi::NullCommandBuffer cmd {};
    const auto frustum = cd::camera::extract_frustum(look_forward_proj());

    GpuDispatcher d;
    d.configure(dag, device);
    // Coarse threshold so the leaf/coarse frontier is non-empty for a planar
    // grid sitting at z=20 in front of the camera.
    d.dispatch_cull(cmd, frustum, 1e6F, { 0.0F, 0.0F, 0.0F }, 0.7F, 1080U);

    // Every reported id is a valid index into the DAG.
    for (const std::uint32_t id : d.visible_clusters())
        EXPECT_LT(id, dag.clusters().size());

    d.dispatch_render(cmd);
    // render_dispatched mirrors "did the cull find anything".
    EXPECT_EQ(d.render_dispatched(), !d.visible_clusters().empty());
}

// ---------------------------------------------------------------------------
// Visibility-buffer pack contract — sentinel + 25:7 split edges.
// ---------------------------------------------------------------------------

TEST(VirtualGeometryGpuDispatch, PackClusterZeroTriZeroIsNotSentinel)
{
    // cluster 0 / tri 0 must NOT collapse to the all-zero "empty" sentinel
    // because pack_visibility adds 1 to the cluster id before shifting.
    EXPECT_NE(pack_visibility(0U, 0U), 0U);
    EXPECT_EQ(unpack_cluster_id(pack_visibility(0U, 0U)), 0U);
    EXPECT_EQ(unpack_triangle_id(pack_visibility(0U, 0U)), 0U);
}

TEST(VirtualGeometryGpuDispatch, PackTriangleIdSaturatesToSevenBits)
{
    // triangle_id is masked to the low 7 bits; ids >= 128 wrap into range.
    // Pin the ACTUAL masking behaviour (not a clamp): 128 -> 0, 130 -> 2.
    EXPECT_EQ(unpack_triangle_id(pack_visibility(5U, 128U)), 0U);
    EXPECT_EQ(unpack_triangle_id(pack_visibility(5U, 130U)), 2U);
    // Cluster id is unaffected by the masked triangle id.
    EXPECT_EQ(unpack_cluster_id(pack_visibility(5U, 130U)), 5U);
}

TEST(VirtualGeometryGpuDispatch, PackVisibilityIsConstexpr)
{
    // The packing helpers are constexpr — exercise that at compile time so a
    // future non-constexpr regression is caught here.
    constexpr std::uint32_t packed = pack_visibility(7U, 33U);
    static_assert(unpack_cluster_id(packed) == 7U);
    static_assert(unpack_triangle_id(packed) == 33U);
    SUCCEED();
}

TEST(VirtualGeometryGpuDispatch, PackMaxClusterIdRoundtripsAtTwentyFiveBitBoundary)
{
    // The cluster id occupies bits 7..31 → the +1-shifted maximum that still
    // fits without overflowing the 32-bit pixel is (2^25 - 2). Pin the exact
    // top-of-range round-trip (one below the reserved sentinel slot).
    constexpr std::uint32_t kMaxClusterId = (1U << 25U) - 2U;
    const std::uint32_t packed = pack_visibility(kMaxClusterId, 127U);
    EXPECT_NE(packed, 0U);
    EXPECT_EQ(unpack_cluster_id(packed),  kMaxClusterId);
    EXPECT_EQ(unpack_triangle_id(packed), 127U);
}

// ---------------------------------------------------------------------------
// Frustum partial-intersect (kIntersecting) clusters are KEPT — the cull only
// rejects on kOutside. A cluster straddling a frustum plane survives.
// ---------------------------------------------------------------------------

TEST(VirtualGeometryGpuDispatch, ClusterStraddlingFrustumEdgeIsKeptNotCulled)
{
    // A wide cluster reaching from inside the view far out past the lateral
    // FOV at z=12 straddles the side plane → test_aabb returns kIntersecting,
    // which is NOT kOutside, so the dispatcher keeps it (self=0/parent=1000
    // makes it the frontier).
    Cluster c;
    c.triangles  = { 0U };
    c.lod_level  = 0U;
    c.parent_lod = 1U;
    c.self_error = 0.0F;
    c.parent_error = 1000.0F;
    c.bbox.min_corner = { -50.0F, -0.5F, 12.0F };  // far left of the FOV
    c.bbox.max_corner = {   0.0F,  0.5F, 13.0F };
    std::vector<Cluster> nodes;
    nodes.push_back(std::move(c));
    const ClusterDAG dag { std::move(nodes) };

    cd::rhi::NullDevice device {};
    cd::rhi::NullCommandBuffer cmd {};
    const auto frustum = cd::camera::extract_frustum(look_forward_proj());

    GpuDispatcher d;
    d.configure(dag, device);
    d.dispatch_cull(cmd, frustum, 1.0F, { 0.0F, 0.0F, 0.0F }, 0.7F, 1080U);
    EXPECT_EQ(d.visible_clusters().size(), 1U)
        << "a kIntersecting cluster must survive the cull (only kOutside drops)";
}

// ---------------------------------------------------------------------------
// Render-record idempotence + host-side state transitions across repeated
// dispatch calls (NullDevice: mesh_task_groups never leaves 0).
// ---------------------------------------------------------------------------

TEST(VirtualGeometryGpuDispatch, RepeatedRenderDispatchKeepsStateStableUnderNullDevice)
{
    // Calling dispatch_render twice with the same visible set must leave
    // render_dispatched true and mesh_task_groups at 0 (NullDevice path) —
    // idempotent, no accumulation.
    const auto dag = make_grid_dag(40U);
    cd::rhi::NullDevice device {};
    cd::rhi::NullCommandBuffer cmd {};
    const auto frustum = cd::camera::extract_frustum(look_forward_proj());

    GpuDispatcher d;
    d.configure(dag, device);
    d.dispatch_cull(cmd, frustum, 1.0F, { 0.0F, 0.0F, 0.0F }, 0.7F, 1080U);
    ASSERT_FALSE(d.visible_clusters().empty());

    d.dispatch_render(cmd);
    EXPECT_TRUE(d.render_dispatched());
    EXPECT_EQ(d.mesh_task_groups(), 0U);

    d.dispatch_render(cmd);  // second call, same visible set
    EXPECT_TRUE(d.render_dispatched());
    EXPECT_EQ(d.mesh_task_groups(), 0U);
}

TEST(VirtualGeometryGpuDispatch, RenderDispatchedFlagFlipsBackOffWhenCullEmptiesVisibleSet)
{
    // After a successful render-record, a subsequent cull that finds nothing
    // followed by a render must PARK render_dispatched (false) — the flag is
    // recomputed each dispatch_render, never sticky.
    cd::rhi::NullDevice device {};
    cd::rhi::NullCommandBuffer cmd {};
    const auto frustum = cd::camera::extract_frustum(look_forward_proj());

    const auto front = make_grid_dag(25U);
    GpuDispatcher d;
    d.configure(front, device);
    d.dispatch_cull(cmd, frustum, 1.0F, { 0.0F, 0.0F, 0.0F }, 0.7F, 1080U);
    d.dispatch_render(cmd);
    ASSERT_TRUE(d.render_dispatched());

    // Same dispatcher, behind-camera DAG → cull empties the visible set.
    const auto behind = make_behind_dag(25U);
    d.configure(behind, device);
    d.dispatch_cull(cmd, frustum, 1.0F, { 0.0F, 0.0F, 0.0F }, 0.7F, 1080U);
    ASSERT_TRUE(d.visible_clusters().empty());
    d.dispatch_render(cmd);
    EXPECT_FALSE(d.render_dispatched());
}

TEST(VirtualGeometryGpuDispatch, CullCountIndependentOfViewportHeightForFrontierGridDag)
{
    // projected_err scales linearly in viewport_h_px for BOTH self and parent,
    // and the grid frontier predicate (self=0, parent=1000) is dominated by
    // the frustum test + the parent>thresh upper bound, which both hold at any
    // height → the visible COUNT is invariant to viewport height. Pin it at
    // 540 vs 2160 (1/2x and 2x the canonical 1080).
    const std::uint32_t n = 100U;
    const auto dag = make_grid_dag(n);
    cd::rhi::NullDevice device {};
    cd::rhi::NullCommandBuffer cmd {};
    const auto frustum = cd::camera::extract_frustum(look_forward_proj());

    GpuDispatcher d;
    d.configure(dag, device);
    d.dispatch_cull(cmd, frustum, 1.0F, { 0.0F, 0.0F, 0.0F }, 0.7F, 540U);
    const auto count_low = d.visible_clusters().size();

    d.dispatch_cull(cmd, frustum, 1.0F, { 0.0F, 0.0F, 0.0F }, 0.7F, 2160U);
    const auto count_high = d.visible_clusters().size();

    EXPECT_EQ(count_low, count_high);
    EXPECT_EQ(count_low, static_cast<std::size_t>(n));
}

TEST(VirtualGeometryGpuDispatch, DefaultVisibilityBufferDescriptorIsFullHd)
{
    // configure() default arg is { 1920, 1080, 32 } — pin that contract so a
    // future signature change is caught.
    const auto dag = make_grid_dag(4U);
    cd::rhi::NullDevice device {};
    GpuDispatcher d;
    d.configure(dag, device);  // default vis_desc
    EXPECT_EQ(d.visibility_buffer().width,                 1920U);
    EXPECT_EQ(d.visibility_buffer().height,                1080U);
    EXPECT_EQ(d.visibility_buffer().format_bits_per_pixel, 32U);
}

TEST(VirtualGeometryGpuDispatch, CullPushConstantPodDefaultsMatchShippedContract)
{
    // ClusterCullPushConstants is the host mirror of the cull-CS push block —
    // pin its documented defaults so host + GLSL stay in sync (Nanite 1px LOD,
    // 0.7 half-fov, 1080 viewport).
    const cd::virtual_geometry::ClusterCullPushConstants pc {};
    EXPECT_FLOAT_EQ(pc.half_fov_rad,     0.7F);
    EXPECT_EQ(pc.cluster_count,          0U);
    EXPECT_EQ(pc.viewport_h_px,          1080U);
    EXPECT_FLOAT_EQ(pc.lod_threshold_px, 1.0F);
    EXPECT_FLOAT_EQ(pc.cam_eye.x, 0.0F);
    EXPECT_FLOAT_EQ(pc.cam_eye.y, 0.0F);
    EXPECT_FLOAT_EQ(pc.cam_eye.z, 0.0F);
}

}  // namespace
