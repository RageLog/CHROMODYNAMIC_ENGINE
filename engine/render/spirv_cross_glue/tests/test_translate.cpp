// =============================================================================
// CHROMODYNAMIC — cd_test_spirv_cross_glue
// B-infra1 — gtest for cd::spirv_cross_glue::translate()
//
// Test plan:
//   1. SPIRV_CROSS_GLUE.TranslateToHlsl
//      Compile a minimal fragment shader GLSL → SPIR-V (via cd::shader),
//      translate SPIR-V → HLSL, verify non-empty output containing "cbuffer"
//      (canonical HLSL uniform-block keyword introduced even for SM 5.1+).
//
//   2. SPIRV_CROSS_GLUE.TranslateToMsl
//      Same SPIR-V → MSL, verify "#include <metal_stdlib>" marker.
//
//   3. SPIRV_CROSS_GLUE.RoundTripGlsl
//      SPIR-V → GLSL (round-trip), verify non-empty compilable GLSL by
//      re-compiling the emitted source with cd::shader and checking that
//      the second compile also succeeds without error.
//
//   4. SPIRV_CROSS_GLUE.EmptyInput
//      Negative: empty span → error string non-empty, ok() == false.
//
// Anti-flakiness: all operations are deterministic CPU transforms; no
// sleep_for, no filesystem access, no network I/O.
// =============================================================================
#include <cd/spirv_cross_glue/Translate.hpp>
#include <cd/shader/Compiler.hpp>

#include <gtest/gtest.h>

#include <span>
#include <string_view>

namespace
{

// A tiny fragment shader with a uniform block. Having a UBO causes SPIRV-Cross
// to emit `cbuffer` in HLSL (the canonical marker checked by TranslateToHlsl).
constexpr const char* kFragSrc = R"glsl(
#version 450
layout(set = 0, binding = 0) uniform Params {
    vec4 color;
} u_params;
layout(location = 0) out vec4 out_color;
void main() {
    out_color = u_params.color;
}
)glsl";

/// Helper: compile kFragSrc to SPIR-V. Skips the test if glslang is absent.
[[nodiscard]] std::vector<std::uint32_t> compile_to_spirv()
{
    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
    {
        return {};  // caller should GTEST_SKIP()
    }

    cd::shader::CompileDesc desc {};
    desc.source      = kFragSrc;
    desc.stage       = cd::shader::ShaderStage::kFragment;
    desc.source_name = "test_frag.glsl";

    auto result = compiler->compile(desc);
    if (!result.has_value())
    {
        ADD_FAILURE() << "glslang compile failed: " << result.error().message;
        return {};
    }
    return result.value().spirv;
}

// ---------------------------------------------------------------------------
// Test 1 — GLSL → SPIR-V → HLSL
// ---------------------------------------------------------------------------
TEST(SPIRV_CROSS_GLUE, TranslateToHlsl)
{
    auto spirv = compile_to_spirv();
    if (spirv.empty())
    {
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG; skipping HLSL translate test";
    }

    const auto result = cd::spirv_cross_glue::translate(
        std::span<const std::uint32_t>{ spirv },
        cd::spirv_cross_glue::Target::kHlsl
    );

    ASSERT_TRUE(result.ok())   << "HLSL translate error: " << result.error;
    EXPECT_FALSE(result.source.empty()) << "HLSL output must be non-empty";

    // cbuffer is the HLSL uniform-block keyword; its presence confirms a real
    // HLSL cross-compilation happened (not a trivial passthrough).
    EXPECT_NE(result.source.find("cbuffer"), std::string::npos)
        << "Expected 'cbuffer' keyword in HLSL output.\nActual output:\n" << result.source;
}

// ---------------------------------------------------------------------------
// Test 2 — GLSL → SPIR-V → MSL
// ---------------------------------------------------------------------------
TEST(SPIRV_CROSS_GLUE, TranslateToMsl)
{
    auto spirv = compile_to_spirv();
    if (spirv.empty())
    {
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG; skipping MSL translate test";
    }

    const auto result = cd::spirv_cross_glue::translate(
        std::span<const std::uint32_t>{ spirv },
        cd::spirv_cross_glue::Target::kMsl
    );

    ASSERT_TRUE(result.ok())   << "MSL translate error: " << result.error;
    EXPECT_FALSE(result.source.empty()) << "MSL output must be non-empty";

    // Every SPIRV-Cross MSL output begins with the Metal standard library include.
    EXPECT_NE(result.source.find("#include <metal_stdlib>"), std::string::npos)
        << "Expected '#include <metal_stdlib>' in MSL output.\nActual output:\n" << result.source;
}

// ---------------------------------------------------------------------------
// Test 3 — Round-trip: GLSL → SPIR-V → GLSL → SPIR-V (recompile check)
// ---------------------------------------------------------------------------
TEST(SPIRV_CROSS_GLUE, RoundTripGlsl)
{
    auto spirv = compile_to_spirv();
    if (spirv.empty())
    {
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG; skipping round-trip test";
    }

    // Translate SPIR-V → GLSL
    const auto glsl_result = cd::spirv_cross_glue::translate(
        std::span<const std::uint32_t>{ spirv },
        cd::spirv_cross_glue::Target::kGlsl
    );

    ASSERT_TRUE(glsl_result.ok())   << "GLSL translate error: " << glsl_result.error;
    ASSERT_FALSE(glsl_result.source.empty()) << "Round-trip GLSL must be non-empty";

    // Re-compile the emitted GLSL to confirm it is syntactically valid.
    auto compiler = cd::shader::make_glslang_compiler();
    ASSERT_NE(compiler, nullptr);  // already guarded above but be explicit

    cd::shader::CompileDesc desc2 {};
    desc2.source      = glsl_result.source;
    desc2.stage       = cd::shader::ShaderStage::kFragment;
    desc2.source_name = "round_trip.glsl";

    const auto recompile = compiler->compile(desc2);
    EXPECT_TRUE(recompile.has_value())
        << "Round-trip GLSL failed to recompile: "
        << (recompile.has_value() ? "" : std::string(recompile.error().message))
        << "\nEmitted GLSL source:\n" << glsl_result.source;
}

// ---------------------------------------------------------------------------
// Test 4 — Negative: empty SPIR-V input
// ---------------------------------------------------------------------------
TEST(SPIRV_CROSS_GLUE, EmptyInput)
{
    const std::span<const std::uint32_t> empty_span {};
    const auto result = cd::spirv_cross_glue::translate(
        empty_span,
        cd::spirv_cross_glue::Target::kHlsl
    );

    EXPECT_FALSE(result.ok())          << "Expected failure on empty input";
    EXPECT_FALSE(result.error.empty()) << "Expected non-empty error string on empty input";
    EXPECT_TRUE(result.source.empty()) << "Expected empty source on failure";
}

}  // namespace
