// =============================================================================
// CHROMODYNAMIC — cd_test_spirv_cross_glue
// B-infra1 — gtest for cd::spirv_cross_glue::translate()
//
// Test plan (16 tests — ADD-ONLY, no emit change, golden-safe):
//   1.  TranslateToHlsl              — frag SPIR-V → HLSL; "cbuffer" marker
//   2.  TranslateToMsl               — frag SPIR-V → MSL; "metal_stdlib" marker
//   3.  RoundTripGlsl                — SPIR-V → GLSL → SPIR-V recompile check
//   4.  EmptyInput                   — empty span → error, ok()==false
//   5.  MalformedSpirvSurfacesError  — garbage words → exception→Result (all 3 targets)
//   6.  TranslateHlslExplicitShaderModel — SM 5.1 explicit version → cbuffer
//   7.  TranslateGlslExplicitVersionEmitsDirective — version=330 → #version 330
//   8.  TranslateMslConfigReportsEntryPoint — cfg arg-buf=true → cleansed entry_point
//   9.  TranslateMslReflectsComputeWorkgroupSize — compute 8×4×2 via cfg overload
//  10.  TranslateMslFragmentWorkgroupIsUnit — frag 1×1×1 default via flat Target::kMsl
//  11.  TranslateMslCfgEmptyInputReturnsError — cfg overload empty guard
//  12.  TranslateMslCfgMalformedSpirvSurfacesError — cfg overload exception→Result
//  13.  TranslateMslCfgFlatBindingSucceeds — argument_buffers=false flat path
//  14.  TranslateMslFlatPathComputeWorkgroupSize — compute 8×4×2 via flat Target::kMsl
//  15.  TranslateHlslPushConstantRemappedToSpace1 — push_constant→b0/space1 remap
//  16.  TranslateGlslDefaultVersionEmits450 — version=0 auto-pick → #version 450
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

// ---------------------------------------------------------------------------
// Test 11 — translate_msl(span, cfg) empty-input guard
// ---------------------------------------------------------------------------
// The public translate_msl(span, cfg) overload has its own early-exit for an
// empty span (Translate.cpp line ~386-388) — distinct from translate()'s guard.
// Verifies that the cfg overload also surfaces an error (not a crash) on empty
// input.
TEST(SPIRV_CROSS_GLUE, TranslateMslCfgEmptyInputReturnsError)
{
    const std::span<const std::uint32_t> empty_span {};
    cd::spirv_cross_glue::MslBindingConfig cfg {};

    const auto result = cd::spirv_cross_glue::translate_msl(empty_span, cfg);

    EXPECT_FALSE(result.ok())          << "Expected failure on empty input (cfg overload)";
    EXPECT_FALSE(result.error.empty()) << "Expected non-empty error string";
    EXPECT_TRUE(result.source.empty()) << "Expected empty source on failure";
}

// ---------------------------------------------------------------------------
// Test 12 — translate_msl(span, cfg) malformed SPIR-V → exception → Result
// ---------------------------------------------------------------------------
// Exercises the exception→TranslateResult conversion in the cfg overload
// (translate_msl_impl's catch block). Garbage words cause CompilerMSL to throw
// CompilerError; the API must catch and surface it as a populated error string.
TEST(SPIRV_CROSS_GLUE, TranslateMslCfgMalformedSpirvSurfacesError)
{
    const std::array<std::uint32_t, 4> garbage { 0xDEADBEEFu, 0x00000001u, 0u, 0u };
    const std::span<const std::uint32_t> bad { garbage };
    cd::spirv_cross_glue::MslBindingConfig cfg {};

    const auto result = cd::spirv_cross_glue::translate_msl(bad, cfg);

    EXPECT_FALSE(result.ok())          << "Malformed SPIR-V must fail (cfg overload)";
    EXPECT_FALSE(result.error.empty()) << "Error string must be populated";
    EXPECT_TRUE(result.source.empty()) << "No source on failure";
}

// ---------------------------------------------------------------------------
// Test 13 — translate_msl(cfg) with argument_buffers=false (flat-binding)
// ---------------------------------------------------------------------------
// The cfg overload's non-default flat-binding path (argument_buffers=false) is
// a distinct code path in translate_msl_impl; the default is true (Tests 8/9
// cover the true path). Verifies that disabling argument buffers still produces
// valid MSL with the metal_stdlib include marker.
TEST(SPIRV_CROSS_GLUE, TranslateMslCfgFlatBindingSucceeds)
{
    auto spirv = compile_to_spirv();
    if (spirv.empty())
    {
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    }

    cd::spirv_cross_glue::MslBindingConfig cfg {};
    cfg.argument_buffers = false;  // classic flat-binding; non-default path

    const auto result = cd::spirv_cross_glue::translate_msl(
        std::span<const std::uint32_t>{ spirv }, cfg);

    ASSERT_TRUE(result.ok()) << "MSL flat-binding translate error: " << result.error;
    EXPECT_NE(result.source.find("#include <metal_stdlib>"), std::string::npos)
        << "Expected '#include <metal_stdlib>' in flat-binding MSL output";
    EXPECT_FALSE(result.entry_point.empty())
        << "translate_msl(cfg) must report the cleansed entry point even with flat binding";
}

// ---------------------------------------------------------------------------
// Test 14 — Compute shader via flat-binding Target::kMsl workgroup reflection
// ---------------------------------------------------------------------------
// The flat-binding translate_msl() free function (Translate.cpp lines 318-350)
// also performs workgroup reflection on the compile() output. Tests 9/10 hit
// the translate_msl_impl path (cfg overload); this test exercises the parallel
// workgroup-reflection code inside the flat-binding free function directly.
TEST(SPIRV_CROSS_GLUE, TranslateMslFlatPathComputeWorkgroupSize)
{
    auto spirv = compile_stage(kComputeSrc, cd::shader::ShaderStage::kCompute,
                               "test_compute_flat.glsl");
    if (spirv.empty())
    {
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    }

    // Use Target::kMsl (routes through the flat-binding free function, not cfg).
    const auto result = cd::spirv_cross_glue::translate(
        std::span<const std::uint32_t>{ spirv },
        cd::spirv_cross_glue::Target::kMsl);

    ASSERT_TRUE(result.ok()) << "MSL flat-path compute translate error: " << result.error;
    // The compute shader declares layout(local_size_x=8, local_size_y=4, local_size_z=2).
    EXPECT_EQ(result.workgroup.x, 8u) << "workgroup.x mismatch on flat-binding path";
    EXPECT_EQ(result.workgroup.y, 4u) << "workgroup.y mismatch on flat-binding path";
    EXPECT_EQ(result.workgroup.z, 2u) << "workgroup.z mismatch on flat-binding path";
}

// ---------------------------------------------------------------------------
// Test 15 — HLSL push_constant remap: SM>=51 → b0/space1
// ---------------------------------------------------------------------------
// Exercises the push_constant remap branch in translate_hlsl (Translate.cpp
// lines 143-165). This branch fires when SM>=51 AND the module has a
// push_constant block. A vertex shader with layout(push_constant) compiled to
// SPIR-V is used. SPIRV-Cross must emit the push_constant cbuffer pinned to
// b0/space1 (not the default b0/space0 collision). The emitted HLSL should
// contain "space1" or "_11" to confirm the remap ran.
TEST(SPIRV_CROSS_GLUE, TranslateHlslPushConstantRemappedToSpace1)
{
    // Fragment shader with a push_constant block. Compiled under Vulkan semantics
    // (kVulkan13 default), push_constant is a valid layout qualifier.
    constexpr const char* kPushConstSrc = R"glsl(
#version 450
layout(push_constant) uniform PushData {
    vec4 offset;
    float scale;
} u_push;
layout(location = 0) out vec4 out_color;
void main() {
    out_color = u_push.offset * u_push.scale;
}
)glsl";

    auto spirv = compile_stage(kPushConstSrc, cd::shader::ShaderStage::kFragment,
                               "test_push_const.glsl");
    if (spirv.empty())
    {
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    }

    // SM 51 triggers the push_constant remap path (sm >= 51U condition).
    const auto result = cd::spirv_cross_glue::translate(
        std::span<const std::uint32_t>{ spirv },
        cd::spirv_cross_glue::Target::kHlsl,
        51u);

    ASSERT_TRUE(result.ok()) << "HLSL push_constant translate error: " << result.error;
    EXPECT_FALSE(result.source.empty());
    // SPIRV-Cross emits push_constant as "cbuffer ... : register(b0, space1)"
    // after the remap. "space1" is the canonical marker.
    EXPECT_NE(result.source.find("space1"), std::string::npos)
        << "Expected 'space1' in HLSL output after push_constant remap.\n"
        << "Actual output:\n" << result.source;
}

// ---------------------------------------------------------------------------
// Test 16 — GLSL version 0 auto-pick emits #version 450
// ---------------------------------------------------------------------------
// translate(..., kGlsl, 0) takes the auto-pick branch (version == 0U) in
// translate_glsl, setting opts.version = kDefaultGlslVersion = 450. The
// emitted GLSL must contain "#version 450" (Test 7 covers explicit 330 but
// not the default-version branch).
TEST(SPIRV_CROSS_GLUE, TranslateGlslDefaultVersionEmits450)
{
    auto spirv = compile_to_spirv();
    if (spirv.empty())
    {
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    }

    // version=0 → auto-pick → #version 450
    const auto result = cd::spirv_cross_glue::translate(
        std::span<const std::uint32_t>{ spirv },
        cd::spirv_cross_glue::Target::kGlsl,
        0u);

    ASSERT_TRUE(result.ok()) << "GLSL default-version translate error: " << result.error;
    EXPECT_NE(result.source.find("#version 450"), std::string::npos)
        << "Expected '#version 450' from auto-pick path.\nActual:\n" << result.source;
}

}  // namespace
