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

}  // namespace
