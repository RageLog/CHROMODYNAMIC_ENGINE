// =============================================================================
// CHROMODYNAMIC — cd/shader/Compiler.hpp
// Sprint S3.7 — shader compiler abstraction.
//
// ICompiler is the engine's source-language-agnostic compilation surface. The
// default backend is glslang (Khronos reference compiler, supports GLSL +
// HLSL → SPIR-V). Slang (Khronos's blessed successor as of Nov 2024) plugs
// into the same interface in a future sprint without touching consumers.
//
// Design constraints:
//   * No global state visible to callers. Compilers may need a one-shot
//     `glslang::InitializeProcess()` style boot; that's an implementation
//     detail handled in the compiler's ctor/dtor.
//   * Thread-safety: each `ICompiler` instance is single-threaded. Engines
//     should create one per worker thread or guard a shared one externally.
//   * Hot-path errors are reported via `cd::core::Result`. No exceptions.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cd::shader
{

// ---- Error domain -----------------------------------------------------------

namespace shader_errors
{
inline constexpr std::uint32_t kDomain = 0x000A;

enum class Code : std::uint32_t
{
    kOk = 0,
    kCompileFailed = 1,    ///< Front-end parse error (syntax, undeclared identifier, …).
    kLinkFailed = 2,       ///< Cross-stage link / SPIR-V emission failed.
    kInvalidArgument = 3,  ///< Caller-side mistake (empty source, unknown stage).
    kInitFailed = 4,       ///< Backend boot failed (glslang::InitializeProcess etc).
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}

/// Owning-message variant for diagnostics built at run time (e.g. glslang
/// InfoLog concatenation). Allocates a shared_ptr so the message view
/// stays valid after the caller's local std::string goes out of scope.
[[nodiscard]] inline cd::core::ErrorCode make_owning(Code c, std::string m)
{
    return cd::core::ErrorCode::make_owning(kDomain, static_cast<std::uint32_t>(c), std::move(m));
}
}  // namespace shader_errors

// ---- Shader description ----------------------------------------------------

/// Pipeline stage the source applies to. Mirrors `cd::rhi::ShaderStage` but is
/// duplicated here so the shader library does not depend on the RHI public
/// header (keeps the dependency DAG strictly: rhi can use shader, not the
/// other way around).
enum class ShaderStage : std::uint8_t
{
    kVertex,
    kFragment,
    kCompute,
    kGeometry,
    kTessControl,
    kTessEval,
    // Phase 136 — ray-tracing pipeline stages.
    kRaygen,
    kMiss,
    kClosestHit,
    kAnyHit,
    kIntersection,
    kCallable,
    // Phase 765 W2A — F5 — mesh-shader pipeline stages.
    // Compiles via glslang's EShLangMesh / EShLangTask entry points (the
    // Vulkan-style GL_EXT_mesh_shader spelling, identical SPIR-V output as
    // GL_NV_mesh_shader once glslang lowers it).
    kMesh,
    kTask,
};

/// Source language at the compiler input. Vulkan SPIR-V is always the output.
enum class ShaderLanguage : std::uint8_t
{
    kGlsl,  ///< OpenGL Shading Language — glslang's native input.
    kHlsl,  ///< DirectX HLSL — glslang's HLSL frontend handles SM 5.x style.
};

/// Targeted Vulkan dialect for SPIR-V emission. The engine baseline is 1.3,
/// matching the RHI's Vulkan backend; 1.4 will be opt-in once we wire the
/// matching device feature flag.
enum class TargetEnv : std::uint8_t
{
    kVulkan12,
    kVulkan13,
};

struct CompileDesc
{
    /// Source text. Must be non-empty.
    std::string_view source {};
    /// Stage the source compiles for.
    ShaderStage stage { ShaderStage::kVertex };
    /// Input source language.
    ShaderLanguage lang { ShaderLanguage::kGlsl };
    /// SPIR-V target environment. Defaults to the engine's RHI baseline.
    TargetEnv target { TargetEnv::kVulkan13 };
    /// Entry-point function name. GLSL ignores this (always "main"); HLSL needs
    /// the actual entry point because a single file may host many.
    std::string_view entry_point { "main" };
    /// Logical name used in compiler diagnostics (e.g. "triangle.vert"). Does
    /// not need to be a real filesystem path.
    std::string_view source_name { "<inline>" };
    /// Emit SPIR-V with `OpSource`/`OpLine` debug info. Off by default to keep
    /// release binaries lean.
    bool generate_debug_info { false };
};

struct CompileResult
{
    /// 32-bit-word SPIR-V module, ready to feed into `cd::rhi::create_shader_module`.
    std::vector<std::uint32_t> spirv {};
    /// Compiler warnings (newline-separated). Empty on a perfectly clean build.
    std::string warnings {};
};

// ---- Interface --------------------------------------------------------------

class ICompiler
{
public:
    ICompiler() noexcept = default;
    virtual ~ICompiler() = default;
    ICompiler(const ICompiler&) = delete;
    ICompiler& operator=(const ICompiler&) = delete;
    ICompiler(ICompiler&&) = delete;
    ICompiler& operator=(ICompiler&&) = delete;

    /// Compile a single shader source into SPIR-V. Errors carry the front-end
    /// diagnostic text in `ErrorCode::message` so callers can surface it.
    [[nodiscard]] virtual cd::core::Result<CompileResult> compile(const CompileDesc& desc) = 0;
};

// ---- Factories --------------------------------------------------------------

/// Build a glslang-backed compiler. Returns nullptr when the engine was built
/// without the glslang backend (CD_ENABLE_GLSLANG=OFF) — callers can detect
/// this and fall back to file-based SPIR-V loading.
[[nodiscard]] std::unique_ptr<ICompiler> make_glslang_compiler();

/// Build a Slang-backed compiler. Slang is Khronos's blessed successor to
/// glslang (Nov 2024); same `ICompiler` interface so callers can A/B test
/// or migrate by swapping the factory call. Returns nullptr when built
/// without CD_ENABLE_SLANG. Slang exclusively accepts its own language and
/// HLSL; passing `ShaderLanguage::kGlsl` returns kInvalidArgument.
[[nodiscard]] std::unique_ptr<ICompiler> make_slang_compiler();

// ---- File helpers (always available) ---------------------------------------

/// Load a pre-compiled SPIR-V module from disk. The file must be a sequence
/// of 32-bit words (output of `glslc -o foo.spv` or `glslangValidator -V`).
[[nodiscard]] cd::core::Result<std::vector<std::uint32_t>> load_spirv_file(std::string_view path);

}  // namespace cd::shader
