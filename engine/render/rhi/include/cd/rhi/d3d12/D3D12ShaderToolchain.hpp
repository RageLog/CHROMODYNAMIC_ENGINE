// =============================================================================
// CHROMODYNAMIC — cd/rhi/d3d12/D3D12ShaderToolchain.hpp
// phase1141 (X4-C1, ADR-20260612-x4-d3d12-shader-toolchain §2.4): the
// single-source shader chain for the D3D12 backend —
//
//   GLSL (Vulkan-style, the engine corpus)
//     → SPIR-V        (cd::shader::ICompiler — pass X5's CachedCompiler
//                      and the SPIR-V half caches automatically)
//     → HLSL SM6.x    (cd::spirv_cross_glue::translate)
//     → DXIL          (cd::rhi::d3d12::compile_hlsl, DXC path)
//
// Error chaining invariant (ADR §2.4-1): whichever stage fails, the
// returned ErrorCode message is prefixed with the stage name
// ("glslang: ..." / "spirv-cross: ..." / "dxc: ...") — blind debugging
// across a three-tool chain is forbidden.
//
// Non-Windows builds return shader_errors::kBackendUnavailable (same
// pattern as D3D12ShaderCompile.cpp).
// =============================================================================
#pragma once

#include <cd/core/Result.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/d3d12/D3D12ShaderCompile.hpp>
#include <cd/shader/Compiler.hpp>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace cd::rhi::d3d12
{

/// GLSL → SPIR-V → HLSL(SM6.x) → DXIL end-to-end chain description.
struct GlslToDxilDesc
{
    std::string_view glsl_source;  ///< Vulkan-style GLSL. Must be non-empty.
    cd::rhi::ShaderStage stage { cd::rhi::ShaderStage::kVertex };
    std::string_view entry_point { "main" };  ///< Entry in the HLSL output; GLSL stays "main".
    std::string_view source_name { "<inline>" };
    ShaderModel model { ShaderModel::kSM6_5 };  ///< kSM5_1 is INVALID on this path (kInvalidArgument-class error).
    std::uint32_t optimization_level { 3 };
    bool generate_debug_info { false };
    /// phase1141: optional include resolver forwarded to the GLSL half —
    /// shader-library modules (cd::gluon) resolve on the D3D12 path too.
    cd::shader::IIncludeResolver* include_resolver { nullptr };
};

/// Run the chain. `spirv_compiler` is caller-owned (X5's CachedCompiler
/// may be passed — the SPIR-V half is then cached automatically). The
/// returned blob feeds straight into ShaderModuleDesc::code/code_size.
[[nodiscard]] cd::core::Result<std::vector<std::uint8_t>>
compile_glsl_to_dxil(cd::shader::ICompiler& spirv_compiler,
                     const GlslToDxilDesc& desc);

/// SPIR-V → HLSL(SM6.x) → DXIL tail of the chain — the same Stage 2 + 3 as
/// `compile_glsl_to_dxil`, but starting from a pre-built SPIR-V module
/// instead of GLSL source. Used by the D3D12 `create_shader_module` path
/// when a caller hands the device SPIR-V words (`ShaderSourceLanguage::
/// kSpirv`). `kSM5_1` is rejected (SM6-only chain), mirroring the GLSL
/// entry point. Stage-prefixed error chaining is preserved.
[[nodiscard]] cd::core::Result<std::vector<std::uint8_t>>
compile_spirv_to_dxil(std::span<const std::uint32_t> spirv,
                      const GlslToDxilDesc& desc);

}  // namespace cd::rhi::d3d12
