// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/QuadSprite2DElement.hpp
// Standalone Concrete C++23 Construct Class #23: `cd::scene::QuadSprite2DElement`
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

// [23] Sprite2D -> QuadSprite2DElement (02:52)
class QuadSprite2DElement final
    : public ChromaConstructBase<23, ConstructCategory::Planar2D, ConstructRole::Element>
{
public:
    std::uint32_t bindless_texture_slot { 2u };
    bool          flip_h                { false };
    bool          flip_v                { false };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "QuadSprite2DElement"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Sprite2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "02:52"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "QuadSprite2DElement + BindlessAtlas"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<QuadSprite2DElement>(*this); }
};

} // namespace cd::scene
