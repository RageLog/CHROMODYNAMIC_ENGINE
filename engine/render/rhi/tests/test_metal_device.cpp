// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_metal_device.cpp
//
// Backend-to-100 Wave 4b / C-METAL-TIER2 (docs/METAL_MAC_TESTING.md §3 Tier-2):
//   cd_test_metal_device — create_metal_device() returns kOk; adapter_name()
//   non-empty; backend() == kMetal (M1/M2 device bring-up).
//
// PLATFORM GATE. The Metal backend (cd_rhi_metal, .mm Objective-C++) compiles
// and links ONLY on Apple with CD_RHI_METAL_ENABLED=ON (auto-forced OFF on
// Windows — see engine/render/rhi/CMakeLists.txt:180-193). So:
//   * On Apple (__APPLE__ && CD_RHI_METAL_ENABLED): the body instantiates a
//     REAL MTLDevice via create_metal_device() and asserts the device surface.
//   * Everywhere else (Windows CI here): a single skip-stub TEST registers the
//     case and GTEST_SKIPs with a clear "Apple-only" message — so the file
//     COMPILES, the target BUILDS + the test REGISTERS + RUNs as SKIPPED on
//     Windows, and the real assertions RUN on a Mac. The Windows build pulls in
//     NO Metal symbols (the skip-stub needs only gtest).
//
// Pattern: Arrange / Act / Assert. No sleep_for; deterministic device calls.
// =============================================================================
#if defined(__APPLE__) && defined(CD_RHI_METAL_ENABLED)

    #include <cd/rhi/IDevice.hpp>
    #include <cd/rhi/metal/MetalDevice.hpp>

    #include <gtest/gtest.h>

    #include <memory>

namespace
{

// ---- M1/M2: create the real Metal device --------------------------------
//
// MTLCreateSystemDefaultDevice() returns a valid device on every Metal-capable
// Mac. create_metal_device() wraps it in the IDevice surface; a kOk result with
// a non-empty adapter name is the device-bring-up proof.
TEST(MetalDevice, CreateReturnsOkAndNamesAdapter)
{
    // Arrange / Act
    cd::rhi::metal::MetalCreateInfo ci {};
    ci.enable_validation = false;
    auto r = cd::rhi::metal::create_metal_device(ci);

    // Assert
    ASSERT_TRUE(r.has_value())
        << "create_metal_device must return kOk on a Metal-capable Mac: "
        << std::string(r.error().message);
    const std::unique_ptr<cd::rhi::IDevice>& dev = *r;
    ASSERT_NE(dev, nullptr);

    EXPECT_EQ(dev->backend(), cd::rhi::Backend::kMetal)
        << "the Metal device must report Backend::kMetal";
    EXPECT_FALSE(dev->adapter_name().empty())
        << "adapter_name() must report the MTLDevice.name (e.g. 'Apple M2')";
}

// The reported device limits must be sane (non-zero) on a real device — guards
// a device-init path that forgot to fill DeviceLimits from the MTLDevice caps.
TEST(MetalDevice, LimitsAreNonZero)
{
    auto r = cd::rhi::metal::create_metal_device({});
    if (!r.has_value())
        GTEST_SKIP() << "no Metal device on this Mac: "
                     << std::string(r.error().message);
    const cd::rhi::DeviceLimits& l = (*r)->limits();
    EXPECT_GT(l.max_texture_dimension_2d, 0u)
        << "max_texture_dimension_2d must be filled from the MTLDevice caps";
}

}  // namespace

#else  // not (Apple && CD_RHI_METAL_ENABLED)

    #include <gtest/gtest.h>

// Windows / non-Apple: register the case so it appears in the ctest list and
// SKIPs cleanly (never fails). No Metal symbols referenced — compiles + links
// with only gtest, so the Windows -Werror build stays clean.
TEST(MetalDevice, SkippedOffApple)
{
    GTEST_SKIP() << "Metal backend disabled on this platform (Apple-only)";
}

#endif  // __APPLE__ && CD_RHI_METAL_ENABLED
