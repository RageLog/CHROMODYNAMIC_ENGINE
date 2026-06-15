// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_metal_shader_toolchain.cpp
// phase M3 (Metal backend): GLSL -> SPIR-V -> MSL host-side toolchain tests.
//
// PURE HOST-SIDE — no MTLDevice, no Metal framework, no GPU. The toolchain
// emits MSL *text*, so these tests run on Windows (and every CI lane that has
// glslang). They assert the chain composes, the binding model lands the
// expected [[buffer(n)]]/[[texture(n)]]/argument-buffer attributes, and a
// compile failure returns a typed, stage-prefixed error.
//
// Pattern: Arrange / Act / Assert. Deterministic CPU transforms only — no
// sleep_for, no filesystem, no network.
// =============================================================================
#include <cd/rhi/metal/MetalShaderToolchain.hpp>
#include <cd/gluon/ModuleRegistry.hpp>
#include <cd/shader/Compiler.hpp>
#include <cd/spirv_cross_glue/Translate.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace
{

// A representative engine fragment shader: a cd/gluon #include, a set-0
// combined-image-sampler texture, and a push_constant block. Exercises every
// limb of the binding model in one source.
constexpr const char* kEngineFS = R"glsl(
#version 450
#extension GL_GOOGLE_include_directive : enable
#include <cd/gluon/tonemap.glsl>
layout(set = 0, binding = 0) uniform sampler2D cd_albedo_tex;
layout(push_constant) uniform PC {
    vec4 tint;
} pc;
layout(location = 0) in  vec2 in_uv;
layout(location = 0) out vec4 o;
void main()
{
    vec3 c = texture(cd_albedo_tex, in_uv).rgb * pc.tint.rgb;
    o = vec4(cd_tonemap_reinhard(c), 1.0);
}
)glsl";

constexpr const char* kMinimalVS = R"glsl(
#version 450
layout(location = 0) in vec3 in_pos;
void main() { gl_Position = vec4(in_pos, 1.0); }
)glsl";

[[nodiscard]] std::unique_ptr<cd::shader::ICompiler> make_compiler_or_null()
{
    return cd::shader::make_glslang_compiler();
}

// Extract every N from the literal attribute "[[buffer(N)]]" in an MSL source.
// Used by the buffer-index disjointness test to prove no emitted [[buffer]]
// lands in the reserved vertex-input range.
[[nodiscard]] std::vector<std::uint32_t> collect_buffer_indices(const std::string& msl)
{
    std::vector<std::uint32_t> out;
    constexpr std::string_view kOpen = "[[buffer(";
    std::size_t pos = 0;
    while ((pos = msl.find(kOpen, pos)) != std::string::npos)
    {
        const std::size_t num_start = pos + kOpen.size();
        std::size_t cur = num_start;
        std::uint32_t value = 0;
        bool has_digit = false;
        while (cur < msl.size() && msl[cur] >= '0' && msl[cur] <= '9')
        {
            value = value * 10U + static_cast<std::uint32_t>(msl[cur] - '0');
            has_digit = true;
            ++cur;
        }
        if (has_digit)
        {
            out.push_back(value);
        }
        pos = num_start;
    }
    return out;
}

// ---- Argument validation (no glslang needed) --------------------------------

TEST(MetalShaderToolchain, EmptyGlslRejected)
{
    auto c = make_compiler_or_null();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    cd::rhi::metal::GlslToMslDesc d {};  // glsl_source empty by default
    const auto r = cd::rhi::metal::compose_glsl_to_msl(*c, d);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(
                  cd::shader::shader_errors::Code::kInvalidArgument));
}

TEST(MetalShaderToolchain, EmptySpirvRejected)
{
    cd::rhi::metal::GlslToMslDesc d {};
    const std::span<const std::uint32_t> empty {};
    const auto r = cd::rhi::metal::compose_spirv_to_msl(empty, d);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(
                  cd::shader::shader_errors::Code::kInvalidArgument));
}

// Stage-prefixed error chaining: a GLSL syntax error must surface with the
// "glslang: " prefix (mirrors the D3D12 toolchain invariant).
TEST(MetalShaderToolchain, GlslErrorIsStagePrefixed)
{
    auto c = make_compiler_or_null();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    cd::rhi::metal::GlslToMslDesc d {};
    d.glsl_source = "#version 450\nvoid main() { bogus_fn(); }\n";
    d.stage = cd::rhi::ShaderStage::kFragment;
    const auto r = cd::rhi::metal::compose_glsl_to_msl(*c, d);
    ASSERT_FALSE(r.has_value());
    const std::string msg { r.error().message };
    EXPECT_EQ(msg.rfind("glslang: ", 0), 0u) << msg;
}

// ---- End-to-end chain (needs glslang; pure CPU, runs on Windows) ------------

TEST(MetalShaderToolchain, VertexChainProducesMsl)
{
    auto c = make_compiler_or_null();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    cd::rhi::metal::GlslToMslDesc d {};
    d.glsl_source = kMinimalVS;
    d.stage = cd::rhi::ShaderStage::kVertex;
    d.source_name = "toolchain_vs";
    const auto r = cd::rhi::metal::compose_glsl_to_msl(*c, d);
    ASSERT_TRUE(r.has_value()) << std::string(r.error().message);
    EXPECT_FALSE(r->source.empty());
    // Every SPIRV-Cross MSL output opens with the Metal standard library.
    EXPECT_NE(r->source.find("#include <metal_stdlib>"), std::string::npos)
        << r->source.substr(0, 200);
    // The entry point is reported (SPIRV-Cross may rename "main"); a vertex
    // shader keeps a vertex-qualified function.
    EXPECT_FALSE(r->entry_point.empty());
    EXPECT_NE(r->source.find("vertex "), std::string::npos) << r->source;
}

TEST(MetalShaderToolchain, FragmentChainResolvesGluonIncludeAndBindings)
{
    auto c = make_compiler_or_null();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    cd::rhi::metal::GlslToMslDesc d {};
    d.glsl_source = kEngineFS;
    d.stage = cd::rhi::ShaderStage::kFragment;
    d.source_name = "engine_fs";
    // include_resolver left null -> embedded gluon catalogue bridge.

    const auto r = cd::rhi::metal::compose_glsl_to_msl(*c, d);
    // A failure here would PROVE the gluon include didn't resolve (glslang
    // errors out) -- so a pass proves resolution flows through the toolchain.
    ASSERT_TRUE(r.has_value()) << std::string(r.error().message);
    const std::string& msl = r->source;
    ASSERT_FALSE(msl.empty());

    // (1) Valid MSL: standard-library include + a fragment entry.
    EXPECT_NE(msl.find("#include <metal_stdlib>"), std::string::npos) << msl;
    EXPECT_NE(msl.find("fragment "), std::string::npos) << msl;
    EXPECT_FALSE(r->entry_point.empty());
    EXPECT_NE(msl.find(r->entry_point), std::string::npos)
        << "reported entry point '" << r->entry_point << "' not found in MSL";

    // (2) Binding model -- SET-PER-ARGUMENT-BUFFER (argument_buffers ON, the
    // default). Descriptor set 0 is emitted as ONE argument-buffer struct bound
    // at [[buffer(0)]] (set N -> [[buffer(N)]], the Metal analog of D3D12
    // space-per-set). Resources INSIDE the set carry [[id(binding)]] within the
    // struct: the combined sampler2D splits into a texture at [[id(0)]] and its
    // sampler at [[id(1)]]. The push_constant is remapped OUT of the set range
    // to the dedicated slot [[buffer(kPushConstantBufferIndex)]].
    EXPECT_NE(msl.find("spvDescriptorSetBuffer0"), std::string::npos)
        << "set 0 must form an argument-buffer struct:\n" << msl;
    EXPECT_NE(msl.find("[[buffer(0)]]"), std::string::npos)
        << "set-0 argument buffer must bind at [[buffer(0)]]:\n" << msl;
    EXPECT_NE(msl.find("[[id(0)]]"), std::string::npos)
        << "set-0 texture (binding 0) must carry [[id(0)]] inside the set:\n" << msl;
    EXPECT_NE(msl.find("[[id(1)]]"), std::string::npos)
        << "set-0 sampler must carry [[id(1)]] inside the set:\n" << msl;
    const std::string pc_attr =
        "[[buffer(" + std::to_string(cd::rhi::metal::kPushConstantBufferIndex) + ")]]";
    EXPECT_NE(msl.find(pc_attr), std::string::npos)
        << "push_constant must be remapped to " << pc_attr << ":\n" << msl;
}

// phase1122 namespace-disjointness CONTRACT TEST. The four [[buffer(N)]]
// resource classes the Metal backend shares one per-stage namespace across MUST
// be disjoint: argument-buffer sets [0..7], push_constant [8], vertex-input
// [9..15], SPIRV-Cross aux [20..30]. SPIRV-Cross emits the set + push buffers;
// the .mm side binds vertex streams in [9..15] by construction (shared constant
// kVertexBufferBaseIndex). This test PROVES the emitted MSL never places any
// SPIRV-Cross-owned [[buffer]] (set argument buffer, push block, or aux buffer)
// in the reserved vertex range, so a host-side vertex bind can never alias one.
TEST(MetalShaderToolchain, EmittedBufferIndicesAvoidVertexRange)
{
    auto c = make_compiler_or_null();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    cd::rhi::metal::GlslToMslDesc d {};
    d.glsl_source = kEngineFS;
    d.stage = cd::rhi::ShaderStage::kFragment;
    d.source_name = "engine_fs_ranges";
    // Default binding: argument_buffers ON, push at kPushConstantBufferIndex (8).

    const auto r = cd::rhi::metal::compose_glsl_to_msl(*c, d);
    ASSERT_TRUE(r.has_value()) << std::string(r.error().message);
    const std::string& msl = r->source;

    // The canonical map (MetalShaderToolchain.hpp shared constants).
    constexpr std::uint32_t kSet0    = 0U;
    const std::uint32_t     kPush    = cd::rhi::metal::kPushConstantBufferIndex;  // 8
    const std::uint32_t     kVtxLo   = cd::rhi::metal::kVertexBufferBaseIndex;    // 9
    const std::uint32_t     kVtxHi   =
        cd::rhi::metal::kVertexBufferBaseIndex + cd::rhi::metal::kMaxVertexBufferSlots - 1U;  // 15
    const std::uint32_t     kAuxLo   = cd::rhi::metal::kSpirvCrossAuxBaseIndex;   // 20

    // Sanity on the contract itself: the four classes are ordered + disjoint.
    static_assert(cd::rhi::metal::kPushConstantBufferIndex
                  >= cd::rhi::metal::kMaxVertexBufferSlots,
                  "push slot must sit above the set range");
    EXPECT_LT(kSet0, kPush);
    EXPECT_LT(kPush, kVtxLo);
    EXPECT_LT(kVtxHi, kAuxLo) << "vertex range must be strictly below the aux floor";

    // (1) The set-0 argument buffer lands at [[buffer(0)]]; the push block lands
    //     at [[buffer(kPush)]]. Both must be present (proves the map is live).
    EXPECT_NE(msl.find("[[buffer(0)]]"), std::string::npos)
        << "set-0 argument buffer must bind at [[buffer(0)]]:\n" << msl;
    const std::string push_attr = "[[buffer(" + std::to_string(kPush) + ")]]";
    EXPECT_NE(msl.find(push_attr), std::string::npos)
        << "push_constant must bind at " << push_attr << ":\n" << msl;

    // (2) THE CONTRACT: NO emitted [[buffer(N)]] may fall in the vertex range
    //     [kVtxLo .. kVtxHi]. SPIRV-Cross owns set + push + aux buffers; if any
    //     of those landed in the vertex range it would alias a host vertex bind.
    const std::vector<std::uint32_t> indices = collect_buffer_indices(msl);
    ASSERT_FALSE(indices.empty()) << "expected at least the set + push buffers:\n" << msl;
    for (const std::uint32_t idx : indices)
    {
        EXPECT_FALSE(idx >= kVtxLo && idx <= kVtxHi)
            << "emitted [[buffer(" << idx << ")]] collides with the reserved "
            << "vertex range [" << kVtxLo << ".." << kVtxHi << "]:\n" << msl;
        // Every emitted index must also stay within Metal's per-stage cap.
        EXPECT_LE(idx, 30U)
            << "emitted [[buffer(" << idx << ")]] exceeds the Metal 31-slot cap:\n" << msl;
    }
}

// The push slot is configurable: changing it moves the [[buffer(n)]] attribute.
TEST(MetalShaderToolchain, PushConstantBufferIndexIsConfigurable)
{
    auto c = make_compiler_or_null();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    cd::rhi::metal::GlslToMslDesc d {};
    d.glsl_source = kEngineFS;
    d.stage = cd::rhi::ShaderStage::kFragment;
    d.source_name = "engine_fs_cfg";
    d.binding.push_constant_buffer_index = 20U;

    const auto r = cd::rhi::metal::compose_glsl_to_msl(*c, d);
    ASSERT_TRUE(r.has_value()) << std::string(r.error().message);
    EXPECT_NE(r->source.find("[[buffer(20)]]"), std::string::npos) << r->source;
}

// compose_spirv_to_msl: the SPIR-V tail produces equivalent MSL from a pre-built
// module (the device's kSpirv entry path).
TEST(MetalShaderToolchain, SpirvTailProducesMsl)
{
    auto c = make_compiler_or_null();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    cd::shader::CompileDesc sd {};
    sd.source = kMinimalVS;
    sd.stage = cd::shader::ShaderStage::kVertex;
    sd.source_name = "spirv_tail_vs";
    const auto spirv = c->compile(sd);
    ASSERT_TRUE(spirv.has_value()) << std::string(spirv.error().message);

    cd::rhi::metal::GlslToMslDesc d {};
    d.stage = cd::rhi::ShaderStage::kVertex;
    const auto r = cd::rhi::metal::compose_spirv_to_msl(spirv->spirv, d);
    ASSERT_TRUE(r.has_value()) << std::string(r.error().message);
    EXPECT_NE(r->source.find("#include <metal_stdlib>"), std::string::npos);
    EXPECT_FALSE(r->entry_point.empty());
}

// The glue-level translate_msl honours argument_buffers=false (classic flat
// binding) -- the single-set/compute fallback path.
TEST(MetalShaderToolchain, FlatBindingFallback)
{
    auto c = make_compiler_or_null();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    cd::shader::CompileDesc sd {};
    sd.source = kEngineFS;
    sd.stage = cd::shader::ShaderStage::kFragment;
    sd.source_name = "flat_fs";
    cd::gluon::ModuleResolver resolver {};
    sd.include_resolver = &resolver;
    const auto spirv = c->compile(sd);
    ASSERT_TRUE(spirv.has_value()) << std::string(spirv.error().message);

    cd::spirv_cross_glue::MslBindingConfig cfg {};
    cfg.argument_buffers = false;
    const auto r = cd::spirv_cross_glue::translate_msl(spirv->spirv, cfg);
    ASSERT_TRUE(r.ok()) << r.error;
    EXPECT_FALSE(r.source.empty());
    // Flat path: the texture still carries a [[texture(n)]]; no argument-buffer
    // struct wrapper is required.
    EXPECT_NE(r.source.find("[[texture(0)]]"), std::string::npos) << r.source;
}

}  // namespace
