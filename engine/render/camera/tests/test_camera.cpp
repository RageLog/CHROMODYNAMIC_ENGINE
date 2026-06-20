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
} // namespace

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
    float xn = 0.0F;
    float yn = 0.0F;
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
    float xn = 0.0F;
    float yn = 0.0F;
    cd::camera::screen_to_ndc(v, 500.0F, 400.0F, xn, yn);
    float px = 0.0F;
    float py = 0.0F;
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

// =============================================================================
// phase12xx 80→100 depth pass — ADD-ONLY edge/negative coverage.
//
// GOLDEN-CRITICAL: these tests only OBSERVE existing math; they add NO new
// production code and change NO view/proj/frustum/lens body. Known-value
// projection assertions are derived directly from cd::math::perspective /
// ortho (column-major m[col][row], Vulkan/D3D depth [0,1]).
// =============================================================================
#include <cd/math/Transform.hpp>

#include <array>
#include <numbers>

namespace
{
// Vulkan/D3D RH perspective (cd::math::perspective): the only non-zero entries.
constexpr float kProjEps = 1e-4F;
} // namespace

// ---- Perspective projection known-value at fov/aspect/near/far extremes -----

TEST(CameraProjection, PerspectiveKnownValuesAtCanonicalParams)
{
    cd::camera::Camera c {};
    c.fov_y  = std::numbers::pi_v<float> / 2.0F;  // 90° → tan(45°) = 1
    c.near_z = 0.5F;
    c.far_z  = 100.0F;
    const float aspect = 16.0F / 9.0F;
    const auto m = cd::camera::projection_matrix(c, aspect);

    const float tan_half = std::tan(c.fov_y * 0.5F);  // == 1
    EXPECT_NEAR(m[0][0], 1.0F / (aspect * tan_half), kProjEps);
    EXPECT_NEAR(m[1][1], 1.0F / tan_half, kProjEps);
    EXPECT_NEAR(m[2][2], c.far_z / (c.near_z - c.far_z), kProjEps);
    EXPECT_FLOAT_EQ(m[2][3], -1.0F);
    EXPECT_NEAR(m[3][2], (c.near_z * c.far_z) / (c.near_z - c.far_z), kProjEps);
    EXPECT_FLOAT_EQ(m[3][3], 0.0F);  // perspective: no affine w term
}

TEST(CameraProjection, PerspectiveNarrowFovEnlargesFocalScale)
{
    // A tiny fov shrinks tan(fov/2), which BLOWS UP m[1][1] = 1/tan_half.
    cd::camera::Camera wide {};
    wide.fov_y = 2.0F;
    cd::camera::Camera narrow {};
    narrow.fov_y = 0.05F;
    const auto mw = cd::camera::projection_matrix(wide, 1.0F);
    const auto mn = cd::camera::projection_matrix(narrow, 1.0F);
    EXPECT_GT(mn[1][1], mw[1][1]);
    EXPECT_TRUE(std::isfinite(mn[1][1]));
}

TEST(CameraProjection, PerspectiveDepthRowMapsNearAndFar)
{
    // Verify the [0,1] depth convention end-to-end: a clip-space point on the
    // near plane maps to ndc.z == 0, on the far plane to ndc.z == 1.
    cd::camera::Camera c {};
    c.fov_y  = 1.0F;
    c.near_z = 0.25F;
    c.far_z  = 40.0F;
    const auto m = cd::camera::projection_matrix(c, 1.5F);

    // View-space point on the near plane is at z = -near_z (RH looks down -Z).
    const auto project_z = [&](float view_z) {
        const float clip_z = m[2][2] * view_z + m[3][2];  // column-major
        const float clip_w = m[2][3] * view_z;            // == -view_z
        return clip_z / clip_w;
    };
    EXPECT_NEAR(project_z(-c.near_z), 0.0F, 1e-3F);
    EXPECT_NEAR(project_z(-c.far_z),  1.0F, 1e-3F);
}

TEST(CameraProjection, PerspectiveTightNearFarStaysFinite)
{
    cd::camera::Camera c {};
    c.fov_y  = 1.0F;
    c.near_z = 0.001F;
    c.far_z  = 1.0e6F;
    const auto m = cd::camera::projection_matrix(c, 2.0F);
    for (std::size_t col = 0; col < 4; ++col)
        for (std::size_t row = 0; row < 4; ++row)
            EXPECT_TRUE(std::isfinite(m[col][row])) << "col=" << col << " row=" << row;
}

TEST(CameraProjection, ProjectionZeroAspectFallsBackToUnity)
{
    cd::camera::Camera c {};
    c.fov_y = 1.0F;
    const auto m_zero = cd::camera::projection_matrix(c, 0.0F);
    const auto m_neg  = cd::camera::projection_matrix(c, -4.0F);
    const auto m_unit = cd::camera::projection_matrix(c, 1.0F);
    // Both degenerate aspects substitute the 1:1 fallback → identical to unity.
    EXPECT_FLOAT_EQ(m_zero[0][0], m_unit[0][0]);
    EXPECT_FLOAT_EQ(m_neg[0][0],  m_unit[0][0]);
}

// ---- Orthographic frustum extraction known-value boundary -------------------

TEST(CameraProjection, OrthographicKnownValuesAndFrustumExtraction)
{
    // Symmetric ortho box: a unit cube at the origin must sit fully inside.
    const auto vp = cd::math::ortho(-2.0F, 2.0F, -2.0F, 2.0F, 0.1F, 10.0F);
    EXPECT_FLOAT_EQ(vp[0][0], 2.0F / 4.0F);   // 2/(right-left)
    EXPECT_FLOAT_EQ(vp[1][1], 2.0F / 4.0F);   // 2/(top-bottom)
    EXPECT_FLOAT_EQ(vp[3][3], 1.0F);          // affine ortho keeps w term

    const auto f = cd::camera::extract_frustum(vp);
    // RIGHT-HANDED ortho (depth [0,1]): the visible depth range is z∈[-far,-near]
    // = [-10,-0.1], so an inside box must have negative z (in front of the camera).
    const auto r = cd::camera::test_aabb(f, { -0.5F, -0.5F, -2.0F }, { 0.5F, 0.5F, -1.0F });
    EXPECT_EQ(r, cd::camera::CullResult::kInside);
    // A box pushed past the right ortho wall is rejected.
    const auto r_out = cd::camera::test_aabb(f, { 5.0F, -0.5F, -2.0F }, { 6.0F, 0.5F, -1.0F });
    EXPECT_EQ(r_out, cd::camera::CullResult::kOutside);
}

// ---- Frustum plane extraction: normals are unit + inward-facing -------------

TEST(Frustum, ExtractedPlaneNormalsAreNormalised)
{
    cd::camera::Camera c {};
    c.eye = { 0.0F, 0.0F, 4.0F };
    c.target = { 0.0F, 0.0F, 0.0F };
    const auto f = cd::camera::extract_frustum(c, 1.3F);
    for (const auto& p : f.planes)
    {
        const float len = std::sqrt(p.normal[0] * p.normal[0] +
                                    p.normal[1] * p.normal[1] +
                                    p.normal[2] * p.normal[2]);
        EXPECT_NEAR(len, 1.0F, 1e-4F);
    }
}

TEST(Frustum, NamedFaceIndicesMatchEnumOrder)
{
    // Lock the documented left/right/bottom/top/near/far ordering.
    EXPECT_EQ(static_cast<std::size_t>(cd::camera::FrustumFace::kLeft),   0U);
    EXPECT_EQ(static_cast<std::size_t>(cd::camera::FrustumFace::kRight),  1U);
    EXPECT_EQ(static_cast<std::size_t>(cd::camera::FrustumFace::kBottom), 2U);
    EXPECT_EQ(static_cast<std::size_t>(cd::camera::FrustumFace::kTop),    3U);
    EXPECT_EQ(static_cast<std::size_t>(cd::camera::FrustumFace::kNear),   4U);
    EXPECT_EQ(static_cast<std::size_t>(cd::camera::FrustumFace::kFar),    5U);
    EXPECT_EQ(cd::camera::kFrustumPlaneCount, 6U);
}

TEST(Frustum, AabbExactlyOnNearPlaneIsAccepted)
{
    // Boundary case: a thin slab whose front face sits on the near plane.
    cd::camera::Camera c {};
    c.eye = { 0.0F, 0.0F, 3.0F };
    c.target = { 0.0F, 0.0F, 0.0F };
    c.near_z = 0.5F;  // near plane at view z = -0.5 → world z = 2.5
    const auto f = cd::camera::extract_frustum(c, 1.0F);
    const auto r = cd::camera::test_aabb(f, { -0.1F, -0.1F, 2.4F }, { 0.1F, 0.1F, 2.6F });
    EXPECT_NE(r, cd::camera::CullResult::kOutside);
}

TEST(Frustum, SphereExactlyTangentToSidePlaneIsNotOutside)
{
    cd::camera::Camera c {};
    c.eye = { 0.0F, 0.0F, 5.0F };
    c.target = { 0.0F, 0.0F, 0.0F };
    c.fov_y = 1.0F;
    const auto f = cd::camera::extract_frustum(c, 1.0F);
    // Grow the radius until the sphere just reaches a side plane: a large
    // radius centred off-axis straddles rather than being culled.
    const auto r = cd::camera::test_sphere(f, { 1.5F, 0.0F, 0.0F }, 1.5F);
    EXPECT_NE(r, cd::camera::CullResult::kOutside);
}

// ---- FirstPerson / Orbit clamp boundaries -----------------------------------

TEST(FirstPersonController, LookClampsPitchAtBothPoles)
{
    cd::camera::FirstPersonController up {};
    up.look(0.0F, -1.0e6F);  // huge upward delta
    EXPECT_GT(up.pitch(), -1.56F);
    EXPECT_LT(up.pitch(), -1.55F);

    cd::camera::FirstPersonController down {};
    down.look(0.0F, 1.0e6F);
    EXPECT_GT(down.pitch(), 1.55F);
    EXPECT_LT(down.pitch(), 1.56F);
}

TEST(FirstPersonController, ZoomClampsToDistMinAndMax)
{
    cd::camera::FirstPersonController fp {};
    cd::camera::Camera c {};
    c.eye = { 0.0F, 0.0F, 5.0F };
    c.target = { 0.0F, 0.0F, 0.0F };
    fp.sync_from_camera(c);
    // Repeated zoom-in must not undercut dist_min.
    for (int i = 0; i < 200; ++i) fp.zoom(1.0F);
    EXPECT_GE(fp.dist(), fp.dist_min);
    // Repeated zoom-out must not exceed dist_max.
    for (int i = 0; i < 400; ++i) fp.zoom(-1.0F);
    EXPECT_LE(fp.dist(), fp.dist_max);
}

TEST(OrbitController, DragClampsElevationShortOfPole)
{
    cd::camera::Camera c {};
    c.eye = { 0.0F, 0.0F, 5.0F };
    c.target = { 0.0F, 0.0F, 0.0F };
    cd::camera::OrbitController orbit {};
    orbit.sync_from_camera(c);
    // Drag far past the top pole; eye must never coincide with target (radius
    // preserved) and Y must stay below the full radius (elevation < π/2).
    orbit.drag(0.0F, 1.0e6F);
    orbit.apply(c);
    const float r = std::sqrt(c.eye[0] * c.eye[0] + c.eye[1] * c.eye[1] + c.eye[2] * c.eye[2]);
    EXPECT_NEAR(r, 5.0F, 1e-2F);
    EXPECT_LT(c.eye[1], r);  // not fully at the pole
}

TEST(OrbitController, ZoomClampsToRadiusWindow)
{
    cd::camera::Camera c {};
    c.eye = { 0.0F, 0.0F, 5.0F };
    c.target = { 0.0F, 0.0F, 0.0F };
    cd::camera::OrbitController orbit {};
    orbit.sync_from_camera(c);
    for (int i = 0; i < 300; ++i) orbit.zoom(1.0F);  // zoom in hard
    orbit.apply(c);
    float r = std::sqrt(c.eye[0] * c.eye[0] + c.eye[1] * c.eye[1] + c.eye[2] * c.eye[2]);
    EXPECT_GE(r, orbit.radius_min - 1e-3F);
    for (int i = 0; i < 600; ++i) orbit.zoom(-1.0F);  // zoom out hard
    orbit.apply(c);
    r = std::sqrt(c.eye[0] * c.eye[0] + c.eye[1] * c.eye[1] + c.eye[2] * c.eye[2]);
    EXPECT_LE(r, orbit.radius_max + 1e-1F);
}

// ---- CameraPath sample boundaries -------------------------------------------

TEST(CameraPath, SampleClampsBelowFirstAndAboveLast)
{
    cd::camera::CameraPath p;
    p.add_key(cd::camera::CameraKey { 1.0F, { 0, 0, 0 }, { 0, 0, -1 } });
    p.add_key(cd::camera::CameraKey { 2.0F, { 10, 0, 0 }, { 10, 0, -1 } });
    p.add_key(cd::camera::CameraKey { 3.0F, { 20, 0, 0 }, { 20, 0, -1 } });
    // Out-of-range LOW clamps to first key.
    const auto lo = p.sample(-100.0F);
    EXPECT_FLOAT_EQ(lo.eye.x, 0.0F);
    // Out-of-range HIGH clamps to last key.
    const auto hi = p.sample(100.0F);
    EXPECT_FLOAT_EQ(hi.eye.x, 20.0F);
}

TEST(CameraPath, SampleExactlyAtFirstAndLastKeyTimes)
{
    cd::camera::CameraPath p;
    p.add_key(cd::camera::CameraKey { 0.0F, { 1, 0, 0 }, { 2, 0, 0 } });
    p.add_key(cd::camera::CameraKey { 1.0F, { 9, 0, 0 }, { 8, 0, 0 } });
    EXPECT_FLOAT_EQ(p.sample(0.0F).eye.x, 1.0F);
    EXPECT_FLOAT_EQ(p.sample(1.0F).eye.x, 9.0F);
}

TEST(CameraPath, MidpointInterpolatesBetweenKeys)
{
    cd::camera::CameraPath p;
    p.add_key(cd::camera::CameraKey { 0.0F, { 0, 0, 0 }, { 0, 0, -1 } });
    p.add_key(cd::camera::CameraKey { 2.0F, { 4, 0, 0 }, { 4, 0, -1 } });
    const auto mid = p.sample(1.0F);  // halfway, two-key path → LERP fallback
    EXPECT_GT(mid.eye.x, 0.0F);
    EXPECT_LT(mid.eye.x, 4.0F);
    EXPECT_NEAR(mid.eye.x, 2.0F, 1e-4F);
}

TEST(CameraPath, AddKeyKeepsSortedOnOutOfOrderInsertion)
{
    cd::camera::CameraPath p;
    p.add_key(cd::camera::CameraKey { 3.0F, { 30, 0, 0 }, {} });
    p.add_key(cd::camera::CameraKey { 1.0F, { 10, 0, 0 }, {} });
    p.add_key(cd::camera::CameraKey { 2.0F, { 20, 0, 0 }, {} });
    ASSERT_EQ(p.size(), 3U);
    const auto& ks = p.keys();
    EXPECT_FLOAT_EQ(ks[0].t, 1.0F);
    EXPECT_FLOAT_EQ(ks[1].t, 2.0F);
    EXPECT_FLOAT_EQ(ks[2].t, 3.0F);
}

TEST(CameraPath, EmptySampleTargetFacesNegativeZ)
{
    cd::camera::CameraPath p;
    const auto k = p.sample(0.25F);
    EXPECT_FLOAT_EQ(k.target.x, 0.0F);
    EXPECT_FLOAT_EQ(k.target.y, 0.0F);
    EXPECT_FLOAT_EQ(k.target.z, -1.0F);
}

// ---- Lens FOV/focal round-trip + edge ----------------------------------------

TEST(Lens, FovFocalRoundTrip)
{
    // fov_y = 2*atan(h / (2f))  ⇒  f = h / (2 * tan(fov/2)).
    const float sensor_h = 24.0F;
    cd::camera::Lens l { 35.0F, 2.0F, 5.0F };
    const float fov = l.fov_y_for(sensor_h);
    const float recovered_focal = sensor_h / (2.0F * std::tan(fov * 0.5F));
    EXPECT_NEAR(recovered_focal, l.focal_length_mm, 1e-3F);
}

TEST(Lens, LongerFocalNarrowsFov)
{
    cd::camera::Lens short_lens { 18.0F, 2.8F, 5.0F };
    cd::camera::Lens long_lens  { 135.0F, 2.8F, 5.0F };
    EXPECT_GT(short_lens.fov_y_for(), long_lens.fov_y_for());
}

TEST(Lens, NegativeFocalLengthReturnsSafeFallback)
{
    cd::camera::Lens l { -10.0F, 1.4F, 5.0F };
    EXPECT_FLOAT_EQ(l.fov_y_for(), 1.0F);
}

TEST(Lens, ApplyToZeroFocalLeavesFovAtFallback)
{
    cd::camera::Camera cam;
    cd::camera::Lens l { 0.0F, 1.4F, 5.0F };
    l.apply_to(cam);
    EXPECT_FLOAT_EQ(cam.fov_y, 1.0F);
}

// ---- look_at degenerate cases (eye==target, up∥forward) ----------------------

TEST(CameraView, LookAtEyeEqualsTargetStaysFinite)
{
    // normalize(0) returns 0 (not NaN) per cd::math; the view stays finite.
    cd::camera::Camera c {};
    c.eye = { 1.0F, 2.0F, 3.0F };
    c.target = { 1.0F, 2.0F, 3.0F };
    const auto m = cd::camera::view_matrix(c);
    for (std::size_t col = 0; col < 4; ++col)
        for (std::size_t row = 0; row < 4; ++row)
            EXPECT_TRUE(std::isfinite(m[col][row])) << "col=" << col << " row=" << row;
}

TEST(CameraView, LookAtUpParallelToForwardStaysFinite)
{
    // up parallel to forward → cross == 0 → normalize(0) == 0, still finite.
    cd::camera::Camera c {};
    c.eye = { 0.0F, 0.0F, 0.0F };
    c.target = { 0.0F, 5.0F, 0.0F };  // forward = +Y
    c.up = { 0.0F, 1.0F, 0.0F };      // up == forward
    const auto m = cd::camera::view_matrix(c);
    for (std::size_t col = 0; col < 4; ++col)
        for (std::size_t row = 0; row < 4; ++row)
            EXPECT_TRUE(std::isfinite(m[col][row])) << "col=" << col << " row=" << row;
}

TEST(CameraView, ViewProjectionWayBehindFarStaysFinite)
{
    cd::camera::Camera c {};
    c.eye = { 0.0F, 0.0F, 1.0e7F };
    c.target = { 0.0F, 0.0F, 0.0F };
    const auto m = cd::camera::view_projection(c, 1.7778F);
    for (std::size_t col = 0; col < 4; ++col)
        for (std::size_t row = 0; row < 4; ++row)
            EXPECT_TRUE(std::isfinite(m[col][row])) << "col=" << col << " row=" << row;
}

// ---- Camera::auto_frame_aabb degenerate (zero-extent box) -------------------

TEST(Camera, AutoFrameDegenerateZeroExtentBoxStaysFinite)
{
    // A point box (min == max) → radius 0; the helper floors the distance so
    // near/far stay positive and ordered, eye stays finite.
    const cd::math::Vec3f pt { 3.0F, 3.0F, 3.0F };
    const auto c = cd::camera::auto_frame_aabb(pt, pt);
    EXPECT_FLOAT_EQ(c.target[0], 3.0F);
    EXPECT_FLOAT_EQ(c.target[1], 3.0F);
    EXPECT_FLOAT_EQ(c.target[2], 3.0F);
    EXPECT_GT(c.far_z, c.near_z);
    EXPECT_GT(c.near_z, 0.0F);
    EXPECT_TRUE(std::isfinite(c.eye[0]));
    EXPECT_TRUE(std::isfinite(c.eye[1]));
    EXPECT_TRUE(std::isfinite(c.eye[2]));
}

TEST(Camera, AutoFrameDistanceScaleBacksCameraOff)
{
    const cd::math::Vec3f mn { -1.0F, -1.0F, -1.0F };
    const cd::math::Vec3f mx { 1.0F, 1.0F, 1.0F };
    const auto near_cam = cd::camera::auto_frame_aabb(mn, mx, 2.5F);
    const auto far_cam  = cd::camera::auto_frame_aabb(mn, mx, 6.0F);
    const auto dist = [](const cd::camera::Camera& c) {
        const float dx = c.eye[0] - c.target[0];
        const float dy = c.eye[1] - c.target[1];
        const float dz = c.eye[2] - c.target[2];
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    };
    EXPECT_GT(dist(far_cam), dist(near_cam));
}

// ---- ViewportInfo edge: offset rect screen↔NDC + non-square aspect ----------

TEST(ViewportInfo, ScreenToNdcCenterIsZero)
{
    cd::camera::ViewportInfo v { 0, 0, 1000, 500 };
    float xn = 0.0F;
    float yn = 0.0F;
    cd::camera::screen_to_ndc(v, 500.0F, 250.0F, xn, yn);
    EXPECT_NEAR(xn, 0.0F, 1e-5F);
    EXPECT_NEAR(yn, 0.0F, 1e-5F);
}

TEST(ViewportInfo, ScreenToNdcZeroDimsDoesNotDivideByZero)
{
    cd::camera::ViewportInfo v { 0, 0, 0, 0 };
    float xn = 0.0F;
    float yn = 0.0F;
    cd::camera::screen_to_ndc(v, 0.0F, 0.0F, xn, yn);
    EXPECT_TRUE(std::isfinite(xn));
    EXPECT_TRUE(std::isfinite(yn));
}

TEST(ViewportInfo, RoundTripOffsetRectAllCorners)
{
    cd::camera::ViewportInfo v { 64, 32, 1280, 720 };
    const std::array<std::array<float, 2>, 4> corners {{
        {{ 64.0F, 32.0F }}, {{ 1344.0F, 32.0F }}, {{ 64.0F, 752.0F }}, {{ 1344.0F, 752.0F }}
    }};
    for (const auto& cpt : corners)
    {
        float xn = 0.0F;
        float yn = 0.0F;
        cd::camera::screen_to_ndc(v, cpt[0], cpt[1], xn, yn);
        float px = 0.0F;
        float py = 0.0F;
        cd::camera::ndc_to_screen(v, xn, yn, px, py);
        EXPECT_NEAR(px, cpt[0], 1e-2F);
        EXPECT_NEAR(py, cpt[1], 1e-2F);
    }
}
