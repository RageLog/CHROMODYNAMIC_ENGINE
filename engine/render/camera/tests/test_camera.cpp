// =============================================================================
// CHROMODYNAMIC — cd::camera tests
//
// Covers Camera matrix derivation, the AABB auto-framer, the OrbitController
// state-machine round-trips, and the frustum AABB cull tester.
// =============================================================================
#include <cd/camera/Camera.hpp>
#include <cd/camera/FirstPersonController.hpp>
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

// Phase 153 — Frustum::test_sphere / contains_sphere.
TEST(Frustum, SphereAtOriginIsVisible)
{
    cd::camera::Camera c {};
    c.eye = { 0.0F, 0.0F, 3.0F };
    c.target = { 0.0F, 0.0F, 0.0F };
    auto f = cd::camera::extract_frustum(c, 1.0F);
    EXPECT_NE(cd::camera::test_sphere(f, { 0.0F, 0.0F, 0.0F }, 0.5F),
              cd::camera::CullResult::kOutside);
    EXPECT_TRUE(cd::camera::contains_sphere(f, { 0.0F, 0.0F, 0.0F }, 0.5F));
}

TEST(Frustum, SphereBehindCameraIsCulled)
{
    cd::camera::Camera c {};
    c.eye = { 0.0F, 0.0F, 3.0F };
    c.target = { 0.0F, 0.0F, 0.0F };
    auto f = cd::camera::extract_frustum(c, 1.0F);
    // Sphere 20 units behind camera (positive Z).
    EXPECT_EQ(cd::camera::test_sphere(f, { 0.0F, 0.0F, 23.0F }, 0.5F),
              cd::camera::CullResult::kOutside);
    EXPECT_FALSE(cd::camera::contains_sphere(f, { 0.0F, 0.0F, 23.0F }, 0.5F));
}

TEST(Frustum, SphereOffToTheSideIsCulled)
{
    cd::camera::Camera c {};
    c.eye = { 0.0F, 0.0F, 3.0F };
    c.target = { 0.0F, 0.0F, 0.0F };
    c.fov_y = 0.5F;
    auto f = cd::camera::extract_frustum(c, 1.0F);
    EXPECT_EQ(cd::camera::test_sphere(f, { 100.0F, 0.0F, 0.0F }, 0.5F),
              cd::camera::CullResult::kOutside);
}

TEST(Frustum, LargeSphereStraddlingFrustumReportsIntersecting)
{
    cd::camera::Camera c {};
    c.eye = { 0.0F, 0.0F, 5.0F };
    c.target = { 0.0F, 0.0F, 0.0F };
    c.fov_y = 0.8F;
    auto f = cd::camera::extract_frustum(c, 1.0F);
    // Big sphere positioned so it overlaps the frustum boundary.
    auto r = cd::camera::test_sphere(f, { 2.5F, 0.0F, 0.0F }, 2.0F);
    EXPECT_EQ(r, cd::camera::CullResult::kIntersecting);
}

TEST(Frustum, TinyCentralSphereReportsKInside)
{
    cd::camera::Camera c {};
    c.eye = { 0.0F, 0.0F, 10.0F };
    c.target = { 0.0F, 0.0F, 0.0F };
    c.fov_y = 1.5F;
    auto f = cd::camera::extract_frustum(c, 1.0F);
    EXPECT_EQ(cd::camera::test_sphere(f, { 0.0F, 0.0F, 0.0F }, 0.01F),
              cd::camera::CullResult::kInside);
}

#include <cd/camera/CameraPath.hpp>

TEST(CameraPath, EmptySampleReturnsOrigin)
{
    cd::camera::CameraPath p;
    auto k = p.sample(0.5F);
    EXPECT_FLOAT_EQ(k.eye.x, 0.0F);
    EXPECT_FLOAT_EQ(k.eye.y, 0.0F);
    EXPECT_FLOAT_EQ(k.eye.z, 0.0F);
}

TEST(CameraPath, SingleKeyEverywhere)
{
    cd::camera::CameraPath p;
    cd::camera::CameraKey k0 { 0.0F, { 1, 2, 3 }, { 4, 5, 6 } };
    p.add_key(k0);
    auto a = p.sample(-1.0F);  // before
    auto b = p.sample(0.0F);   // exact
    auto c = p.sample(10.0F);  // after
    EXPECT_FLOAT_EQ(a.eye.x, 1.0F);
    EXPECT_FLOAT_EQ(b.eye.x, 1.0F);
    EXPECT_FLOAT_EQ(c.eye.x, 1.0F);
}

TEST(CameraPath, SampleAtKeyTimeReturnsKey)
{
    cd::camera::CameraPath p;
    p.add_key(cd::camera::CameraKey { 0.0F, { 0, 0, 0 }, { 1, 0, 0 } });
    p.add_key(cd::camera::CameraKey { 1.0F, { 5, 0, 0 }, { 6, 0, 0 } });
    auto k = p.sample(1.0F);
    EXPECT_FLOAT_EQ(k.eye.x, 5.0F);
    EXPECT_FLOAT_EQ(k.target.x, 6.0F);
}

TEST(CameraPath, ApplyWritesIntoCamera)
{
    cd::camera::CameraPath p;
    p.add_key(cd::camera::CameraKey { 0.0F, { 7, 0, 0 }, { 8, 0, 0 } });
    cd::camera::Camera cam;
    cd::camera::CameraPath::apply(cam, p.sample(0.0F));
    EXPECT_FLOAT_EQ(cam.eye.x, 7.0F);
    EXPECT_FLOAT_EQ(cam.target.x, 8.0F);
}

#include <cd/camera/Lens.hpp>

TEST(Lens, StandardLensYieldsReasonableFov)
{
    cd::camera::Lens l = cd::camera::lens_standard();
    const float fov = l.fov_y_for(24.0F);
    // 50mm on 24mm sensor → ~0.45 rad ≈ 26°
    EXPECT_GT(fov, 0.4F);
    EXPECT_LT(fov, 0.5F);
}

TEST(Lens, WideLensHasLargerFovThanTelephoto)
{
    auto wide = cd::camera::lens_wide().fov_y_for();
    auto tele = cd::camera::lens_telephoto().fov_y_for();
    EXPECT_GT(wide, tele);
}

TEST(Lens, ApplyToCameraUpdatesFovY)
{
    cd::camera::Camera cam;
    const float before = cam.fov_y;
    cd::camera::lens_portrait().apply_to(cam);
    EXPECT_NE(cam.fov_y, before);
}

TEST(Lens, ZeroFocalLengthReturnsSafeFallback)
{
    cd::camera::Lens l { 0.0F, 1.4F, 5.0F };
    EXPECT_FLOAT_EQ(l.fov_y_for(), 1.0F);
}

#include <cd/camera/ViewportInfo.hpp>

TEST(ViewportInfo, AspectComputation)
{
    cd::camera::ViewportInfo v { 0, 0, 1920, 1080 };
    EXPECT_NEAR(cd::camera::aspect(v), 1.7778F, 1e-3F);
}

TEST(ViewportInfo, AspectZeroHeightSafeFallback)
{
    cd::camera::ViewportInfo v { 0, 0, 100, 0 };
    EXPECT_FLOAT_EQ(cd::camera::aspect(v), 1.0F);
}

TEST(ViewportInfo, ScreenToNdcCorners)
{
    cd::camera::ViewportInfo v { 0, 0, 800, 600 };
    float xn = 0.0F, yn = 0.0F;
    cd::camera::screen_to_ndc(v, 0.0F, 0.0F, xn, yn);
    EXPECT_FLOAT_EQ(xn, -1.0F);
    EXPECT_FLOAT_EQ(yn,  1.0F);
    cd::camera::screen_to_ndc(v, 800.0F, 600.0F, xn, yn);
    EXPECT_FLOAT_EQ(xn,  1.0F);
    EXPECT_FLOAT_EQ(yn, -1.0F);
}

TEST(ViewportInfo, RoundTrip)
{
    cd::camera::ViewportInfo v { 100, 200, 800, 600 };
    float xn = 0.0F, yn = 0.0F;
    cd::camera::screen_to_ndc(v, 500.0F, 400.0F, xn, yn);
    float px = 0.0F, py = 0.0F;
    cd::camera::ndc_to_screen(v, xn, yn, px, py);
    EXPECT_NEAR(px, 500.0F, 1e-3F);
    EXPECT_NEAR(py, 400.0F, 1e-3F);
}

// =============================================================================
// W6-A: FirstPersonController tests.
// =============================================================================

TEST(FirstPersonController, SyncFromCameraExtractsYawPitchDist)
{
    cd::camera::Camera c {};
    c.eye    = { 0.0F, 0.0F, 5.0F };
    c.target = { 0.0F, 0.0F, 0.0F };
    cd::camera::FirstPersonController fp;
    fp.sync_from_camera(c);
    EXPECT_NEAR(fp.dist(),  5.0F, 1e-3F);
    EXPECT_NEAR(fp.pitch(), 0.0F, 1e-3F);
    EXPECT_NEAR(fp.yaw(),   0.0F, 1e-3F);
}

TEST(FirstPersonController, LookClampsPitchUnderHalfPi)
{
    cd::camera::FirstPersonController fp;
    // Apply a huge downward mouse delta — pitch must clamp short of PI/2.
    fp.look(0.0F, 100000.0F);
    EXPECT_LT(fp.pitch(), 1.56F);
    EXPECT_GT(fp.pitch(), 1.55F);
}

TEST(FirstPersonController, MoveAccumulatesIntoApply)
{
    cd::camera::Camera c {};
    c.eye    = { 0.0F, 0.0F, 5.0F };
    c.target = { 0.0F, 0.0F, 0.0F };
    cd::camera::FirstPersonController fp;
    fp.sync_from_camera(c);
    fp.move(1.0F, { 1.0F, 0.0F, 0.0F });  // strafe right for 1 second
    fp.apply(c);
    // After a positive +X strafe the target should have moved along
    // the camera's local right (~+X for a camera looking down -Z).
    EXPECT_GT(c.target[0], 0.0F);
}

TEST(FirstPersonController, ZoomShrinksDist)
{
    cd::camera::FirstPersonController fp;
    cd::camera::Camera c {};
    c.eye = { 0.0F, 0.0F, 5.0F };
    c.target = { 0.0F, 0.0F, 0.0F };
    fp.sync_from_camera(c);
    const float d0 = fp.dist();
    fp.zoom(1.0F);
    EXPECT_LT(fp.dist(), d0);
}
