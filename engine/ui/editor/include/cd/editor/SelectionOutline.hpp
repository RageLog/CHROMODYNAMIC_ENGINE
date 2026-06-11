// =============================================================================
// CHROMODYNAMIC — cd/editor/SelectionOutline.hpp
// Phase 115 / Wave 283 — selection-outline state primitive.
//
// Visual "this entity is selected" feedback comes in three families:
//   * Stencil-based outline (write mask in selection pass, scale + dark
//     tint in post pass).
//   * Jump-flooding distance outline (compute pass with mip pyramid).
//   * Wireframe / bounding-box overlay (simplest, cheapest).
//
// All three need the same upstream state: a set of selected entities +
// per-frame visual params (color, thickness, opacity). This header is
// the engine-side data type that drives the renderer-side outline pass
// (which lands phase by phase as each technique is added).
//
// Storage: one `SelectionOutline` instance lives next to the editor's
// EditHistory + SelectionSet. The renderer reads it once per frame
// and pumps the corresponding pass.
//
// Header-only because it's pure data + a couple of helpers.
// =============================================================================
#pragma once

#include <algorithm>
#include <cd/core/Defines.hpp>
#include <cd/ecs/Entity.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace cd::editor
{

/// Visual style of the outline pass. Each style maps to a different
/// renderer-side implementation, but they share the same state.
enum class OutlineStyle : std::uint8_t
{
    kNone        = 0,  ///< no outline drawn this frame
    kWireframe   = 1,  ///< wireframe / bounding-box overlay (cheapest)
    kStencilEdge = 2,  ///< stencil-driven 1-pixel edge (mid cost)
    kJumpFlood   = 3,  ///< distance-field outline w/ thickness (highest cost)
};

class SelectionOutline
{
public:
    SelectionOutline() noexcept = default;

    void set(cd::ecs::Entity e) noexcept
    {
        selected_.clear();
        if (e.is_valid()) selected_.push_back(e);
    }

    void add(cd::ecs::Entity e)
    {
        if (!e.is_valid()) return;
        if (std::ranges::find(selected_, e) == selected_.end())
            selected_.push_back(e);
    }

    void remove(cd::ecs::Entity e) noexcept
    {
        selected_.erase(std::ranges::remove(selected_, e).begin(), selected_.end());
    }

    void clear() noexcept { selected_.clear(); }

    [[nodiscard]] bool contains(cd::ecs::Entity e) const noexcept
    {
        for (const auto& s : selected_) if (s == e) return true;
        return false;
    }

    [[nodiscard]] const std::vector<cd::ecs::Entity>& entities() const noexcept { return selected_; }
    [[nodiscard]] std::size_t size() const noexcept { return selected_.size(); }
    [[nodiscard]] bool empty() const noexcept { return selected_.empty(); }

    // ------ Visual parameters (treat as plain config) -----------------------
    OutlineStyle    style     { OutlineStyle::kStencilEdge };
    cd::math::Vec3f color     { 1.0F, 0.55F, 0.05F };  ///< warm orange (editor default)
    float           thickness { 1.5F };                 ///< pixels (jump-flood) or
                                                         ///< scale factor (stencil)
    float           opacity   { 1.0F };

    /// Clamp the visual parameters into reasonable ranges. Callers
    /// driving the values from UI sliders can run this once per frame.
    void clamp_params() noexcept
    {
        thickness = std::max(thickness, 0.0F);
        thickness = std::min(thickness, 16.0F);
        opacity = std::max(opacity, 0.0F);
        opacity = std::min(opacity, 1.0F);
    }

private:
    std::vector<cd::ecs::Entity> selected_;
};

}  // namespace cd::editor
