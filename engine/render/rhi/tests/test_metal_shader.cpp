// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_metal_shader.cpp
//
// Backend-to-100 Wave 4b / C-METAL-TIER2 (docs/METAL_MAC_TESTING.md §3 Tier-2):
//   cd_test_metal_shader — create_shader_module(kGlsl) -> GLSL -> MSL ->
//   newLibraryWithSource: -> MTLLibrary -> MTLFunction (M6 in-device toolchain).
//   The HOST-SIDE GLSL->MSL text is already locked by
//   cd_test_metal_shader_toolchain; THIS test proves the .mm device path
//   actually compiles that MSL into a real MTLLibrary on the GPU driver.
//
// PLATFORM GATE (see test_metal_device.cpp for the full rationale): real
// MTLDevice on Apple; skip-stub everywhere else. Compiles + registers on
// Windows, runs on a Mac.
//
// Pattern: Arrange / Act / Assert. Deterministic; no sleep_for.
// =============================================================================
#if defined(__APPLE__) && defined(CD_RHI_METAL_ENABLED)

    #include <cd/rhi/Descriptors.hpp>
    #include <cd/rhi/Enums.hpp>
    #include <cd/rhi/Handles.hpp>
    #include <cd/rhi/IDevice.hpp>
    #include <cd/rhi/metal/MetalDevice.hpp>

    #include <gtest/gtest.h>

    #include <cstdint>
    #include <memory>
    #include <string>

namespace
{

constexpr const char* kVS = R"glsl(
#version 450
layout(location = 0) in vec3 in_pos;
void main() { gl_Position = vec4(in_pos, 1.0); }
)glsl";

constexpr const char* kBrokenGlsl =
    "#version 450\nvoid main() { this_is_not_a_function(); }\n";

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_metal_device_or_null()
{
    cd::rhi::metal::MetalCreateInfo ci {};
    ci.enable_validation = false;
    auto r = cd::rhi::metal::create_metal_device(ci);
    return r.has_value() ? std::move(*r) : nullptr;
}

// ---- M6: kGlsl -> MSL -> MTLLibrary on the real driver ----------------------
TEST(MetalShader, GlslModuleCompilesToMtlLibrary)
{
    auto dev = make_metal_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no Metal device on this Mac";
    auto& d = *dev;

    cd::rhi::ShaderModuleDesc sd {};
    sd.stage       = cd::rhi::ShaderStage::kVertex;
    sd.code        = kVS;
    sd.code_size   = std::char_traits<char>::length(kVS);
    sd.entry_point = "main";
    sd.language    = cd::rhi::ShaderSourceLanguage::kGlsl;

    auto r = d.create_shader_module(sd);
    ASSERT_TRUE(r.has_value())
        << "create_shader_module(kGlsl) must lower GLSL->MSL and compile a "
           "MTLLibrary on the driver: "
        << (r.has_value() ? std::string {}
                          : std::string(r.error().message.begin(),
                                        r.error().message.end()));
    EXPECT_TRUE(r->is_valid());
    d.destroy_shader_module(*r);
}

// A GLSL source with a compile error must surface a typed failure from the
// in-device toolchain, NOT a crash (negative / edge case).
TEST(MetalShader, BrokenGlslIsRejectedNotCrashing)
{
    auto dev = make_metal_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no Metal device on this Mac";
    auto& d = *dev;

    cd::rhi::ShaderModuleDesc sd {};
    sd.stage       = cd::rhi::ShaderStage::kFragment;
    sd.code        = kBrokenGlsl;
    sd.code_size   = std::char_traits<char>::length(kBrokenGlsl);
    sd.entry_point = "main";
    sd.language    = cd::rhi::ShaderSourceLanguage::kGlsl;

    const auto r = d.create_shader_module(sd);
    EXPECT_FALSE(r.has_value())
        << "a GLSL compile error must produce a typed module-creation failure";
}

}  // namespace

#else  // not (Apple && CD_RHI_METAL_ENABLED)

    #include <gtest/gtest.h>

TEST(MetalShader, SkippedOffApple)
{
    GTEST_SKIP() << "Metal backend disabled on this platform (Apple-only)";
}

#endif  // __APPLE__ && CD_RHI_METAL_ENABLED
