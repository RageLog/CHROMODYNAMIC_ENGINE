// =============================================================================
// CHROMODYNAMIC — test_axis_gizmo_v2.cpp
// Phase 1071 — AxisGizmo v2: modes, plane pads, scalar value drags and
// the ray-plane pick kit.
//
// Pattern: Arrange / Act / Assert. Pure CPU — no GPU, no ImGui.
// =============================================================================
#include <cd/editor/AxisGizmo.hpp>
#include <gtest/gtest.h>

#include <cmath>

using cd::editor::AxisGizmo;
using cd::editor::GizmoAxis;
using cd::editor::GizmoMode;
using cd::editor::PickRay;
using cd::math::Vec3f;

// ---------------------------------------------------------------------------
// Plane pads
// ---------------------------------------------------------------------------

TEST(AxisGizmoV2, IsPlaneClassifiesPadValues)
{
    EXPECT_FALSE(cd::editor::is_plane(GizmoAxis::kNone));
    EXPECT_FALSE(cd::editor::is_plane(GizmoAxis::kX));
    EXPECT_FALSE(cd::editor::is_plane(GizmoAxis::kY));
    EXPECT_FALSE(cd::editor::is_plane(GizmoAxis::kZ));
    EXPECT_TRUE(cd::editor::is_plane(GizmoAxis::kXY));
    EXPECT_TRUE(cd::editor::is_plane(GizmoAxis::kXZ));
    EXPECT_TRUE(cd::editor::is_plane(GizmoAxis::kYZ));
}

TEST(AxisGizmoV2, PlaneDragAppliesExactlyTwoComponents)
{
    AxisGizmo g;
    g.set_target({ 1.0F, 2.0F, 3.0F });
    g.begin_drag(GizmoAxis::kXZ, { 0.0F, 0.0F, 0.0F });
    g.update_drag({ 0.5F, 9.0F, -0.25F });  // delta (0.5, 9, -0.25)

    const auto& t = g.target();
    EXPECT_FLOAT_EQ(t.x, 1.5F);    // x passes
    EXPECT_FLOAT_EQ(t.y, 2.0F);    // y BLOCKED on the XZ pad
    EXPECT_FLOAT_EQ(t.z, 2.75F);   // z passes

    const auto total = g.end_drag();
    EXPECT_FLOAT_EQ(total.x, 0.5F);
    EXPECT_FLOAT_EQ(total.y, 0.0F);
    EXPECT_FLOAT_EQ(total.z, -0.25F);
}

TEST(AxisGizmoV2, PlaneNormalsMatchMissingAxis)
{
    const auto nxy = cd::editor::plane_normal(GizmoAxis::kXY);
    const auto nxz = cd::editor::plane_normal(GizmoAxis::kXZ);
    const auto nyz = cd::editor::plane_normal(GizmoAxis::kYZ);
    EXPECT_FLOAT_EQ(nxy.z, 1.0F);
    EXPECT_FLOAT_EQ(nxz.y, 1.0F);
    EXPECT_FLOAT_EQ(nyz.x, 1.0F);
    const auto none = cd::editor::plane_normal(GizmoAxis::kX);
    EXPECT_FLOAT_EQ(cd::math::dot(none, none), 0.0F);
}

// ---------------------------------------------------------------------------
// Mode switching
// ---------------------------------------------------------------------------

TEST(AxisGizmoV2, ModeDefaultsToTranslateAndSwitches)
{
    AxisGizmo g;
    EXPECT_EQ(g.mode(), GizmoMode::kTranslate);
    g.set_mode(GizmoMode::kRotate);
    EXPECT_EQ(g.mode(), GizmoMode::kRotate);
    g.set_mode(GizmoMode::kScale);
    EXPECT_EQ(g.mode(), GizmoMode::kScale);
}

TEST(AxisGizmoV2, ModeSwitchBlockedMidDrag)
{
    AxisGizmo g;
    g.begin_drag(GizmoAxis::kX, {});
    g.set_mode(GizmoMode::kRotate);          // must be ignored
    EXPECT_EQ(g.mode(), GizmoMode::kTranslate);
    (void)g.end_drag();
    g.set_mode(GizmoMode::kRotate);          // now allowed
    EXPECT_EQ(g.mode(), GizmoMode::kRotate);
}

// ---------------------------------------------------------------------------
// Scalar value drags (rotate / scale sessions)
// ---------------------------------------------------------------------------

TEST(AxisGizmoV2, ValueDragAccumulatesAndResets)
{
    AxisGizmo g;
    g.begin_value_drag(GizmoAxis::kY);
    EXPECT_TRUE(g.is_dragging());
    EXPECT_EQ(g.active_axis(), GizmoAxis::kY);

    g.update_value_drag(0.25F);
    g.update_value_drag(0.75F);              // overwrite, not add
    EXPECT_FLOAT_EQ(g.drag_value(), 0.75F);

    const float total = g.end_value_drag();
    EXPECT_FLOAT_EQ(total, 0.75F);
    EXPECT_FALSE(g.is_dragging());
    EXPECT_EQ(g.active_axis(), GizmoAxis::kNone);
    EXPECT_FLOAT_EQ(g.drag_value(), 0.0F);
}

TEST(AxisGizmoV2, ValueDragIgnoresNoneAxis)
{
    AxisGizmo g;
    g.begin_value_drag(GizmoAxis::kNone);
    EXPECT_FALSE(g.is_dragging());
    g.update_value_drag(5.0F);               // no session — must be a no-op
    EXPECT_FLOAT_EQ(g.end_value_drag(), 0.0F);
}

// ---------------------------------------------------------------------------
// Ray-plane kit
// ---------------------------------------------------------------------------

TEST(AxisGizmoV2, RayPlaneHitsGroundPlane)
{
    const PickRay ray { { 0.0F, 5.0F, 0.0F }, { 0.0F, -1.0F, 0.0F } };
    const auto hit = cd::editor::intersect_ray_plane(
        ray, { 0.0F, 1.0F, 0.0F }, { 0.0F, 1.0F, 0.0F });
    ASSERT_TRUE(hit.has_value());
    EXPECT_FLOAT_EQ(hit->y, 1.0F);
    EXPECT_FLOAT_EQ(hit->x, 0.0F);
    EXPECT_FLOAT_EQ(hit->z, 0.0F);
}

TEST(AxisGizmoV2, RayPlaneRejectsParallelAndBehind)
{
    // Parallel: ray sliding along the plane.
    const PickRay parallel { { 0.0F, 5.0F, 0.0F }, { 1.0F, 0.0F, 0.0F } };
    EXPECT_FALSE(cd::editor::intersect_ray_plane(
        parallel, {}, { 0.0F, 1.0F, 0.0F }).has_value());

    // Behind: plane is behind the ray origin.
    const PickRay away { { 0.0F, 5.0F, 0.0F }, { 0.0F, 1.0F, 0.0F } };
    EXPECT_FALSE(cd::editor::intersect_ray_plane(
        away, {}, { 0.0F, 1.0F, 0.0F }).has_value());
}

TEST(AxisGizmoV2, AxisDragPlaneContainsAxisAndFacesView)
{
    // Looking diagonally down at the scene, dragging along X.
    const Vec3f view = cd::math::normalize(Vec3f { 0.3F, -0.7F, 0.6F });
    const auto n = cd::editor::axis_drag_plane_normal(GizmoAxis::kX, view);

    // Plane contains the axis -> normal ⟂ axis.
    EXPECT_NEAR(cd::math::dot(n, Vec3f { 1.0F, 0.0F, 0.0F }), 0.0F, 1e-5F);
    // Faces the camera: |dot(n, view)| should be substantial.
    EXPECT_GT(std::fabs(cd::math::dot(n, view)), 0.5F);
    // Unit length.
    EXPECT_NEAR(cd::math::dot(n, n), 1.0F, 1e-5F);
}

TEST(AxisGizmoV2, AxisDragPlaneDegenerateFallback)
{
    // View dead-parallel to the drag axis: helper must still return a
    // unit vector perpendicular to the axis (any such plane is valid).
    const auto n = cd::editor::axis_drag_plane_normal(
        GizmoAxis::kX, { 1.0F, 0.0F, 0.0F });
    EXPECT_NEAR(cd::math::dot(n, Vec3f { 1.0F, 0.0F, 0.0F }), 0.0F, 1e-5F);
    EXPECT_NEAR(cd::math::dot(n, n), 1.0F, 1e-5F);
}

TEST(AxisGizmoV2, PickRayFromNdcIdentityLooksForward)
{
    // Identity view-proj: NDC == world. Center pixel must yield a +Z ray
    // from z=0 (Vulkan near) toward z=1 (far).
    const cd::math::Mat4f ident = cd::math::Mat4f::identity();
    const auto ray = cd::editor::pick_ray_from_ndc(ident, 0.0F, 0.0F);
    ASSERT_TRUE(ray.has_value());
    EXPECT_NEAR(ray->origin.z, 0.0F, 1e-5F);
    EXPECT_NEAR(ray->dir.z, 1.0F, 1e-5F);
    EXPECT_NEAR(ray->dir.x, 0.0F, 1e-5F);

    // Off-center NDC shifts the origin but keeps the forward direction.
    const auto corner = cd::editor::pick_ray_from_ndc(ident, 0.5F, -0.25F);
    ASSERT_TRUE(corner.has_value());
    EXPECT_NEAR(corner->origin.x, 0.5F, 1e-5F);
    EXPECT_NEAR(corner->origin.y, -0.25F, 1e-5F);
    EXPECT_NEAR(corner->dir.z, 1.0F, 1e-5F);
}

// ---------------------------------------------------------------------------
// Regression net: v1 axis behaviour unchanged by the v2 extension.
// ---------------------------------------------------------------------------

TEST(AxisGizmoV2, SingleAxisDragStillSingleComponent)
{
    AxisGizmo g;
    g.set_target({ 0.0F, 0.0F, 0.0F });
    g.begin_drag(GizmoAxis::kY, {});
    g.update_drag({ 4.0F, 2.0F, -7.0F });
    EXPECT_FLOAT_EQ(g.target().x, 0.0F);
    EXPECT_FLOAT_EQ(g.target().y, 2.0F);
    EXPECT_FLOAT_EQ(g.target().z, 0.0F);
    (void)g.end_drag();
}
