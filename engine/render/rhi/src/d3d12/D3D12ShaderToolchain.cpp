// =============================================================================
// CHROMODYNAMIC — cd/rhi/d3d12/D3D12ShaderToolchain.cpp
// phase1141 (X4-C1): GLSL → SPIR-V → HLSL(SM6.x) → DXIL chain
// composition. See the header + ADR-20260612-x4-d3d12-shader-toolchain
// §2.4 for the binding invariants.
// =============================================================================
#include <cd/rhi/d3d12/D3D12ShaderToolchain.hpp>

#include <cd/spirv_cross_glue/Translate.hpp>

#include <string>
#include <utility>

namespace cd::rhi::d3d12
{

namespace
{

/// cd::rhi::ShaderStage → cd::shader::ShaderStage. The two enums are a
/// deliberate copy (Compiler.hpp keeps the shader library independent of
/// the RHI headers), so the mapping is explicit and exhaustive.
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
        default:                return SS::kVertex;
    }
}

/// SPIRV-Cross HLSL `version` hint: shader model × 10 (Translate.hpp
/// contract). kSM5_1 is rejected before this is called.
[[nodiscard]] std::uint32_t hlsl_version_for(ShaderModel m) noexcept
{
    switch (m)
    {
        case ShaderModel::kSM6_0: return 60;
        case ShaderModel::kSM6_5: return 65;
        case ShaderModel::kSM5_1: break;  // rejected by the caller
    }
    return 65;
}

[[nodiscard]] cd::core::ErrorCode prefixed(std::string_view stage_name,
                                           std::string_view detail,
                                           shader_errors::Code code)
{
    std::string msg { stage_name };
    msg += ": ";
    msg += detail;
    return cd::core::ErrorCode::make_owning(
        shader_errors::kDomain, static_cast<std::uint32_t>(code),
        std::move(msg));
}

}  // namespace

cd::core::Result<std::vector<std::uint8_t>>
compile_glsl_to_dxil(cd::shader::ICompiler& spirv_compiler,
                     const GlslToDxilDesc& desc)
{
#if !defined(_WIN32)
    (void)spirv_compiler;
    (void)desc;
    return std::unexpected(shader_errors::make(
        shader_errors::Code::kBackendUnavailable,
        "compile_glsl_to_dxil: non-Windows build"));
#else
    if (desc.glsl_source.empty())
    {
        return std::unexpected(shader_errors::make(
            shader_errors::Code::kCompileFailed,
            "compile_glsl_to_dxil: empty GLSL source"));
    }
    if (desc.model == ShaderModel::kSM5_1)
    {
        // ADR §2.4: the single-source chain is SM6-only — the SM5.1
        // D3DCompile path has no SPIRV-Cross-compatible profile set.
        return std::unexpected(shader_errors::make(
            shader_errors::Code::kUnsupportedStage,
            "compile_glsl_to_dxil: kSM5_1 is invalid on the GLSL chain "
            "(use compile_hlsl directly for legacy SM5.1)"));
    }

    // ---- Stage 1: GLSL → SPIR-V (glslang; cached when the caller passes
    // X5's CachedCompiler) ---------------------------------------------------
    cd::shader::CompileDesc sd {};
    sd.source = desc.glsl_source;
    sd.stage = to_shader_stage(desc.stage);
    sd.lang = cd::shader::ShaderLanguage::kGlsl;
    sd.target = cd::shader::TargetEnv::kVulkan13;
    sd.source_name = desc.source_name;
    sd.generate_debug_info = desc.generate_debug_info;
    sd.include_resolver = desc.include_resolver;
    auto spirv_r = spirv_compiler.compile(sd);
    if (!spirv_r.has_value())
    {
        return std::unexpected(prefixed("glslang", spirv_r.error().message,
                                        shader_errors::Code::kCompileFailed));
    }

    // ---- Stage 2: SPIR-V → HLSL (SPIRV-Cross; exceptions are contained
    // inside translate() per Translate.hpp) -----------------------------------
    const auto hlsl = cd::spirv_cross_glue::translate(
        spirv_r->spirv, cd::spirv_cross_glue::Target::kHlsl,
        hlsl_version_for(desc.model));
    if (!hlsl.ok())
    {
        return std::unexpected(prefixed("spirv-cross", hlsl.error,
                                        shader_errors::Code::kCompileFailed));
    }

    // ---- Stage 3: HLSL → DXIL (DXC) -----------------------------------------
    CompileOptions co {};
    co.source = hlsl.source;
    co.entry_point = desc.entry_point;
    co.stage = desc.stage;
    co.source_name = desc.source_name;
    co.optimization_level = desc.optimization_level;
    co.model = desc.model;
    auto dxil = compile_hlsl(co);
    if (!dxil.has_value())
    {
        return std::unexpected(prefixed("dxc", dxil.error().message,
                                        static_cast<shader_errors::Code>(
                                            dxil.error().code)));
    }
    return dxil;
#endif
}

}  // namespace cd::rhi::d3d12
