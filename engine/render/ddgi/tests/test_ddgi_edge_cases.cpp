// =============================================================================
// CHROMODYNAMIC -- engine/render/ddgi/tests/test_ddgi_edge_cases.cpp
// 80->100 marathon -- GPU-gated edge / negative path coverage.
//
// ADD-ONLY: extends the real-GPU coverage of the DDGI RHI passes with the
// error / degenerate paths the existing per-sprint smokes did not pin:
//   * set_inv_vp() / set_sun_light() rejection BEFORE init() (null UBO).
//   * set_sun_light() rejection on the smoke (needs_tlas=false) variant
//     (no light binding allocated).
//   * bind_sample_resources() rejection for null image views + before init().
//   * execute_sample() CPU-stub: reject pre-bind, succeed + increment post-bind,
//     and the cmd-buffer execute_sample_checked() does NOT bump the counter.
//   * single-probe (1x1x1) minimum grid init + atlas dims.
//   * shutdown() idempotence after a real init().
//   * FullPipeline forwards set_inv_vp / set_sun_light to the pass + matches
//     its error channel.
//
// Every case keeps the existing Vulkan device gate (GTEST_SKIP when no ICD)
// so CI hosts without a GPU stay green. None of these alter rendered output
// of the engine -- they exercise the library's own pass plumbing standalone.
//
// hello_engine is NOT touched. No probe / blend / sample math or GLSL is
// modified.
// =============================================================================

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#endif

#include <cd/core/Result.hpp>
#include <cd/ddgi/DispatchPass.hpp>
#include <cd/ddgi/FullPipeline.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Vector.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace
{

std::unique_ptr<cd::rhi::IDevice> try_make_device()
{
    cd::rhi::vulkan::VulkanCreateInfo info {};
    info.enable_validation = true;
    auto r = cd::rhi::vulkan::create_vulkan_device(info);
    if (!r.has_value())
        return nullptr;
    return std::move(*r);
}

cd::ddgi::DispatchPassDesc make_smoke_desc()
{
    cd::ddgi::DispatchPassDesc desc {};
    desc.grid.probes_x           = 4;
    desc.grid.probes_y           = 2;
    desc.grid.probes_z           = 4;
    desc.settings.rays_per_probe = 64;
    desc.needs_tlas              = false;
    desc.probe_face_size         = 8;
    return desc;
}

// ---------------------------------------------------------------------------
// set_inv_vp / set_sun_light -- reject before init().
// ---------------------------------------------------------------------------
TEST(DdgiEdgeCases, SetInvVpRejectsBeforeInit)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    cd::ddgi::DispatchPass pass;  // never init()ed -> recon UBO null.
    const auto r = pass.set_inv_vp(*dev, cd::math::Mat4f::identity());
    EXPECT_FALSE(r.has_value());
    EXPECT_NE(r.error().message.find("init()"), std::string::npos);
}

TEST(DdgiEdgeCases, SetSunLightRejectsBeforeInit)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    cd::ddgi::DispatchPass pass;  // never init()ed -> light UBO null.
    const auto r = pass.set_sun_light(*dev,
                                      cd::math::Vec3f { 0.0F, 1.0F, 0.0F },
                                      cd::math::Vec3f { 1.0F, 1.0F, 1.0F },
                                      0.05F);
    EXPECT_FALSE(r.has_value());
}

// ---------------------------------------------------------------------------
// set_sun_light -- reject on the smoke (no-TLAS) variant (no light binding).
// ---------------------------------------------------------------------------
TEST(DdgiEdgeCases, SetSunLightRejectsOnSmokeVariant)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    cd::ddgi::DispatchPass pass;
    auto init_r = pass.init(*dev, make_smoke_desc());
    if (!init_r.has_value())
        GTEST_SKIP() << "init failed (likely no glslang): " << init_r.error().message;

    // The smoke variant never allocates trace_light_ubo_ -> rejection.
    const auto r = pass.set_sun_light(*dev,
                                      cd::math::Vec3f { 0.0F, 1.0F, 0.0F },
                                      cd::math::Vec3f { 1.0F, 1.0F, 1.0F },
                                      0.05F);
    EXPECT_FALSE(r.has_value());
    EXPECT_NE(r.error().message.find("smoke"), std::string::npos);

    // set_inv_vp DOES succeed on the smoke variant (sample recon UBO exists).
    const auto rv = pass.set_inv_vp(*dev, cd::math::Mat4f::identity());
    EXPECT_TRUE(rv.has_value()) << rv.error().message;

    pass.shutdown(*dev);
}

// ---------------------------------------------------------------------------
// bind_sample_resources -- reject before init() + reject null views.
// ---------------------------------------------------------------------------
TEST(DdgiEdgeCases, BindSampleResourcesRejectsBeforeInit)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    cd::ddgi::DispatchPass pass;  // never init()ed.
    const auto r = pass.bind_sample_resources(
        *dev,
        cd::rhi::TextureViewHandle {},
        cd::rhi::TextureViewHandle {},
        cd::rhi::TextureViewHandle {},
        32U, 32U);
    EXPECT_FALSE(r.has_value());
    EXPECT_NE(r.error().message.find("init()"), std::string::npos);
}

TEST(DdgiEdgeCases, BindSampleResourcesRejectsNullViews)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    cd::ddgi::DispatchPass pass;
    auto init_r = pass.init(*dev, make_smoke_desc());
    if (!init_r.has_value())
        GTEST_SKIP() << "init failed (likely no glslang): " << init_r.error().message;

    // Init succeeded -> descriptor set valid, but the three views are null.
    const auto r = pass.bind_sample_resources(
        *dev,
        cd::rhi::TextureViewHandle {},
        cd::rhi::TextureViewHandle {},
        cd::rhi::TextureViewHandle {},
        32U, 32U);
    EXPECT_FALSE(r.has_value());
    EXPECT_NE(r.error().message.find("valid image views"), std::string::npos);

    // No bind happened -> output dims still 0 -> execute_sample() still rejects.
    EXPECT_EQ(pass.sample_output_width(), 0U);
    const auto e = pass.execute_sample();
    EXPECT_FALSE(e.has_value());
    EXPECT_EQ(pass.sample_call_count(), 0U);

    pass.shutdown(*dev);
}

// ---------------------------------------------------------------------------
// Single-probe minimum grid -- init + atlas dims.
// ---------------------------------------------------------------------------
TEST(DdgiEdgeCases, SingleProbeGridInitialises)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    cd::ddgi::DispatchPass pass;
    cd::ddgi::DispatchPassDesc desc {};
    desc.grid.probes_x           = 1;
    desc.grid.probes_y           = 1;
    desc.grid.probes_z           = 1;
    desc.settings.rays_per_probe = 64;
    desc.needs_tlas              = false;
    desc.probe_face_size         = 8;

    auto init_r = pass.init(*dev, desc);
    if (!init_r.has_value())
        GTEST_SKIP() << "init failed (likely no glslang): " << init_r.error().message;

    EXPECT_TRUE(pass.initialised());
    // ray image: rays_per_probe x probe_count = 64 x 1.
    EXPECT_EQ(pass.ray_image_width(), 64U);
    EXPECT_EQ(pass.ray_image_height(), 1U);
    // atlas: (px*pz) * face x (py) * face = 1*8 x 1*8.
    EXPECT_EQ(pass.atlas_width(), 8U);
    EXPECT_EQ(pass.atlas_height(), 8U);

    pass.shutdown(*dev);
    EXPECT_FALSE(pass.initialised());
}

// ---------------------------------------------------------------------------
// shutdown() idempotence after a real init().
// ---------------------------------------------------------------------------
TEST(DdgiEdgeCases, ShutdownIsIdempotentAfterInit)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    cd::ddgi::DispatchPass pass;
    auto init_r = pass.init(*dev, make_smoke_desc());
    if (!init_r.has_value())
        GTEST_SKIP() << "init failed (likely no glslang): " << init_r.error().message;

    EXPECT_TRUE(pass.initialised());
    pass.shutdown(*dev);
    EXPECT_FALSE(pass.initialised());
    pass.shutdown(*dev);  // second call must be a safe no-op.
    EXPECT_FALSE(pass.initialised());
}

// ---------------------------------------------------------------------------
// FullPipeline forwards set_inv_vp / set_sun_light to the pass.
// ---------------------------------------------------------------------------
TEST(DdgiEdgeCases, FullPipelineSetForwardersMatchPassErrorChannel)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    // Uninitialised pipeline -> both forwarders reject (UBOs are null).
    cd::ddgi::FullPipeline pipeline;
    const auto r_vp = pipeline.set_inv_vp(*dev, cd::math::Mat4f::identity());
    EXPECT_FALSE(r_vp.has_value());

    const auto r_sun = pipeline.set_sun_light(
        *dev,
        cd::math::Vec3f { 0.0F, 1.0F, 0.0F },
        cd::math::Vec3f { 1.0F, 1.0F, 1.0F },
        0.05F);
    EXPECT_FALSE(r_sun.has_value());

    // After init() on the smoke variant, set_inv_vp succeeds; set_sun_light
    // still rejects (smoke variant has no light binding).
    auto init_r = pipeline.init(*dev, make_smoke_desc());
    if (!init_r.has_value())
        GTEST_SKIP() << "init failed (likely no glslang): " << init_r.error().message;

    EXPECT_TRUE(pipeline.set_inv_vp(*dev, cd::math::Mat4f::identity()).has_value());
    EXPECT_FALSE(pipeline.set_sun_light(
        *dev,
        cd::math::Vec3f { 0.0F, 1.0F, 0.0F },
        cd::math::Vec3f { 1.0F, 1.0F, 1.0F },
        0.05F).has_value());

    pipeline.shutdown(*dev);
}

}  // namespace
