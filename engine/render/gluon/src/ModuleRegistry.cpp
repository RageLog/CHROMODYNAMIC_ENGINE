// =============================================================================
// CHROMODYNAMIC — cd/gluon/ModuleRegistry.cpp
// =============================================================================
#include <cd/gluon/ModuleRegistry.hpp>

// Generated at configure time from shaders/modules/*.glsl — defines
// cd::gluon::detail::kEmbeddedModules (sorted by virtual_path).
#include <gluon_embedded.hpp>

#include <string>

namespace cd::gluon
{

std::span<const ModuleDesc> modules() noexcept
{
    return detail::kEmbeddedModules;
}

const ModuleDesc* find_module(std::string_view requested) noexcept
{
    constexpr std::string_view kPrefix { "cd/gluon/" };
    for (const auto& m : detail::kEmbeddedModules)
    {
        if (m.virtual_path == requested)
            return &m;
        // bare-name form: "brdf.glsl" matches "cd/gluon/brdf.glsl"
        if (m.virtual_path.size() == kPrefix.size() + requested.size() &&
            m.virtual_path.substr(kPrefix.size()) == requested)
            return &m;
    }
    return nullptr;
}

std::optional<cd::shader::IIncludeResolver::Resolved>
ModuleResolver::resolve(std::string_view requested,
                        std::string_view /*requester*/,
                        bool /*system_include*/)
{
    const auto* m = find_module(requested);
    if (m == nullptr)
        return std::nullopt;
    return Resolved { std::string { m->virtual_path },
                      std::string { m->content } };
}

}  // namespace cd::gluon
