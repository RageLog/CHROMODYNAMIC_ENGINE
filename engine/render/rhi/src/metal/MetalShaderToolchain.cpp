// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/src/metal/MetalShaderToolchain.cpp
// phase M3 (Metal backend): GLSL → SPIR-V → MSL chain composition.
//
// PURE HOST-SIDE C++ (NOT a .mm): the output is MSL text, so this TU builds and
// tests on Windows with no Metal framework. Built UNCONDITIONALLY into the
// host-side cd::rhi_metal_shader library; only the .mm `MetalDevice` is gated
// behind CD_RHI_METAL_ENABLED / Apple.
//
// See the header + ADR-20260614-d3d12-binding-model §4 for the binding model.
// =============================================================================
#include <cd/rhi/metal/MetalShaderToolchain.hpp>

#include <cd/gluon/ModuleRegistry.hpp>
#include <cd/spirv_cross_glue/Translate.hpp>

#include <string>
#include <utility>

namespace cd::rhi::metal
{

namespace
{

/// cd::rhi::ShaderStage → cd::shader::ShaderStage. The two enums are a
/// deliberate copy (Compiler.hpp keeps the shader library independent of the
/// RHI headers), so the mapping is explicit and exhaustive — mirrors the D3D12
/// toolchain's `to_shader_stage`.
[[nodiscard]] cd::shader::ShaderStage to_shader_stage(cd::rhi::ShaderStage s) noexcept
{
    using RS = cd::rhi::ShaderStage;
    using SS = cd::shader::ShaderStage;
    switch (s)
    {
        case RS::kVertex:       return SS::kVertex;
        case RS::kFragment:     return SS::kFragment;
        case RS::kCompute:      return SS::kCompute;
        case RS::kGeometry:     return SS::kGeometry;
        case RS::kTessControl:  return SS::kTessControl;
        case RS::kTessEval:     return SS::kTessEval;
        case RS::kRayGen:       return SS::kRaygen;
        case RS::kMiss:         return SS::kMiss;
        case RS::kClosestHit:   return SS::kClosestHit;
        case RS::kAnyHit:       return SS::kAnyHit;
        case RS::kIntersection: return SS::kIntersection;
        case RS::kCallable:     return SS::kCallable;
        case RS::kMesh:         return SS::kMesh;
        case RS::kTask:         return SS::kTask;
        case RS::kNone:         return SS::kVertex;
        case RS::kAllGraphics:  return SS::kVertex;
        default:                return SS::kVertex;
    }
}

[[nodiscard]] cd::core::ErrorCode prefixed(std::string_view stage_name,
                                           std::string_view detail,
                                           cd::shader::shader_errors::Code code)
{
    std::string msg { stage_name };
    msg += ": ";
    msg += detail;
    return cd::shader::shader_errors::make_owning(code, std::move(msg));
}

/// Shared Stage 2 (SPIR-V → MSL via SPIRV-Cross). No Metal API touched — the
/// output is text. Used by both the GLSL and SPIR-V entry points.
[[nodiscard]] cd::core::Result<MslArtifact>
spirv_to_msl_tail(std::span<const std::uint32_t> spirv, const GlslToMslDesc& desc)
{
    cd::spirv_cross_glue::MslBindingConfig cfg {};
    cfg.version = desc.msl_version;
    cfg.argument_buffers = desc.binding.argument_buffers;
    cfg.push_constant_buffer_index = desc.binding.push_constant_buffer_index;

    const auto msl = cd::spirv_cross_glue::translate_msl(spirv, cfg);
    if (!msl.ok())
    {
        return std::unexpected(prefixed("spirv-cross", msl.error,
                                        cd::shader::shader_errors::Code::kCompileFailed));
    }

    MslArtifact out {};
    out.source = msl.source;
    out.entry_point = msl.entry_point;
    // M6 (ADR-20260615): surface the reflected compute local workgroup size so
    // the .mm dispatch path can use the real threads-per-threadgroup instead of
    // the hardcoded 1x1x1. 1x1x1 for a non-compute module (glue normalises 0).
    out.workgroup.x = msl.workgroup.x;
    out.workgroup.y = msl.workgroup.y;
    out.workgroup.z = msl.workgroup.z;
    return out;
}

}  // namespace

cd::core::Result<MslArtifact>
compose_glsl_to_msl(cd::shader::ICompiler& spirv_compiler, const GlslToMslDesc& desc)
{
    if (desc.glsl_source.empty())
    {
        return std::unexpected(cd::shader::shader_errors::make(
            cd::shader::shader_errors::Code::kInvalidArgument,
            "compose_glsl_to_msl: empty GLSL source"));
    }

    // ---- Stage 1: GLSL → SPIR-V (glslang; cached when the caller passes X5's
    // CachedCompiler). A null include_resolver bridges to the embedded gluon
    // catalogue via a function-local ModuleResolver
    // (ADR-20260614-gluon-consumer-resolver-pattern §2.2/§2.3): stateless +
    // deterministic, built BEFORE compile() and alive across the call.
    cd::gluon::ModuleResolver default_resolver {};
    cd::shader::IIncludeResolver* const include_resolver =
        desc.include_resolver != nullptr ? desc.include_resolver : &default_resolver;

    cd::shader::CompileDesc sd {};
    sd.source = desc.glsl_source;
    sd.stage = to_shader_stage(desc.stage);
    sd.lang = cd::shader::ShaderLanguage::kGlsl;
    sd.target = cd::shader::TargetEnv::kVulkan13;
    sd.source_name = desc.source_name;
    sd.generate_debug_info = desc.generate_debug_info;
    sd.include_resolver = include_resolver;
    auto spirv_r = spirv_compiler.compile(sd);
    if (!spirv_r.has_value())
    {
        return std::unexpected(prefixed("glslang", spirv_r.error().message,
                                        cd::shader::shader_errors::Code::kCompileFailed));
    }

    // ---- Stage 2: SPIR-V → MSL (SPIRV-Cross) --------------------------------
    return spirv_to_msl_tail(spirv_r->spirv, desc);
}

cd::core::Result<MslArtifact>
compose_spirv_to_msl(std::span<const std::uint32_t> spirv, const GlslToMslDesc& desc)
{
    if (spirv.empty())
    {
        return std::unexpected(cd::shader::shader_errors::make(
            cd::shader::shader_errors::Code::kInvalidArgument,
            "compose_spirv_to_msl: empty SPIR-V module"));
    }
    return spirv_to_msl_tail(spirv, desc);
}

}  // namespace cd::rhi::metal
