// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/TileCanvas2DElement.hpp
// Standalone Concrete C++23 Construct Class #42: `cd::scene::TileCanvas2DElement`
// =============================================================================
#pragma once

#include <cd/scene/constructs/IChromaConstruct.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace cd::scene
{

// [42] TileMap -> TileCanvas2DElement (06:08)
class TileCanvas2DElement final
    : public ChromaConstructBase<42, ConstructCategory::Planar2D, ConstructRole::Element>
{
public:
    std::uint32_t cell_size_px       { 16u };
    bool          greedy_mesh_strips { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "TileCanvas2DElement"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "TileMap"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "06:08"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "TileCanvas2DElement + GreedyCollider"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<TileCanvas2DElement>(*this); }
};

} // namespace cd::scene
