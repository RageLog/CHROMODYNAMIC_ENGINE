// =============================================================================
// CHROMODYNAMIC — cd/spirv_cross_glue/Translate.cpp
// B-infra1 — SPIRV-Cross C++ API bridge.
//
// All three backends (GLSL, HLSL, MSL) are compiled unconditionally — the
// CMakeLists.txt already gates SPIRV_CROSS_ENABLE_{GLSL,HLSL,MSL}=ON and
// links the corresponding static libraries.
//
// Exception policy: SPIRV-Cross throws `spirv_cross::CompilerError` (a
// std::runtime_error subclass) for malformed input. We catch at the API
// boundary and convert to TranslateResult::error so callers never need an
// exception-aware call stack.
// =============================================================================
#include <cd/spirv_cross_glue/Translate.hpp>

// SPIRV-Cross is vendored/FetchContent; suppress its diagnostics so they
// don't trip the engine's -Wall -Werror build.
#if defined(_MSC_VER)
    #pragma warning(push, 0)
#endif
#if defined(__clang__) || defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wold-style-cast"
    #pragma GCC diagnostic ignored "-Wshadow"
    #pragma GCC diagnostic ignored "-Wsign-conversion"
    #pragma GCC diagnostic ignored "-Wconversion"
    #pragma GCC diagnostic ignored "-Wmissing-declarations"
    #pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <spirv_glsl.hpp>
#include <spirv_hlsl.hpp>
#include <spirv_msl.hpp>

#if defined(_MSC_VER)
    #pragma warning(pop)
#endif
#if defined(__clang__) || defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace cd::spirv_cross_glue
{

namespace
{

// Auto-pick version constants (version == 0 path).
constexpr std::uint32_t kDefaultGlslVersion = 450U;    // GLSL 4.50
constexpr std::uint32_t kDefaultHlslVersion = 60U;     // HLSL SM 6.0
constexpr std::uint32_t kDefaultMslVersion  = 20200U;  // MSL 2.2
// Argument buffers require MSL 2.0 (packed 20000). The set-per-argument-buffer
// binding model (phase M3) raises any lower request to this floor.
constexpr std::uint32_t kMinArgumentBufferMslVersion = 20000U;  // MSL 2.0
// M9 (ADR-20260615): inline ray tracing (ray-query) lowers to the MSL
// metal::raytracing intersector. SPIRV-Cross emits the
// `#include <metal_raytracing>` block under `#if __METAL_VERSION__ >= 230`
// (spirv_msl.cpp ~1696) and selects the modern
// `acceleration_structure<instancing>` AS type only at MSL >= 2.4
// (spirv_msl.cpp ~14965). When the SPIR-V declares CapabilityRayQueryKHR we
// raise the floor to MSL 2.4 so the emitted MSL's ray-query constructs are
// active + use the current AS type. Below this floor the include is gated out
// and the shader would not compile on-device.
constexpr std::uint32_t kMinRayQueryMslVersion = 20400U;  // MSL 2.4

// Base [[buffer(N)]] index the 11 SPIRV-Cross CompilerMSL auxiliary buffers are
// pinned to (phase1122 namespace fix). Mirrors CompilerMSL's upstream defaults
// (spirv_msl.hpp: [20..30]) but is set EXPLICITLY so the aux range is
// deterministic + provably disjoint from the engine's vertex-input range
// [9..15] (cd::rhi::metal::kVertexBufferBaseIndex). This is the lower-layer
// glue's local copy of cd::rhi::metal::kSpirvCrossAuxBaseIndex — the rhi/metal
// header sits ABOVE this library in the DAG, so the value is duplicated here by
// design (one number, two layers); the host test asserts both agree.
constexpr std::uint32_t kSpirvCrossAuxBaseIndex = 20U;

// M6 (ADR-20260615): normalise a SPIRV-Cross reflected entry-point workgroup
// size into our WorkgroupSize. A non-compute module reports 0 for each
// component (no SPIR-V LocalSize execution mode); we clamp to 1 so the Metal
// threads-per-threadgroup never collapses to a 0-thread dispatch.
[[nodiscard]] WorkgroupSize reflect_workgroup_size(
    const spirv_cross::SPIREntryPoint& ep) noexcept
{
    WorkgroupSize w {};
    w.x = ep.workgroup_size.x != 0U ? ep.workgroup_size.x : 1U;
    w.y = ep.workgroup_size.y != 0U ? ep.workgroup_size.y : 1U;
    w.z = ep.workgroup_size.z != 0U ? ep.workgroup_size.z : 1U;
    return w;
}

[[nodiscard]] TranslateResult translate_glsl(
    const std::vector<std::uint32_t>& words,
    std::uint32_t                      version)
{
    try
    {
        spirv_cross::CompilerGLSL compiler { words };
        spirv_cross::CompilerGLSL::Options opts {};
        opts.version = (version == 0U) ? kDefaultGlslVersion : version;
        opts.es = false;
        opts.vulkan_semantics = false;  // emit GL-compatible GLSL, not Vulkan-GLSL
        compiler.set_common_options(opts);
        return TranslateResult { compiler.compile(), {} };
    }
    catch (const std::exception& ex)
    {
        return TranslateResult { {}, std::string("spirv-cross GLSL: ") + ex.what() };
    }
}

[[nodiscard]] TranslateResult translate_hlsl(
    const std::vector<std::uint32_t>& words,
    std::uint32_t                      version)
{
    try
    {
        spirv_cross::CompilerHLSL compiler { words };
        spirv_cross::CompilerHLSL::Options hlsl_opts {};
        // shader_model is expressed as major*10 + minor in SPIRV-Cross options.
        // Default: SM 6.0 → 60. If caller passes e.g. 51 → SM 5.1.
        const auto sm =
            static_cast<std::uint32_t>((version == 0U) ? kDefaultHlslVersion : version);
        hlsl_opts.shader_model = sm;
        compiler.set_hlsl_options(hlsl_opts);

        // ---- D3D12 binding model: space-per-set + explicit push_constant remap
        // (ADR-20260614-d3d12-binding-model). On SM>=51 SPIRV-Cross already
        // emits resource bindings as `register(<class>M, spaceN)` where N is the
        // Vulkan descriptor-set index and M the binding index — matching the
        // D3D12 root signature's per-set table layout (space N). So NO resource
        // remap is needed here.
        //
        // The push_constant block, however, is emitted register-LESS by default
        // (its desc_set == ResourceBindingPushConstantDescriptorSet == ~0u), so
        // DXC auto-assigns it b0/space0 — which both collides with the set-0 CBV
        // b0 and misses the root signature's root-constant slot at b0/space1.
        // Force it onto b0/space1 via a RootConstants layout that spans the
        // declared push_constant struct size (the same byte range the D3D12 root
        // signature derives for its 32-bit-constants slot).
        if (sm >= 51U)
        {
            const spirv_cross::ShaderResources res = compiler.get_shader_resources();
            if (!res.push_constant_buffers.empty())
            {
                const spirv_cross::Resource& pc = res.push_constant_buffers.front();
                const std::size_t struct_size =
                    compiler.get_declared_struct_size(compiler.get_type(pc.base_type_id));
                // RootConstants byte range must be a multiple of 4 (SPIRV-Cross
                // contract). push_constant blocks are always 4-byte aligned, but
                // round up defensively so a partial trailing word is covered.
                const std::uint32_t end =
                    (static_cast<std::uint32_t>(struct_size) + 3U) & ~3U;
                if (end > 0U)
                {
                    std::vector<spirv_cross::RootConstants> rc(1);
                    rc[0].start   = 0U;
                    rc[0].end     = end;
                    rc[0].binding = 0U;   // b0
                    rc[0].space   = 1U;   // space1 (matches root sig 32-bit-constants slot)
                    compiler.set_root_constant_layouts(std::move(rc));
                }
            }
        }

        return TranslateResult { compiler.compile(), {} };
    }
    catch (const std::exception& ex)
    {
        return TranslateResult { {}, std::string("spirv-cross HLSL: ") + ex.what() };
    }
}

[[nodiscard]] TranslateResult translate_msl_impl(
    const std::vector<std::uint32_t>& words,
    std::uint32_t                      version,
    bool                               argument_buffers,
    std::uint32_t                      push_constant_buffer_index)
{
    try
    {
        spirv_cross::CompilerMSL compiler { words };
        spirv_cross::CompilerMSL::Options msl_opts {};
        // msl_version is a packed (major*10000 + minor*100) integer.
        // Default: MSL 2.2 → 20200.
        msl_opts.msl_version = (version == 0U) ? kDefaultMslVersion : version;

        // ---- Binding model (phase M3, ADR-20260614-d3d12-binding-model §4)
        // SET-PER-ARGUMENT-BUFFER — the Metal analog of D3D12 space-per-set.
        // With argument_buffers ON, SPIRV-Cross emits one argument-buffer
        // struct per Vulkan descriptor set; the engine convention binds the
        // argument buffer for set N at Metal [[buffer(N)]]. Resources inside a
        // set keep their `binding` as the [[id(binding)]] within the struct, so
        // (set, binding) survives 1:1 — exactly mirroring "space N == set N".
        // Argument buffers require MSL >= 2.0; raise the floor defensively.
        if (argument_buffers)
        {
            msl_opts.argument_buffers = true;
            // Raise to the MSL 2.0 floor argument buffers require.
            msl_opts.msl_version =
                std::max(msl_opts.msl_version, kMinArgumentBufferMslVersion);
        }

        // ---- M9 (ADR-20260615): inline ray tracing (ray-query) floor.
        // If the module declares CapabilityRayQueryKHR (the engine's
        // rayQueryEXT path: GL_EXT_ray_query -> SPV_KHR_ray_query), raise the
        // MSL version to 2.4 so SPIRV-Cross's emitted `#include
        // <metal_raytracing>` block (guarded `#if __METAL_VERSION__ >= 230`)
        // is active and the acceleration-structure type lowers to the current
        // `acceleration_structure<instancing>` form. This is the host-side
        // half of the Metal RT surface (AS build is the .mm/Mac side); the
        // emitted MSL ray-query is fully validated here on every platform.
        {
            const spirv_cross::SmallVector<spv::Capability>& caps =
                compiler.get_declared_capabilities();
            const bool uses_ray_query =
                std::ranges::find(caps, spv::CapabilityRayQueryKHR)
                != caps.end();
            if (uses_ray_query)
            {
                msl_opts.msl_version =
                    std::max(msl_opts.msl_version, kMinRayQueryMslVersion);
            }
        }

        // ---- SPIRV-Cross auxiliary-buffer pin (phase1122 namespace fix).
        // CompilerMSL emits its OWN [[buffer(N)]] auxiliary buffers (swizzle /
        // buffer-size / indirect-params / view-mask / output / tess / dynamic-
        // offsets / input / index). They DEFAULT to the top of the per-stage
        // namespace, [20..30] (spirv_msl.hpp Options). The engine's Metal
        // binding map (MetalInternal.hpp) reserves vertex-input buffers at
        // [9..15] and pins these aux buffers to that SAME canonical top range so
        // they are DETERMINISTIC and provably DISJOINT from the vertex range —
        // they can never drift down into [9..15] across SPIRV-Cross versions.
        // The values mirror the upstream defaults but are set explicitly so the
        // contract is host-verifiable (see test_metal_shader_toolchain.cpp).
        //   base = kSpirvCrossAuxBaseIndex = 20.
        msl_opts.shader_patch_input_buffer_index  = kSpirvCrossAuxBaseIndex + 0U;   // 20
        msl_opts.shader_index_buffer_index        = kSpirvCrossAuxBaseIndex + 1U;   // 21
        msl_opts.shader_input_buffer_index        = kSpirvCrossAuxBaseIndex + 2U;   // 22
        msl_opts.dynamic_offsets_buffer_index     = kSpirvCrossAuxBaseIndex + 3U;   // 23
        msl_opts.view_mask_buffer_index           = kSpirvCrossAuxBaseIndex + 4U;   // 24
        msl_opts.buffer_size_buffer_index         = kSpirvCrossAuxBaseIndex + 5U;   // 25
        msl_opts.shader_tess_factor_buffer_index  = kSpirvCrossAuxBaseIndex + 6U;   // 26
        msl_opts.shader_patch_output_buffer_index = kSpirvCrossAuxBaseIndex + 7U;   // 27
        msl_opts.shader_output_buffer_index       = kSpirvCrossAuxBaseIndex + 8U;   // 28
        msl_opts.indirect_params_buffer_index     = kSpirvCrossAuxBaseIndex + 9U;   // 29
        msl_opts.swizzle_buffer_index             = kSpirvCrossAuxBaseIndex + 10U;  // 30

        compiler.set_msl_options(msl_opts);

        // ---- push_constant -> its own dedicated [[buffer(n)]] slot.
        // SPIRV-Cross addresses the push_constant block with the reserved
        // (desc_set, binding) == (kPushConstDescSet, kPushConstBinding). Remap
        // it to a fixed buffer index OUTSIDE the descriptor-set range so the
        // device can `setVertex/FragmentBytes` the push range at one stable
        // slot regardless of how many sets the shader declares — the analog of
        // the D3D12 "push_constant -> b0/space1 root constants" decision.
        if (!words.empty())
        {
            const spirv_cross::ShaderResources res = compiler.get_shader_resources();
            if (!res.push_constant_buffers.empty())
            {
                spirv_cross::MSLResourceBinding pc {};
                pc.stage    = compiler.get_execution_model();
                pc.desc_set = spirv_cross::kPushConstDescSet;
                pc.binding  = spirv_cross::kPushConstBinding;
                pc.count    = 1U;
                pc.msl_buffer  = push_constant_buffer_index;
                pc.msl_texture = 0U;
                pc.msl_sampler = 0U;
                compiler.add_msl_resource_binding(pc);
            }
        }

        std::string source = compiler.compile();
        // SPIRV-Cross renames the MSL entry point per stage (reserved-word
        // dodge: a fragment "main" becomes "main0"). Report the cleansed name
        // so the device looks the function up correctly on the MTLLibrary.
        const spirv_cross::SmallVector<spirv_cross::EntryPoint> eps =
            compiler.get_entry_points_and_stages();
        std::string entry;
        if (!eps.empty())
        {
            entry = compiler.get_cleansed_entry_point_name(
                eps.front().name, eps.front().execution_model);
        }
        TranslateResult out {};
        out.source = std::move(source);
        out.entry_point = std::move(entry);
        // M6 (ADR-20260615): reflect the compute local workgroup size from the
        // entry point's SPIR-V LocalSize execution mode
        // (SPIREntryPoint::WorkgroupSize, the GLSL layout(local_size_*) decl).
        // compile() above runs the reflection passes, so the entry point's
        // workgroup_size is now populated; a non-compute module reports 0 here,
        // which we normalise to 1 so the Metal threads-per-threadgroup never
        // collapses to a 0-thread dispatch. This is the source of truth the .mm
        // dispatch() consumes (ComputePipelineDesc has no workgroup field).
        // The base Compiler::get_entry_point() is protected, so query through
        // the public named overload using the entry point we already enumerated.
        if (!eps.empty())
        {
            const spirv_cross::SPIREntryPoint& ep =
                compiler.get_entry_point(eps.front().name,
                                         eps.front().execution_model);
            out.workgroup = reflect_workgroup_size(ep);
        }
        return out;
    }
    catch (const std::exception& ex)
    {
        return TranslateResult { {}, std::string("spirv-cross MSL: ") + ex.what() };
    }
}

[[nodiscard]] TranslateResult translate_msl(
    const std::vector<std::uint32_t>& words,
    std::uint32_t                      version)
{
    // Plain Target::kMsl path: classic flat binding (no argument buffers),
    // push_constant left at SPIRV-Cross's default auto-allocated buffer slot.
    // Preserves the historical `translate(..., kMsl)` output byte-for-byte.
    try
    {
        spirv_cross::CompilerMSL compiler { words };
        spirv_cross::CompilerMSL::Options msl_opts {};
        msl_opts.msl_version = (version == 0U) ? kDefaultMslVersion : version;
        compiler.set_msl_options(msl_opts);
        TranslateResult out {};
        out.source = compiler.compile();
        // M6: same workgroup reflection on the flat-binding path (see above);
        // query through the public named get_entry_point overload.
        const spirv_cross::SmallVector<spirv_cross::EntryPoint> eps =
            compiler.get_entry_points_and_stages();
        if (!eps.empty())
        {
            const spirv_cross::SPIREntryPoint& ep =
                compiler.get_entry_point(eps.front().name,
                                         eps.front().execution_model);
            out.workgroup = reflect_workgroup_size(ep);
        }
        return out;
    }
    catch (const std::exception& ex)
    {
        return TranslateResult { {}, std::string("spirv-cross MSL: ") + ex.what() };
    }
}

}  // namespace

TranslateResult translate(
    std::span<const std::uint32_t> spirv,
    Target                         target,
    std::uint32_t                  version)
{
    if (spirv.empty())
    {
        return TranslateResult { {}, "spirv-cross: empty SPIR-V input" };
    }

    // spirv_cross constructors accept std::vector<uint32_t>; copy once.
    const std::vector<std::uint32_t> words { spirv.begin(), spirv.end() };

    switch (target)
    {
        case Target::kGlsl:
            return translate_glsl(words, version);
        case Target::kHlsl:
            return translate_hlsl(words, version);
        case Target::kMsl:
            return translate_msl(words, version);
    }

    // Unreachable: switch is exhaustive over the enum.
    return TranslateResult { {}, "spirv-cross: unknown target" };
}

TranslateResult translate_msl(
    std::span<const std::uint32_t> spirv,
    const MslBindingConfig&        cfg)
{
    if (spirv.empty())
    {
        return TranslateResult { {}, "spirv-cross: empty SPIR-V input" };
    }
    const std::vector<std::uint32_t> words { spirv.begin(), spirv.end() };
    return translate_msl_impl(words, cfg.version, cfg.argument_buffers,
                              cfg.push_constant_buffer_index);
}

}  // namespace cd::spirv_cross_glue
