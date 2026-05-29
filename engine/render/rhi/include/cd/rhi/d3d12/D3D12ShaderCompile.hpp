// =============================================================================
// CHROMODYNAMIC — cd/rhi/d3d12/D3D12ShaderCompile.hpp
// Phase 14.C / Wave 159 — HLSL → DXIL bytecode compile helper.
//
// Wraps `D3DCompile2` from d3dcompiler.dll (Windows SDK) so sample +
// app code can produce a DXIL blob at runtime without including
// d3dcompiler.h directly. Pass the resulting bytecode to
// `cd::rhi::d3d12::create_d3d12_device()`-built devices via the standard
// `cd::rhi::ShaderModuleDesc::code` + `code_size` fields.
//
// This is the D3D12 counterpart of `cd::shader::compile_glsl` for the
// Vulkan path. Both produce backend-native bytecode that the engine's
// `IDevice::create_shader_module` consumes uniformly.
//
// Targets accepted (Shader Model 6 / D3D12-compatible):
//   ShaderStage::kVertex   → "vs_5_0" / "vs_5_1"
//   ShaderStage::kFragment → "ps_5_0" / "ps_5_1"
//   ShaderStage::kCompute  → "cs_5_0" / "cs_5_1"
// (Higher targets like vs_6_x require DXC which is its own
// vendored dependency. Phase 14.C ships D3DCompile / SM 5_x; DXC
// is a Phase-15 candidate.)
// =============================================================================
#pragma once

#include <cd/core/Result.hpp>
#include <cd/rhi/Enums.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace cd::rhi::d3d12
{

namespace shader_errors
{
inline constexpr std::uint32_t kDomain = 0x0017;

enum class Code : std::uint32_t
{
    kOk = 0,
    kUnsupportedStage = 1,         ///< Engine stage has no SM5/SM6 target.
    kCompileFailed = 2,            ///< Compiler returned an error blob.
    kBackendUnavailable = 3,       ///< Built on a non-Windows platform.
    kDxcUnavailable = 4,           ///< SM6 requested but dxcompiler.dll missing at runtime.
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace shader_errors

/// Shader Model selection (Phase 15.C). SM5_1 is the legacy D3DCompile
/// path that's been wired since Phase 14.C; SM6_0 / SM6_5 route through
/// DXC's IDxcCompiler3. SM6_3+ is required by the ray-tracing
/// raygen/closesthit/miss stages.
enum class ShaderModel : std::uint8_t
{
    kSM5_1,
    kSM6_0,
    kSM6_5,
};

struct CompileOptions
{
    /// HLSL source code (no length terminator required).
    std::string_view source;
    /// Entry point function name in the source.
    std::string_view entry_point { "main" };
    /// Stage to compile for. Drives the target profile selection.
    cd::rhi::ShaderStage stage { cd::rhi::ShaderStage::kVertex };
    /// Optional human-readable name for D3DCompile error messages.
    std::string_view source_name { "<inline>" };
    /// Optimization level. 0 = fastest compile, 3 = best codegen. Debug-
    /// time picks 0; release picks 3.
    std::uint32_t optimization_level { 0 };
    /// Shader Model target. SM5_1 = D3DCompile; SM6_x = DXC.
    ShaderModel model { ShaderModel::kSM5_1 };
};

/// Compile the HLSL source in `opts` into a DXIL bytecode blob. The
/// returned vector is the raw `ID3DBlob` payload — pass `.data()` and
/// `.size()` straight into `cd::rhi::ShaderModuleDesc`.
///
/// Failures:
///   * `kBackendUnavailable` — non-Windows compile target.
///   * `kUnsupportedStage` — stage has no SM5 target (e.g. RayGen).
///   * `kCompileFailed` — error log is in the ErrorCode's owning
///     message (always populated when this code is returned).
[[nodiscard]] cd::core::Result<std::vector<std::uint8_t>>
compile_hlsl(const CompileOptions& opts);

}  // namespace cd::rhi::d3d12
