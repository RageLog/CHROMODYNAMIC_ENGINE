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

#include <gtest/gtest.h>

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

}  // namespace
