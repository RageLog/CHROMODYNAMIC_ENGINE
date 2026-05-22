// =============================================================================
// CHROMODYNAMIC — cd::camera tests
//
// Covers Camera matrix derivation, the AABB auto-framer, the OrbitController
// state-machine round-trips, and the frustum AABB cull tester.
// =============================================================================
#include <cd/camera/Camera.hpp>
#include <cd/camera/Frustum.hpp>
#include <cd/camera/OrbitController.hpp>
#include <gtest/gtest.h>

#include <cmath>

namespace
{
constexpr float kEps = 1e-4F;
}

TEST(Camera, ViewProjectionFiniteForZeroAspect)
{
    cd::camera::Camera c {};
    c.eye = { 0.0F, 0.0F, 3.0F };
    // Zero aspect should NOT produce NaN — the helper substitutes 1:1.
    auto m = cd::camera::view_projection(c, 0.0F);
    for (std::size_t col = 0; col < 4; ++col)
    {
        for (std::size_t row = 0; row < 4; ++row)
        {
            EXPECT_TRUE(std::isfinite(m[col][row])) << "col=" << col << " row=" << row;
        }
    }
}

TEST(Camera, AutoFrameAabbCentersAndScalesDistance)
{
    cd::math::Vec3f mn { -2.0F, -1.0F, -3.0F };
    cd::math::Vec3f mx { 4.0F, 5.0F, 1.0F };
    auto c = cd::camera::auto_frame_aabb(mn, mx);
    // Target = centre of the AABB.
    EXPECT_NEAR(c.target[0], 1.0F, kEps);
    EXPECT_NEAR(c.target[1], 2.0F, kEps);
    EXPECT_NEAR(c.target[2], -1.0F, kEps);
    // Eye is offset from target by a finite amount, never NaN.
    EXPECT_TRUE(std::isfinite(c.eye[0]));
    EXPECT_TRUE(std::isfinite(c.eye[1]));
    EXPECT_TRUE(std::isfinite(c.eye[2]));
    EXPECT_GT(c.far_z, c.near_z);
}

TEST(OrbitController, SyncFromCameraThenAutoSpinKeepsRadius)
{
    cd::camera::Camera c {};
    c.eye = { 5.0F, 0.0F, 0.0F };
    c.target = { 0.0F, 0.0F, 0.0F };

    cd::camera::OrbitController orbit {};
    orbit.sync_from_camera(c);
    orbit.auto_spin_rate = 1.0F;

    // 1 second of auto-spin. Radius from target must be preserved.
    orbit.update_auto(c, 1.0F);
    const float r = std::sqrt(c.eye[0] * c.eye[0] + c.eye[1] * c.eye[1] + c.eye[2] * c.eye[2]);
    EXPECT_NEAR(r, 5.0F, kEps);
}

TEST(OrbitController, DragAndZoomChangeEye)
{
    cd::camera::Camera c {};
    c.eye = { 3.0F, 0.0F, 0.0F };
    c.target = { 0.0F, 0.0F, 0.0F };
    cd::camera::OrbitController orbit {};
    orbit.sync_from_camera(c);

    const auto before = c.eye;
    orbit.drag(100.0F, 50.0F);
    orbit.zoom(0.5F);
    orbit.apply(c);
    EXPECT_NE(c.eye[0], before[0]);
}

TEST(Frustum, AabbAtOriginIsVisibleFromDefaultCamera)
{
    cd::camera::Camera c {};
    c.eye = { 0.0F, 0.0F, 3.0F };
    c.target = { 0.0F, 0.0F, 0.0F };
    auto f = cd::camera::extract_frustum(c, 1.0F);
    auto r = cd::camera::test_aabb(f, { -0.5F, -0.5F, -0.5F }, { 0.5F, 0.5F, 0.5F });
    EXPECT_NE(r, cd::camera::CullResult::kOutside);
}

TEST(Frustum, AabbWayBehindCameraIsCulled)
{
    cd::camera::Camera c {};
    c.eye = { 0.0F, 0.0F, 3.0F };
    c.target = { 0.0F, 0.0F, 0.0F };
    auto f = cd::camera::extract_frustum(c, 1.0F);
    // Object 20 units BEHIND the camera (camera looks toward -Z; behind = +Z).
    auto r = cd::camera::test_aabb(f, { -0.5F, -0.5F, 22.0F }, { 0.5F, 0.5F, 23.0F });
    EXPECT_EQ(r, cd::camera::CullResult::kOutside);
}

TEST(Frustum, AabbOffToTheSideIsCulled)
{
    cd::camera::Camera c {};
    c.eye = { 0.0F, 0.0F, 3.0F };
    c.target = { 0.0F, 0.0F, 0.0F };
    c.fov_y = 0.5F;  // narrow fov so the off-screen AABB is clearly outside.
    auto f = cd::camera::extract_frustum(c, 1.0F);
    auto r = cd::camera::test_aabb(f, { 100.0F, -0.5F, -0.5F }, { 101.0F, 0.5F, 0.5F });
    EXPECT_EQ(r, cd::camera::CullResult::kOutside);
}

TEST(Frustum, FullyInsideAabbReportsKInside)
{
    cd::camera::Camera c {};
    c.eye = { 0.0F, 0.0F, 10.0F };
    c.target = { 0.0F, 0.0F, 0.0F };
    c.fov_y = 1.5F;  // wide
    auto f = cd::camera::extract_frustum(c, 1.0F);
    // Tiny box near the centre — must be fully inside.
    auto r = cd::camera::test_aabb(f, { -0.01F, -0.01F, -0.01F }, { 0.01F, 0.01F, 0.01F });
    EXPECT_EQ(r, cd::camera::CullResult::kInside);
}
