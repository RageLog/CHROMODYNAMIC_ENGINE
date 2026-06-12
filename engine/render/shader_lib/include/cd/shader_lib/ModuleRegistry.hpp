// =============================================================================
// CHROMODYNAMIC — cd/shader_lib/ModuleRegistry.hpp
// phase1133 (SL-C step 2, ADR-20260612-shader-library-architecture):
// catalogue of the embedded GLSL shader modules + the IIncludeResolver
// that serves them to cd::shader::ICompiler.
//
// The authoritative sources are engine/render/shader_lib/shaders/modules/
// *.glsl; CMake embeds them at configure time (editing a module re-runs
// the configure step via CMAKE_CONFIGURE_DEPENDS). The library is
// self-contained: consumers link cd::shader_lib and never touch the
// filesystem, which keeps the catalogue usable as a standalone product.
// =============================================================================
#pragma once

#include <cd/shader/Compiler.hpp>

#include <optional>
#include <span>
#include <string_view>

namespace cd::shader_lib
{

/// One embedded module: canonical virtual path + full GLSL text.
struct ModuleDesc
{
    std::string_view virtual_path;  ///< e.g. "cd/shader_lib/brdf.glsl"
    std::string_view content;
};

/// Full catalogue, sorted by virtual_path.
[[nodiscard]] std::span<const ModuleDesc> modules() noexcept;

/// Lookup by canonical virtual path ("cd/shader_lib/brdf.glsl") or the
/// bare module name ("brdf.glsl"). Returns nullptr when unknown.
[[nodiscard]] const ModuleDesc* find_module(std::string_view requested) noexcept;

/// cd::shader::IIncludeResolver over the embedded catalogue. Stateless
/// and deterministic (ADR §2.2 contract); resolution ignores the
/// requester and the system/local distinction — module identity is the
/// path alone.
class ModuleResolver final : public cd::shader::IIncludeResolver
{
public:
    ModuleResolver() noexcept = default;

    [[nodiscard]] std::optional<Resolved> resolve(
        std::string_view requested,
        std::string_view requester,
        bool system_include) override;
};

}  // namespace cd::shader_lib
