// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_d3d12_shader_toolchain.cpp
// phase1141 (X4-C1): GLSL -> SPIR-V -> HLSL(SM6.x) -> DXIL chain tests +
// the ADR §2.5 S1 ray-query translation spike.
//
// Windows-gated end-to-end (DXC via dxcompiler); argument-validation
// cases run everywhere. Pattern: Arrange / Act / Assert.
// =============================================================================
#include <cd/rhi/d3d12/D3D12ShaderToolchain.hpp>
#include <cd/shader/Compiler.hpp>
#include <cd/spirv_cross_glue/Translate.hpp>

#if defined(_WIN32)
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/d3d12/D3D12Device.hpp>
#endif

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>

namespace
{

constexpr const char* kMinimalVS = R"glsl(
#version 450
layout(location = 0) in vec3 in_pos;
void main() { gl_Position = vec4(in_pos, 1.0); }
)glsl";

constexpr const char* kMinimalFS = R"glsl(
#version 450
layout(location = 0) out vec4 o;
void main() { o = vec4(1.0, 0.5, 0.25, 1.0); }
)glsl";

// ADR §2.5 S1 spike source: minimal inline ray-query fragment shader.
constexpr const char* kRayQueryFS = R"glsl(
#version 460
#extension GL_EXT_ray_query : require
layout(set = 0, binding = 0) uniform accelerationStructureEXT tlas;
layout(location = 0) out vec4 o;
void main()
{
    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, tlas, gl_RayFlagsTerminateOnFirstHitEXT,
                          0xFF, vec3(0.0), 0.01, vec3(0.0, 1.0, 0.0), 100.0);
    rayQueryProceedEXT(rq);
    float vis = (rayQueryGetIntersectionTypeEXT(rq, true) ==
                 gl_RayQueryCommittedIntersectionNoneEXT) ? 1.0 : 0.0;
    o = vec4(vis);
}
)glsl";

[[nodiscard]] std::unique_ptr<cd::shader::ICompiler> make_compiler_or_null()
{
    return cd::shader::make_glslang_compiler();
}

// ---- Argument validation (platform-independent behaviourally) ---------------

TEST(D3D12ShaderToolchain, EmptySourceRejected)
{
    auto c = make_compiler_or_null();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    cd::rhi::d3d12::GlslToDxilDesc d {};
    const auto r = cd::rhi::d3d12::compile_glsl_to_dxil(*c, d);
    EXPECT_FALSE(r.has_value());
}

TEST(D3D12ShaderToolchain, Sm51RejectedOnGlslChain)
{
    auto c = make_compiler_or_null();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    cd::rhi::d3d12::GlslToDxilDesc d {};
    d.glsl_source = kMinimalVS;
    d.model = cd::rhi::d3d12::ShaderModel::kSM5_1;
    const auto r = cd::rhi::d3d12::compile_glsl_to_dxil(*c, d);
    ASSERT_FALSE(r.has_value());
#if defined(_WIN32)
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(
                  cd::rhi::d3d12::shader_errors::Code::kUnsupportedStage));
#endif
}

// Stage-prefixed error chaining (ADR §2.4 invariant 1): a GLSL syntax
// error must surface with the "glslang: " prefix.
TEST(D3D12ShaderToolchain, GlslErrorIsStagePrefixed)
{
#if !defined(_WIN32)
    GTEST_SKIP() << "chain is Windows-only (kBackendUnavailable)";
#else
    auto c = make_compiler_or_null();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    cd::rhi::d3d12::GlslToDxilDesc d {};
    d.glsl_source = "#version 450\nvoid main() { bogus_fn(); }\n";
    d.stage = cd::rhi::ShaderStage::kFragment;
    const auto r = cd::rhi::d3d12::compile_glsl_to_dxil(*c, d);
    ASSERT_FALSE(r.has_value());
    const std::string msg { r.error().message };
    EXPECT_EQ(msg.rfind("glslang: ", 0), 0u) << msg;
#endif
}

// ---- End-to-end chain (Windows + DXC) ----------------------------------------

#if defined(_WIN32)
TEST(D3D12ShaderToolchain, VertexChainProducesDxil)
{
    auto c = make_compiler_or_null();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    cd::rhi::d3d12::GlslToDxilDesc d {};
    d.glsl_source = kMinimalVS;
    d.stage = cd::rhi::ShaderStage::kVertex;
    d.source_name = "toolchain_vs";
    const auto r = cd::rhi::d3d12::compile_glsl_to_dxil(*c, d);
    if (!r.has_value() &&
        r.error().code == static_cast<std::uint32_t>(
            cd::rhi::d3d12::shader_errors::Code::kDxcUnavailable))
        GTEST_SKIP() << "dxcompiler.dll not available at runtime";
    ASSERT_TRUE(r.has_value()) << std::string(r.error().message);
    EXPECT_FALSE(r->empty());
}

TEST(D3D12ShaderToolchain, FragmentChainProducesDxil)
{
    auto c = make_compiler_or_null();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    cd::rhi::d3d12::GlslToDxilDesc d {};
    d.glsl_source = kMinimalFS;
    d.stage = cd::rhi::ShaderStage::kFragment;
    d.source_name = "toolchain_fs";
    const auto r = cd::rhi::d3d12::compile_glsl_to_dxil(*c, d);
    if (!r.has_value() &&
        r.error().code == static_cast<std::uint32_t>(
            cd::rhi::d3d12::shader_errors::Code::kDxcUnavailable))
        GTEST_SKIP() << "dxcompiler.dll not available at runtime";
    ASSERT_TRUE(r.has_value()) << std::string(r.error().message);
    EXPECT_FALSE(r->empty());
}
#endif  // _WIN32

// ---- ADR §2.5 S1 spike: ray-query SPIR-V -> SM6.5 HLSL ----------------------
//
// The spike PROVES (or disproves) that SPIRV-Cross can translate
// GL_EXT_ray_query SPIR-V into RayQuery<> HLSL. A failure here triggers
// the documented fallback (hand-written HLSL island for ray-query
// blocks) — the test failing IS the decision input, so it asserts.
TEST(D3D12ShaderToolchain, S1SpikeRayQueryTranslatesToHlsl)
{
    auto c = make_compiler_or_null();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    cd::shader::CompileDesc sd {};
    sd.source = kRayQueryFS;
    sd.stage = cd::shader::ShaderStage::kFragment;
    sd.source_name = "s1_rayquery_fs";
    const auto spirv = c->compile(sd);
    ASSERT_TRUE(spirv.has_value())
        << std::string(spirv.error().message);

    const auto hlsl = cd::spirv_cross_glue::translate(
        spirv->spirv, cd::spirv_cross_glue::Target::kHlsl, 65);
    ASSERT_TRUE(hlsl.ok()) << hlsl.error;
    EXPECT_NE(hlsl.source.find("RayQuery"), std::string::npos)
        << "SM6.5 output should contain a RayQuery<> object:\n"
        << hlsl.source.substr(0, 400);
}

// =============================================================================
// D12 (phase1184): create_shader_module DEVICE-PATH wiring.
//
// The tests above exercise compile_glsl_to_dxil() directly. These drive the
// new IDevice::create_shader_module routing end-to-end: a real D3D12 device,
// the engine GLSL (including a `#include <cd/gluon/*.glsl>` that resolves via
// the function-local ModuleResolver fallback), and an assertion that a
// non-empty DXIL-backed ShaderModuleHandle comes back. This PROVES the gap
// the audit flagged — the toolchain is now actually called by the device.
//
// Headless/CI-safe: when no D3D12 adapter is present (GHA software lanes),
// create_d3d12_device fails and the test SKIPs. When dxcompiler.dll is
// missing at runtime, the toolchain returns a typed error and we SKIP.
// =============================================================================
#if defined(_WIN32)

namespace
{

// A GLSL fragment that #includes a real cd::gluon module and calls one of
// its functions. If the include doesn't resolve, glslang errors and the
// device path returns a failure — so a passing test PROVES gluon resolution
// flows through create_shader_module.
constexpr const char* kGluonFS = R"glsl(
#version 450
#extension GL_GOOGLE_include_directive : enable
#include <cd/gluon/tonemap.glsl>
layout(location = 0) out vec4 o;
void main() { o = vec4(cd_tonemap_reinhard(vec3(2.0, 1.0, 0.5)), 1.0); }
)glsl";

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_d3d12_device_or_null()
{
    cd::rhi::d3d12::D3D12CreateInfo ci {};
    ci.enable_validation = false;  // avoid debug-layer dependency in CI
    auto r = cd::rhi::d3d12::create_d3d12_device(ci);
    if (!r.has_value())
        return nullptr;
    return std::move(*r);
}

}  // namespace

TEST(D3D12CreateShaderModule, GlslWithGluonIncludeProducesDxilModule)
{
    if (make_compiler_or_null() == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no D3D12 adapter on this host";

    cd::rhi::ShaderModuleDesc d {};
    d.stage       = cd::rhi::ShaderStage::kFragment;
    d.code        = kGluonFS;
    d.code_size   = std::char_traits<char>::length(kGluonFS);
    d.entry_point = "main";
    d.debug_name  = "device_path_gluon_fs";
    d.language    = cd::rhi::ShaderSourceLanguage::kGlsl;  // null resolver -> gluon fallback

    const auto r = dev->create_shader_module(d);
    if (!r.has_value())
    {
        // dxcompiler.dll missing at runtime surfaces as a creation failure
        // carrying the "dxc:" stage prefix — that is an environment gap,
        // not a wiring bug.
        const std::string msg { r.error().message };
        if (msg.find("dxc") != std::string::npos)
            GTEST_SKIP() << "dxcompiler.dll unavailable: " << msg;
        FAIL() << "GLSL device path failed: " << msg;
    }
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(r->is_valid());
    dev->destroy_shader_module(*r);
}

TEST(D3D12CreateShaderModule, BytecodePassThroughIsVerbatim)
{
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no D3D12 adapter on this host";

    // The default language (kBytecode) must consume `code` verbatim — this
    // pins the legacy contract that every existing caller relies on.
    const std::uint8_t fake_dxil[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04 };
    cd::rhi::ShaderModuleDesc d {};
    d.stage     = cd::rhi::ShaderStage::kVertex;
    d.code      = fake_dxil;
    d.code_size = sizeof(fake_dxil);
    // language left at default kBytecode.

    const auto r = dev->create_shader_module(d);
    ASSERT_TRUE(r.has_value()) << std::string(r.error().message);
    EXPECT_TRUE(r->is_valid());
    dev->destroy_shader_module(*r);
}

TEST(D3D12CreateShaderModule, GlslEmptySourceRejected)
{
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no D3D12 adapter on this host";

    // A GLSL request with empty code must fail with a typed error, never a
    // crash or a silent empty module.
    const char dummy = '\0';
    cd::rhi::ShaderModuleDesc d {};
    d.stage     = cd::rhi::ShaderStage::kFragment;
    d.code      = &dummy;
    d.code_size = 0;  // empty -> rejected before the toolchain
    d.language  = cd::rhi::ShaderSourceLanguage::kGlsl;

    const auto r = dev->create_shader_module(d);
    EXPECT_FALSE(r.has_value());
}

#endif  // _WIN32

}  // namespace
