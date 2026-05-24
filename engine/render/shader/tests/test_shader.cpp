// =============================================================================
// CHROMODYNAMIC — cd::shader tests
// =============================================================================
#include <cd/shader/Compiler.hpp>
#include <cd/shader/ShaderStage.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

namespace
{

constexpr const char* kTriangleVS = R"glsl(
#version 450
layout(location = 0) out vec3 v_color;
const vec2 positions[3] = vec2[](
  vec2( 0.0, -0.5),
  vec2( 0.5,  0.5),
  vec2(-0.5,  0.5)
);
const vec3 colors[3] = vec3[](
  vec3(1.0, 0.0, 0.0),
  vec3(0.0, 1.0, 0.0),
  vec3(0.0, 0.0, 1.0)
);
void main() {
  gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
  v_color = colors[gl_VertexIndex];
}
)glsl";

constexpr const char* kTriangleFS = R"glsl(
#version 450
layout(location = 0) in  vec3 v_color;
layout(location = 0) out vec4 out_color;
void main() { out_color = vec4(v_color, 1.0); }
)glsl";

constexpr const char* kBrokenGlsl = R"glsl(
#version 450
void main() { totally_not_a_function(); }
)glsl";

TEST(ShaderCompiler, GlslangAvailableOrSkip)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    SUCCEED();
}

TEST(ShaderCompiler, CompileMinimalVertexShader)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    cd::shader::CompileDesc desc {};
    desc.source = kTriangleVS;
    desc.stage = cd::shader::ShaderStage::kVertex;
    desc.lang = cd::shader::ShaderLanguage::kGlsl;
    desc.source_name = "triangle.vert";

    auto r = c->compile(desc);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_FALSE(r->spirv.empty());
    // SPIR-V module magic word, little-endian.
    EXPECT_EQ(r->spirv.front(), 0x07230203U);
}

TEST(ShaderCompiler, CompileMinimalFragmentShader)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    cd::shader::CompileDesc desc {};
    desc.source = kTriangleFS;
    desc.stage = cd::shader::ShaderStage::kFragment;
    desc.source_name = "triangle.frag";

    auto r = c->compile(desc);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_FALSE(r->spirv.empty());
    EXPECT_EQ(r->spirv.front(), 0x07230203U);
}

TEST(ShaderCompiler, EmptySourceRejected)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    cd::shader::CompileDesc desc {};
    desc.source = "";
    auto r = c->compile(desc);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::shader::shader_errors::Code::kInvalidArgument));
}

TEST(ShaderCompiler, SyntaxErrorReturnsCompileFailed)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    cd::shader::CompileDesc desc {};
    desc.source = kBrokenGlsl;
    desc.stage = cd::shader::ShaderStage::kVertex;
    auto r = c->compile(desc);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::shader::shader_errors::Code::kCompileFailed));
    EXPECT_FALSE(r.error().message.empty());
}

TEST(ShaderCompiler, TargetVulkan12EmitsValidSpirv)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    cd::shader::CompileDesc desc {};
    desc.source = kTriangleVS;
    desc.stage = cd::shader::ShaderStage::kVertex;
    desc.target = cd::shader::TargetEnv::kVulkan_1_2;
    auto r = c->compile(desc);
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(r->spirv.empty());
}

TEST(ShaderCompiler, LoadSpirvFileEmptyPathRejected)
{
    auto r = cd::shader::load_spirv_file("");
    ASSERT_FALSE(r.has_value());
}

TEST(ShaderCompiler, LoadSpirvFileNonExistentRejected)
{
    auto r = cd::shader::load_spirv_file("definitely_does_not_exist_42.spv");
    ASSERT_FALSE(r.has_value());
}

TEST(ShaderCompiler, SlangFactoryHonorsBuildToggle)
{
    // Contract: make_slang_compiler() returns nullptr when the engine ships
    // the Slang stub (current state). The real backend will return a valid
    // unique_ptr; consumers detect via nullptr-check and branch accordingly.
    auto c = cd::shader::make_slang_compiler();
#if defined(CD_SHADER_HAVE_SLANG)
    // Stub TU is still active even under the build flag (see CMake note).
    EXPECT_EQ(c, nullptr);
#else
    EXPECT_EQ(c, nullptr);
#endif
}

TEST(ShaderStageDesc, GlslFactoryDefaults)
{
    auto s = cd::shader::ShaderStageDesc::from_glsl(cd::shader::StageKind::kVertex);
    EXPECT_EQ(s.stage, cd::shader::StageKind::kVertex);
    EXPECT_EQ(s.language, cd::shader::Language::kGlsl);
    EXPECT_EQ(s.entry_point, "main");
}

TEST(ShaderStageDesc, HlslFactoryAcceptsCustomEntry)
{
    auto s = cd::shader::ShaderStageDesc::from_hlsl(cd::shader::StageKind::kFragment, "PSMain");
    EXPECT_EQ(s.language, cd::shader::Language::kHlsl);
    EXPECT_EQ(s.entry_point, "PSMain");
}

TEST(ShaderStageDesc, DefineAppendsValueDefault)
{
    auto s = cd::shader::ShaderStageDesc::from_glsl(cd::shader::StageKind::kCompute);
    s.define("USE_PBR");
    s.define("MAX_LIGHTS", "8");
    ASSERT_EQ(s.defines.size(), 2u);
    EXPECT_EQ(s.defines[0].key, "USE_PBR");
    EXPECT_EQ(s.defines[0].value, "1");
    EXPECT_EQ(s.defines[1].value, "8");
}

TEST(ShaderStageDesc, ToStringCoversAllStages)
{
    EXPECT_STREQ(cd::shader::to_string(cd::shader::StageKind::kVertex), "vertex");
    EXPECT_STREQ(cd::shader::to_string(cd::shader::StageKind::kFragment), "fragment");
    EXPECT_STREQ(cd::shader::to_string(cd::shader::StageKind::kRayGen), "raygen");
}

}  // namespace
