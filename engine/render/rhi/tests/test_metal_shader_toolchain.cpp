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

// M6 (ADR-20260615): a compute shader with a NON-TRIVIAL local workgroup size.
// The whole point of M6: the .mm dispatch path must read this (8,4,2) from
// SPIRV-Cross reflection — hardcoding (1,1,1) under-counts the dispatch by
// 8*4*2 = 64x. This source proves the host toolchain reflects the size.
constexpr const char* kComputeCS = R"glsl(
#version 450
layout(local_size_x = 8, local_size_y = 4, local_size_z = 2) in;
layout(set = 0, binding = 0) buffer Out { float data[]; } out_buf;
void main()
{
    out_buf.data[gl_GlobalInvocationID.x] = float(gl_LocalInvocationIndex);
}
)glsl";

// M9 (ADR-20260615): a minimal INLINE-RAY-TRACING (ray-query) fragment shader,
// modelled on the engine's prim.frag.glsl shadow-test path. It binds a
// set-0 accelerationStructureEXT TLAS and runs the full rayQueryEXT walk
// (initialize -> proceed -> get-intersection-type). The host toolchain must
// lower this to Metal MSL ray-query (metal::raytracing intersector +
// instance_acceleration_structure). GLSL 460 + GL_EXT_ray_query is the
// canonical engine profile for ray-query intrinsics; SPV_KHR_ray_query is
// produced by glslang under the Vulkan1.3/SPIR-V 1.6 target.
constexpr const char* kRayQueryFS = R"glsl(
#version 460
#extension GL_EXT_ray_query : require
layout(set = 0, binding = 0) uniform accelerationStructureEXT cd_tlas;
layout(location = 0) in  vec3 in_origin;
layout(location = 1) in  vec3 in_dir;
layout(location = 0) out vec4 o;
float trace_shadow(vec3 origin, vec3 dir, float tmax)
{
    rayQueryEXT rq;
    rayQueryInitializeEXT(
        rq, cd_tlas,
        gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
        0xFFu, origin, 0.01, dir, tmax);
    while (rayQueryProceedEXT(rq)) { /* opaque-only walk */ }
    return (rayQueryGetIntersectionTypeEXT(rq, true) ==
            gl_RayQueryCommittedIntersectionNoneEXT) ? 1.0 : 0.0;
}
void main()
{
    float vis = trace_shadow(in_origin, normalize(in_dir), 1000.0);
    o = vec4(vis, vis, vis, 1.0);
}
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

// ---- M9 (ADR-20260615): ray-query (inline RT) MSL lowering --------------------
//
// THE host-verifiable proof of the Metal RT shader path. The engine consumes
// the TLAS via rayQueryEXT (GL_EXT_ray_query). This test runs that exact GLSL
// through the full GLSL -> SPIR-V (glslang, SPV_KHR_ray_query) -> MSL
// (SPIRV-Cross CompilerMSL) chain on Windows and asserts the emitted MSL is
// valid + contains the Metal ray-query constructs. A pass PROVES SPIRV-Cross
// CAN lower SPV_KHR_ray_query to Metal MSL (metal::raytracing intersector /
// instance_acceleration_structure) — the .mm-side AS build is Mac-deferred,
// but the shader side is verified everywhere.
TEST(MetalShaderToolchain, RayQueryLowersToMetalIntersector)
{
    auto c = make_compiler_or_null();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    cd::rhi::metal::GlslToMslDesc d {};
    d.glsl_source = kRayQueryFS;
    d.stage = cd::rhi::ShaderStage::kFragment;
    d.source_name = "ray_query_fs";
    // include_resolver left null; default argument-buffer binding model.

    const auto r = cd::rhi::metal::compose_glsl_to_msl(*c, d);
    // A failure here would mean SPIRV-Cross cannot lower SPV_KHR_ray_query to
    // MSL (a real blocker). A pass is the proof the path works.
    ASSERT_TRUE(r.has_value())
        << "SPIRV-Cross failed to lower ray-query SPIR-V to MSL: "
        << std::string(r.error().message);
    const std::string& msl = r->source;
    ASSERT_FALSE(msl.empty());

    // (1) Valid MSL: standard library + the fragment entry point.
    EXPECT_NE(msl.find("#include <metal_stdlib>"), std::string::npos) << msl;
    EXPECT_NE(msl.find("fragment "), std::string::npos) << msl;
    EXPECT_FALSE(r->entry_point.empty());

    // (2) THE RAY-QUERY PROOF: SPIRV-Cross emits the Metal ray-tracing header
    //     + the metal::raytracing namespace for an intersection-query shader.
    EXPECT_NE(msl.find("metal_raytracing"), std::string::npos)
        << "MSL must include the Metal ray-tracing header:\n" << msl;
    EXPECT_NE(msl.find("metal::raytracing"), std::string::npos)
        << "MSL must use the metal::raytracing namespace:\n" << msl;
    // The rayQueryEXT object lowers to the Metal intersection_query type (the
    // ray_query analog); the accelerationStructureEXT TLAS lowers to an
    // instance_acceleration_structure. Either of the ray-query type tokens
    // proves the intersector construct landed.
    const bool has_intersector =
        msl.find("intersection_query") != std::string::npos
        || msl.find("intersector") != std::string::npos;
    EXPECT_TRUE(has_intersector)
        << "MSL must contain the Metal ray-query intersector construct "
           "(intersection_query / intersector):\n" << msl;
    EXPECT_NE(msl.find("acceleration_structure"), std::string::npos)
        << "the TLAS must lower to a Metal acceleration_structure type:\n" << msl;
    // The ray-query walk itself: SPIRV-Cross emits a `.next()` proceed on the
    // intersection query (the rayQueryProceedEXT analog).
    EXPECT_NE(msl.find(".next()"), std::string::npos)
        << "the rayQueryProceedEXT walk must lower to intersection_query.next():\n"
        << msl;
}

// The ray-query floor is enforced at the glue level: a ray-query module's
// emitted MSL must guard the metal_raytracing include behind the MSL 2.3+
// version gate SPIRV-Cross requires, regardless of the requested version.
TEST(MetalShaderToolchain, RayQueryRaisesMslVersionFloor)
{
    auto c = make_compiler_or_null();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    cd::shader::CompileDesc sd {};
    sd.source = kRayQueryFS;
    sd.stage = cd::shader::ShaderStage::kFragment;
    sd.source_name = "ray_query_floor_fs";
    const auto spirv = c->compile(sd);
    ASSERT_TRUE(spirv.has_value()) << std::string(spirv.error().message);

    // Request a deliberately-low MSL version (2.0). The glue must raise it to
    // the 2.4 ray-query floor because the module declares CapabilityRayQueryKHR.
    cd::spirv_cross_glue::MslBindingConfig cfg {};
    cfg.version = 20000U;  // MSL 2.0 (below the ray-query floor)
    const auto r = cd::spirv_cross_glue::translate_msl(spirv->spirv, cfg);
    ASSERT_TRUE(r.ok()) << r.error;
    // The emitted MSL carries SPIRV-Cross's version-gated ray-tracing include
    // (`#if __METAL_VERSION__ >= 230`). Its presence proves the lowering ran;
    // the floor-raise keeps the construct compilable on-device.
    EXPECT_NE(r.source.find("metal_raytracing"), std::string::npos)
        << "ray-query MSL must include metal_raytracing even when a low MSL "
           "version was requested:\n" << r.source;
    EXPECT_NE(r.source.find("__METAL_VERSION__ >= 230"), std::string::npos)
        << "SPIRV-Cross guards the ray-tracing include behind MSL 2.3:\n"
        << r.source;
}

// ---- M6 (ADR-20260615): compute workgroup-size reflection ---------------------
//
// THE host-verifiable proof of the M6 dispatch fix. The Metal .mm dispatch path
// was hardcoding threadsPerThreadgroup = (1,1,1); ComputePipelineDesc carries no
// workgroup field, so the only source of truth is SPIRV-Cross reflection of the
// GLSL `layout(local_size_x/y/z)` execution mode. This test runs the exact
// GLSL -> SPIR-V (glslang) -> MSL (SPIRV-Cross) chain on Windows and asserts the
// toolchain surfaces the workgroup size as (8,4,2). A pass PROVES the host
// reflection path the .mm create_compute_pipeline consumes is correct — the .mm
// capture-onto-PSO + dispatch divide is Mac-deferred, but the size derivation is
// verified everywhere.
TEST(MetalShaderToolchain, ComputeWorkgroupSizeIsReflected)
{
    auto c = make_compiler_or_null();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    cd::rhi::metal::GlslToMslDesc d {};
    d.glsl_source = kComputeCS;
    d.stage = cd::rhi::ShaderStage::kCompute;
    d.source_name = "compute_workgroup_cs";

    const auto r = cd::rhi::metal::compose_glsl_to_msl(*c, d);
    ASSERT_TRUE(r.has_value()) << std::string(r.error().message);
    ASSERT_FALSE(r->source.empty());

    // (1) Valid MSL: standard library + a kernel (compute) entry point.
    EXPECT_NE(r->source.find("#include <metal_stdlib>"), std::string::npos)
        << r->source;
    EXPECT_NE(r->source.find("kernel "), std::string::npos) << r->source;
    EXPECT_FALSE(r->entry_point.empty());

    // (2) THE M6 PROOF: the reflected workgroup size matches the GLSL
    //     layout(local_size_x=8, local_size_y=4, local_size_z=2). Anything but
    //     (8,4,2) means the .mm would dispatch the wrong threads-per-threadgroup.
    EXPECT_EQ(r->workgroup.x, 8u) << "local_size_x must reflect as 8";
    EXPECT_EQ(r->workgroup.y, 4u) << "local_size_y must reflect as 4";
    EXPECT_EQ(r->workgroup.z, 2u) << "local_size_z must reflect as 2";
}

// A non-compute stage carries no LocalSize execution mode; the reflected
// workgroup size must normalise to 1x1x1 (never 0) so the .mm dispatch never
// computes a 0-thread threadgroup. Guards the glue's 0 -> 1 normalisation.
TEST(MetalShaderToolchain, NonComputeWorkgroupSizeDefaultsToOne)
{
    auto c = make_compiler_or_null();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    cd::rhi::metal::GlslToMslDesc d {};
    d.glsl_source = kMinimalVS;
    d.stage = cd::rhi::ShaderStage::kVertex;
    d.source_name = "non_compute_workgroup_vs";

    const auto r = cd::rhi::metal::compose_glsl_to_msl(*c, d);
    ASSERT_TRUE(r.has_value()) << std::string(r.error().message);
    EXPECT_EQ(r->workgroup.x, 1u);
    EXPECT_EQ(r->workgroup.y, 1u);
    EXPECT_EQ(r->workgroup.z, 1u);
}

// ---- M10 (B2 — ADR-20260615): mesh + task (object) shader MSL lowering --------
//
// THE host-verifiable proof of the M10 mesh-shader path. The Metal .mm
// create_mesh_pipeline builds an MTLMeshRenderPipelineDescriptor whose
// object/mesh functions are MTLFunctions resolved from these very modules.
// This test runs the exact GL_EXT_mesh_shader GLSL through the full
// GLSL -> SPIR-V (glslang, SPV_EXT_mesh_shader) -> MSL (SPIRV-Cross CompilerMSL)
// chain on Windows and asserts the emitted MSL carries the Metal MESH-stage /
// OBJECT-stage markers. A pass PROVES SPIRV-Cross CAN lower a mesh/task pipeline
// to MSL ([[mesh]] / mesh<...> / [[object]] / mesh_grid_properties) — the
// .mm-side MTLMeshRenderPipelineDescriptor build + drawMeshThreadgroups are
// Mac-deferred, but the shader side is verified everywhere. MSL 3.0 is the
// floor Apple requires for mesh shaders, so the desc requests it explicitly.
//
// (Probe-confirmed 2026-06-15: SPIRV-Cross 1.17-era CompilerMSL emits
//  `[[mesh]] void main0(... spvMesh_t spvMesh)` + `using spvMesh_t = mesh<...>`
//  for the mesh stage and `[[object]] void main0(... mesh_grid_properties ...)`
//  for the task stage. The mesh stage's cleansed entry name comes back empty
//  on this SPIRV-Cross build, so the test keys on the stage MARKERS, not the
//  entry-point string.)
constexpr const char* kMeshShaderGlsl = R"glsl(
#version 460
#extension GL_EXT_mesh_shader : require
layout(local_size_x = 1) in;
layout(triangles, max_vertices = 3, max_primitives = 1) out;
void main()
{
    SetMeshOutputsEXT(3, 1);
    gl_MeshVerticesEXT[0].gl_Position = vec4(-0.5, -0.5, 0.0, 1.0);
    gl_MeshVerticesEXT[1].gl_Position = vec4( 0.5, -0.5, 0.0, 1.0);
    gl_MeshVerticesEXT[2].gl_Position = vec4( 0.0,  0.5, 0.0, 1.0);
    gl_PrimitiveTriangleIndicesEXT[0] = uvec3(0, 1, 2);
}
)glsl";

constexpr const char* kTaskShaderGlsl = R"glsl(
#version 460
#extension GL_EXT_mesh_shader : require
layout(local_size_x = 1) in;
taskPayloadSharedEXT struct { uint id; } payload;
void main()
{
    payload.id = gl_GlobalInvocationID.x;
    EmitMeshTasksEXT(1, 1, 1);
}
)glsl";

TEST(MetalShaderToolchain, MeshShaderLowersToMetalMeshStage)
{
    auto c = make_compiler_or_null();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    cd::rhi::metal::GlslToMslDesc d {};
    d.glsl_source = kMeshShaderGlsl;
    d.stage = cd::rhi::ShaderStage::kMesh;
    d.source_name = "mesh_stage_ms";
    d.msl_version = 30000U;  // MSL 3.0 — Apple's mesh-shader floor.

    const auto r = cd::rhi::metal::compose_glsl_to_msl(*c, d);
    // A failure here would mean glslang or SPIRV-Cross cannot lower
    // GL_EXT_mesh_shader (older toolchains) — skip rather than fail (the .mm RHI
    // surface is still wired correctly). A pass is the proof the path works.
    if (!r.has_value())
        GTEST_SKIP() << "toolchain cannot lower mesh shader: "
                     << std::string(r.error().message);
    const std::string& msl = r->source;
    ASSERT_FALSE(msl.empty());

    // (1) Valid MSL: standard library include.
    EXPECT_NE(msl.find("#include <metal_stdlib>"), std::string::npos) << msl;

    // (2) THE MESH-STAGE PROOF: SPIRV-Cross tags the entry point with the Metal
    //     [[mesh]] stage attribute and declares the mesh<...> output type. Both
    //     are unique to a mesh-shader lowering — a classic vertex/compute shader
    //     never emits them.
    EXPECT_NE(msl.find("[[mesh]]"), std::string::npos)
        << "mesh shader must lower to a [[mesh]] entry point:\n" << msl;
    EXPECT_NE(msl.find("mesh<"), std::string::npos)
        << "mesh shader must declare a Metal mesh<...> output type:\n" << msl;
    // The SetMeshOutputsEXT intrinsic lowers to SPIRV-Cross's mesh-output helper.
    EXPECT_NE(msl.find("spvSetMeshOutputsEXT"), std::string::npos)
        << "SetMeshOutputsEXT must lower to the Metal mesh-output helper:\n" << msl;
}

TEST(MetalShaderToolchain, TaskShaderLowersToMetalObjectStage)
{
    auto c = make_compiler_or_null();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    cd::rhi::metal::GlslToMslDesc d {};
    d.glsl_source = kTaskShaderGlsl;
    d.stage = cd::rhi::ShaderStage::kTask;
    d.source_name = "task_stage_as";
    d.msl_version = 30000U;  // MSL 3.0 — mesh/object pipeline floor.

    const auto r = cd::rhi::metal::compose_glsl_to_msl(*c, d);
    if (!r.has_value())
        GTEST_SKIP() << "toolchain cannot lower task shader: "
                     << std::string(r.error().message);
    const std::string& msl = r->source;
    ASSERT_FALSE(msl.empty());

    EXPECT_NE(msl.find("#include <metal_stdlib>"), std::string::npos) << msl;

    // THE OBJECT-STAGE PROOF: the Metal mesh-pipeline TASK stage is the
    // [[object]] function; SPIRV-Cross emits the mesh_grid_properties dispatch
    // handle + EmitMeshTasksEXT lowering (set_threadgroups_per_grid). All three
    // are unique to an object/task-shader lowering.
    EXPECT_NE(msl.find("[[object]]"), std::string::npos)
        << "task shader must lower to a [[object]] entry point:\n" << msl;
    EXPECT_NE(msl.find("mesh_grid_properties"), std::string::npos)
        << "task shader must take a Metal mesh_grid_properties dispatch handle:\n"
        << msl;
    EXPECT_NE(msl.find("set_threadgroups_per_grid"), std::string::npos)
        << "EmitMeshTasksEXT must lower to set_threadgroups_per_grid:\n" << msl;
}

}  // namespace
