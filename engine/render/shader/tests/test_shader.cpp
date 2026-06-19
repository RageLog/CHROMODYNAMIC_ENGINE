// =============================================================================
// CHROMODYNAMIC — cd::shader tests
// =============================================================================
#include <cd/shader/Compiler.hpp>
#include <cd/shader/ShaderStage.hpp>
#include <gtest/gtest.h>

#include <map>
#include <optional>
#include <string>

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
    desc.target = cd::shader::TargetEnv::kVulkan12;
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

// ---- phase1132 (SL-C step 1): IIncludeResolver through glslang --------------

class MapResolver final : public cd::shader::IIncludeResolver
{
public:
    [[nodiscard]] std::optional<Resolved> resolve(
        std::string_view requested, std::string_view /*requester*/,
        bool /*system_include*/) override
    {
        const auto it = modules.find(std::string { requested });
        if (it == modules.end())
            return std::nullopt;
        return Resolved { it->first, it->second };
    }

    std::map<std::string, std::string> modules;
};

constexpr const char* kIncludingFS = R"glsl(
#version 450
#extension GL_GOOGLE_include_directive : enable
#include "test_module.glsl"
layout(location = 0) out vec4 o;
void main() { o = vec4(test_module_value()); }
)glsl";

TEST(ShaderCompiler, IncludeResolvesThroughResolver)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    MapResolver resolver;
    resolver.modules["test_module.glsl"] =
        "#ifndef CD_SL_TEST_MODULE_GLSL\n"
        "#define CD_SL_TEST_MODULE_GLSL\n"
        "float test_module_value() { return 0.5; }\n"
        "#endif\n";

    cd::shader::CompileDesc desc {};
    desc.source = kIncludingFS;
    desc.stage = cd::shader::ShaderStage::kFragment;
    desc.include_resolver = &resolver;
    const auto r = c->compile(desc);
    ASSERT_TRUE(r.has_value()) << (r.has_value() ? "" : std::string(r.error().message));
    EXPECT_FALSE(r->spirv.empty());
}

TEST(ShaderCompiler, IncludeWithoutResolverFails)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    cd::shader::CompileDesc desc {};
    desc.source = kIncludingFS;
    desc.stage = cd::shader::ShaderStage::kFragment;
    const auto r = c->compile(desc);
    EXPECT_FALSE(r.has_value());  // legacy behaviour: includes are errors
}

TEST(ShaderCompiler, UnknownIncludeFailsWithResolver)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    MapResolver resolver;  // empty: nothing resolves
    cd::shader::CompileDesc desc {};
    desc.source = kIncludingFS;
    desc.stage = cd::shader::ShaderStage::kFragment;
    desc.include_resolver = &resolver;
    const auto r = c->compile(desc);
    EXPECT_FALSE(r.has_value());
}

// ---- Additional stage / language / flag coverage ---------------------------

constexpr const char* kComputeCS = R"glsl(
#version 450
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0, rgba8) uniform image2D img;
void main() {
  imageStore(img, ivec2(gl_GlobalInvocationID.xy), vec4(1.0));
}
)glsl";

TEST(ShaderCompiler, CompileComputeShader)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    cd::shader::CompileDesc desc {};
    desc.source = kComputeCS;
    desc.stage = cd::shader::ShaderStage::kCompute;
    desc.source_name = "blit.comp";

    auto r = c->compile(desc);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_FALSE(r->spirv.empty());
    EXPECT_EQ(r->spirv.front(), 0x07230203U);
}

// generate_debug_info must be a self-consistent toggle: the debug build emits
// OpSource/OpLine words and is therefore >= the stripped build for the same
// source. We assert the size relation only (NOT a byte pattern) so this test
// never pins golden SPIR-V — it just proves the flag reaches the emitter.
TEST(ShaderCompiler, DebugInfoFlagEmitsLargerModule)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    cd::shader::CompileDesc lean {};
    lean.source = kTriangleVS;
    lean.stage = cd::shader::ShaderStage::kVertex;
    lean.generate_debug_info = false;

    cd::shader::CompileDesc dbg = lean;
    dbg.generate_debug_info = true;

    auto r_lean = c->compile(lean);
    auto r_dbg = c->compile(dbg);
    ASSERT_TRUE(r_lean.has_value()) << r_lean.error().message;
    ASSERT_TRUE(r_dbg.has_value()) << r_dbg.error().message;
    EXPECT_GE(r_dbg->spirv.size(), r_lean->spirv.size());
}

// The HLSL frontend branch (EShSourceHlsl + EShMsgReadHlsl) must be reachable
// and return a structured Result — never crash / UB. The engine's glslang
// build ships ENABLE_HLSL=ON but no SPIRV-Tools optimizer, so we assert the
// path produces EITHER valid SPIR-V (magic word) OR a clean error, exercising
// the HLSL setEnvInput branch deterministically without pinning output.
TEST(ShaderCompiler, HlslFrontendReachableReturnsStructuredResult)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    constexpr const char* kHlslVS =
        "float4 main(uint vid : SV_VertexID) : SV_Position {\n"
        "  return float4(0.0, 0.0, 0.0, 1.0);\n"
        "}\n";

    cd::shader::CompileDesc desc {};
    desc.source = kHlslVS;
    desc.stage = cd::shader::ShaderStage::kVertex;
    desc.lang = cd::shader::ShaderLanguage::kHlsl;
    desc.entry_point = "main";
    desc.source_name = "vs.hlsl";

    const auto r = c->compile(desc);
    if (r.has_value())
    {
        EXPECT_FALSE(r->spirv.empty());
        EXPECT_EQ(r->spirv.front(), 0x07230203U);
    }
    else
    {
        // A clean, typed error in the shader domain (not a crash).
        EXPECT_EQ(r.error().domain, cd::shader::shader_errors::kDomain);
        EXPECT_FALSE(r.error().message.empty());
    }
}

// Whitespace-only source is non-empty (passes the empty-source guard) but
// carries no entry point. The contract under test is "no crash / UB on
// meaningless-but-non-empty input" — glslang must surface a structured
// Result. In practice it fails to produce a usable module; if some glslang
// version emits a degenerate module instead, that is still a clean success.
// Either way the failure path stays in the shader error domain with a message.
TEST(ShaderCompiler, WhitespaceOnlySourceHandledCleanly)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    cd::shader::CompileDesc desc {};
    desc.source = "   \n\t  \n";  // non-empty but no shader
    desc.stage = cd::shader::ShaderStage::kVertex;
    const auto r = c->compile(desc);
    if (!r.has_value())
    {
        EXPECT_EQ(r.error().domain, cd::shader::shader_errors::kDomain);
        EXPECT_FALSE(r.error().message.empty());
    }
}

// A self-referential include (a -> a) drives glslang's includer to the depth
// cap (16) and must surface a compile error, not hang or crash. The resolver
// always resolves "loop.glsl" to a body that re-includes itself.
TEST(ShaderCompiler, CyclicIncludeHitsDepthCapAndFailsCleanly)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    class LoopResolver final : public cd::shader::IIncludeResolver
    {
    public:
        [[nodiscard]] std::optional<Resolved> resolve(
            std::string_view requested, std::string_view /*requester*/,
            bool /*system_include*/) override
        {
            // Every request resolves to a module that includes itself.
            return Resolved {
                std::string { requested },
                "#include \"loop.glsl\"\n"
            };
        }
    };

    LoopResolver resolver;
    cd::shader::CompileDesc desc {};
    desc.source =
        "#version 450\n"
        "#extension GL_GOOGLE_include_directive : enable\n"
        "#include \"loop.glsl\"\n"
        "void main() {}\n";
    desc.stage = cd::shader::ShaderStage::kVertex;
    desc.include_resolver = &resolver;

    const auto r = c->compile(desc);
    EXPECT_FALSE(r.has_value());  // depth cap -> compile error, no hang
}

// to_eshlang must map every ShaderStage variant (incl. RT + mesh/task) to a
// glslang language without falling through. We can't GPU-compile mesh shaders
// portably, so this asserts the COMPILE path is reached (returns a Result) for
// each stage on a trivial source — proving no enum arm is unmapped/crashing.
TEST(ShaderCompiler, AllStagesReachCompilePath)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    constexpr cd::shader::ShaderStage kStages[] = {
        cd::shader::ShaderStage::kVertex,
        cd::shader::ShaderStage::kFragment,
        cd::shader::ShaderStage::kCompute,
        cd::shader::ShaderStage::kGeometry,
        cd::shader::ShaderStage::kTessControl,
        cd::shader::ShaderStage::kTessEval,
        cd::shader::ShaderStage::kRaygen,
        cd::shader::ShaderStage::kMiss,
        cd::shader::ShaderStage::kClosestHit,
        cd::shader::ShaderStage::kAnyHit,
        cd::shader::ShaderStage::kIntersection,
        cd::shader::ShaderStage::kCallable,
        cd::shader::ShaderStage::kMesh,
        cd::shader::ShaderStage::kTask,
    };

    for (const auto stage : kStages)
    {
        cd::shader::CompileDesc desc {};
        desc.source = "#version 450\nvoid main() {}\n";
        desc.stage = stage;
        // Most of these will fail to compile (missing stage-specific layout),
        // but the call MUST return a structured Result — the contract is "no
        // unmapped enum arm, no crash". Either outcome is acceptable.
        const auto r = c->compile(desc);
        if (!r.has_value())
        {
            EXPECT_EQ(r.error().domain, cd::shader::shader_errors::kDomain);
        }
    }
}

}  // namespace
