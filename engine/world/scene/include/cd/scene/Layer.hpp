// =============================================================================
// CHROMODYNAMIC — cd/scene/Layer.hpp
// Phase 69.B / Wave 237 — named scene layer registry.
//
// Scene layers ("Background", "Geometry", "Foreground", "UI", "Debug")
// give the renderer a higher-level coarse grouping above VisibilityMask
// (Phase 38, which is a fine-grain channel bitmask).
//
// LayerRegistry maps layer names to dense uint8 indices. Each entity
// optionally stores a `LayerIndex` component; the renderer iterates
// layers in registry order for deterministic draw ordering at the
// layer level.
//
// Pure data; iteration order = insertion order.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cd::scene
{

using LayerIndex = std::uint8_t;
inline constexpr LayerIndex kInvalidLayer = 0xFF;

class LayerRegistry
{
public:
    LayerIndex add(std::string name)
    {
        const LayerIndex idx = static_cast<LayerIndex>(names_.size());
        names_.push_back(std::move(name));
        return idx;
    }

    [[nodiscard]] LayerIndex find(std::string_view name) const noexcept
    {
        for (std::size_t i = 0; i < names_.size(); ++i)
            if (names_[i] == name) return static_cast<LayerIndex>(i);
        return kInvalidLayer;
    }

    [[nodiscard]] std::string_view name(LayerIndex idx) const noexcept
    {
        return (idx < names_.size()) ? std::string_view { names_[idx] } : std::string_view {};
    }

    [[nodiscard]] std::size_t size() const noexcept { return names_.size(); }

    [[nodiscard]] const std::vector<std::string>& all() const noexcept { return names_; }

    void clear() noexcept { names_.clear(); }

private:
    std::vector<std::string> names_;
};

}  // namespace cd::scene
