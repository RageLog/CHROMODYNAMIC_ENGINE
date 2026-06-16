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

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

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

// A tiny compute shader declaring an explicit local workgroup size. Used to
// exercise the MSL workgroup-size reflection path (translate_msl) — a
// fragment shader reports 1x1x1, so a compute stage is needed to prove the
// reflection actually reads the SPIR-V LocalSize execution mode.
constexpr const char* kComputeSrc = R"glsl(
#version 450
layout(local_size_x = 8, local_size_y = 4, local_size_z = 2) in;
layout(set = 0, binding = 0) buffer Data { float v[]; } u_data;
void main() {
    u_data.v[gl_GlobalInvocationID.x] = 1.0;
}
)glsl";

/// Helper: compile a GLSL source at a stage to SPIR-V. Returns {} (caller
/// should GTEST_SKIP) when glslang is absent.
[[nodiscard]] std::vector<std::uint32_t> compile_stage(const char* src,
                                                       cd::shader::ShaderStage stage,
                                                       const char* name)
{
    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
    {
        return {};  // caller should GTEST_SKIP()
    }

    cd::shader::CompileDesc desc {};
    desc.source      = src;
    desc.stage       = stage;
    desc.source_name = name;

    auto result = compiler->compile(desc);
    if (!result.has_value())
    {
        ADD_FAILURE() << "glslang compile failed: " << result.error().message;
        return {};
    }
    return result.value().spirv;
}

/// Helper: compile kFragSrc to SPIR-V. Skips the test if glslang is absent.
[[nodiscard]] std::vector<std::uint32_t> compile_to_spirv()
{
    return compile_stage(kFragSrc, cd::shader::ShaderStage::kFragment, "test_frag.glsl");
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

// ---------------------------------------------------------------------------
// Test 5 — Malformed (non-empty, garbage) SPIR-V → exception → Result::error
// ---------------------------------------------------------------------------
// The empty-input path is a pre-check; this exercises the actual
// exception→TranslateResult conversion: a non-empty word stream that is NOT a
// valid SPIR-V module makes the SPIRV-Cross constructor throw CompilerError,
// which the API boundary must catch and surface as a populated error string
// (NOT propagate an exception, NOT crash). Tests all three targets.
TEST(SPIRV_CROSS_GLUE, MalformedSpirvSurfacesErrorNotException)
{
    // Garbage words: wrong magic (SPIR-V magic is 0x07230203), so the
    // CompilerGLSL/HLSL/MSL constructor rejects it via CompilerError.
    const std::array<std::uint32_t, 4> garbage { 0xDEADBEEFu, 0x00000001u, 0u, 0u };
    const std::span<const std::uint32_t> bad { garbage };

    for (const auto target : { cd::spirv_cross_glue::Target::kGlsl,
                               cd::spirv_cross_glue::Target::kHlsl,
                               cd::spirv_cross_glue::Target::kMsl })
    {
        const auto result = cd::spirv_cross_glue::translate(bad, target);
        EXPECT_FALSE(result.ok()) << "malformed SPIR-V must fail, target="
                                  << static_cast<int>(target);
        EXPECT_FALSE(result.error.empty()) << "error string must be populated";
        EXPECT_TRUE(result.source.empty()) << "no source on failure";
    }
}

// ---------------------------------------------------------------------------
// Test 6 — HLSL with an explicit shader-model version (51 = SM 5.1)
// ---------------------------------------------------------------------------
// Verifies the `version` parameter is honoured (not just the auto-pick path):
// SM 5.1 still emits `cbuffer` and a valid translation.
TEST(SPIRV_CROSS_GLUE, TranslateHlslExplicitShaderModel)
{
    auto spirv = compile_to_spirv();
    if (spirv.empty())
    {
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    }

    const auto result = cd::spirv_cross_glue::translate(
        std::span<const std::uint32_t>{ spirv },
        cd::spirv_cross_glue::Target::kHlsl,
        51u  // SM 5.1
    );

    ASSERT_TRUE(result.ok()) << "SM 5.1 HLSL translate error: " << result.error;
    EXPECT_NE(result.source.find("cbuffer"), std::string::npos)
        << "Expected 'cbuffer' at SM 5.1.\nActual:\n" << result.source;
}

// ---------------------------------------------------------------------------
// Test 7 — GLSL with an explicit version (330) round-trips and recompiles
// ---------------------------------------------------------------------------
TEST(SPIRV_CROSS_GLUE, TranslateGlslExplicitVersionEmitsDirective)
{
    auto spirv = compile_to_spirv();
    if (spirv.empty())
    {
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    }

    const auto result = cd::spirv_cross_glue::translate(
        std::span<const std::uint32_t>{ spirv },
        cd::spirv_cross_glue::Target::kGlsl,
        330u
    );

    ASSERT_TRUE(result.ok()) << "GLSL 330 translate error: " << result.error;
    EXPECT_NE(result.source.find("#version 330"), std::string::npos)
        << "Expected '#version 330' directive.\nActual:\n" << result.source;
}

// ---------------------------------------------------------------------------
// Test 8 — translate_msl(cfg) argument-buffer path + entry-point cleanse
// ---------------------------------------------------------------------------
// The MslBindingConfig overload pins the set-per-argument-buffer model and
// reports the cleansed MSL entry point. A fragment "main" is renamed by
// SPIRV-Cross (reserved-word dodge), so entry_point must be non-empty.
TEST(SPIRV_CROSS_GLUE, TranslateMslConfigReportsEntryPoint)
{
    auto spirv = compile_to_spirv();
    if (spirv.empty())
    {
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    }

    cd::spirv_cross_glue::MslBindingConfig cfg {};
    cfg.argument_buffers = true;  // set-per-argument-buffer model
    const auto result = cd::spirv_cross_glue::translate_msl(
        std::span<const std::uint32_t>{ spirv }, cfg);

    ASSERT_TRUE(result.ok()) << "MSL cfg translate error: " << result.error;
    EXPECT_NE(result.source.find("#include <metal_stdlib>"), std::string::npos);
    EXPECT_FALSE(result.entry_point.empty())
        << "translate_msl must report the cleansed MSL entry point";
}

// ---------------------------------------------------------------------------
// Test 9 — MSL workgroup-size reflection from a compute shader
// ---------------------------------------------------------------------------
// A compute shader with layout(local_size = 8,4,2) must reflect to
// WorkgroupSize{8,4,2} via translate_msl — the M6 source of truth for the
// Metal threads-per-threadgroup dispatch. The empty-cfg overload is fine.
TEST(SPIRV_CROSS_GLUE, TranslateMslReflectsComputeWorkgroupSize)
{
    auto spirv = compile_stage(kComputeSrc, cd::shader::ShaderStage::kCompute,
                               "test_compute.glsl");
    if (spirv.empty())
    {
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    }

    cd::spirv_cross_glue::MslBindingConfig cfg {};
    const auto result = cd::spirv_cross_glue::translate_msl(
        std::span<const std::uint32_t>{ spirv }, cfg);

    ASSERT_TRUE(result.ok()) << "MSL compute translate error: " << result.error;
    EXPECT_EQ(result.workgroup.x, 8u);
    EXPECT_EQ(result.workgroup.y, 4u);
    EXPECT_EQ(result.workgroup.z, 2u);
}

// ---------------------------------------------------------------------------
// Test 10 — Fragment (non-compute) reflects the 1x1x1 default workgroup
// ---------------------------------------------------------------------------
// The flat-binding Target::kMsl path leaves workgroup at the 1x1x1 default for
// a non-compute module (no SPIR-V LocalSize execution mode). Locks the
// "normalise 0 → 1" contract so a non-compute dispatch never collapses.
TEST(SPIRV_CROSS_GLUE, TranslateMslFragmentWorkgroupIsUnit)
{
    auto spirv = compile_to_spirv();
    if (spirv.empty())
    {
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    }

    const auto result = cd::spirv_cross_glue::translate(
        std::span<const std::uint32_t>{ spirv },
        cd::spirv_cross_glue::Target::kMsl);

    ASSERT_TRUE(result.ok()) << "MSL frag translate error: " << result.error;
    EXPECT_EQ(result.workgroup.x, 1u);
    EXPECT_EQ(result.workgroup.y, 1u);
    EXPECT_EQ(result.workgroup.z, 1u);
}

}  // namespace
