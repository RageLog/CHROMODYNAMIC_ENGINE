// =============================================================================
// CHROMODYNAMIC — cd/asset/PathResolver.hpp
// Phase 65.B / Wave 233 — variable substitution in asset paths.
//
// Asset paths in scene files often reference engine-defined variables:
//
//   "{LEVEL}/textures/{REGION}.cdtex"
//   "{PROJECT}/shaders/{PASS}/standard.vert"
//
// `PathResolver` is a tiny `name → value` substitution map + an
// `expand(template)` function that replaces every `{NAME}` token.
// Unknown variables are left as `{NAME}` literally (defensive — caller
// catches at higher level).
//
// Stays in cd::asset because scene/asset loaders are the main client.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <string>
#include <string_view>
#include <unordered_map>

namespace cd::asset
{

class PathResolver
{
public:
    void set(std::string name, std::string value)
    {
        vars_[std::move(name)] = std::move(value);
    }

    [[nodiscard]] std::string expand(std::string_view tmpl) const
    {
        std::string out;
        out.reserve(tmpl.size());
        for (std::size_t i = 0; i < tmpl.size(); )
        {
            if (tmpl[i] == '{')
            {
                const auto close = tmpl.find('}', i + 1);
                if (close == std::string_view::npos)
                {
                    out.append(tmpl.substr(i));
                    break;
                }
                const auto name = tmpl.substr(i + 1, close - i - 1);
                auto it = vars_.find(std::string { name });
                if (it != vars_.end())
                    out.append(it->second);
                else
                    out.append(tmpl.substr(i, close - i + 1));   // keep "{NAME}"
                i = close + 1;
            }
            else
            {
                out.push_back(tmpl[i++]);
            }
        }
        return out;
    }

    [[nodiscard]] std::size_t variable_count() const noexcept { return vars_.size(); }

    void clear() noexcept { vars_.clear(); }

private:
    std::unordered_map<std::string, std::string> vars_;
};

}  // namespace cd::asset
