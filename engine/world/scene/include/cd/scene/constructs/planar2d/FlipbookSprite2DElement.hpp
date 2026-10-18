// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/FlipbookSprite2DElement.hpp
// Standalone Concrete C++23 Construct Class #24: `cd::scene::FlipbookSprite2DElement`
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

// [24] AnimatedSprite2D -> FlipbookSprite2DElement (02:52)
class FlipbookSprite2DElement final
    : public ChromaConstructBase<24, ConstructCategory::Planar2D, ConstructRole::Element>
{
public:
    float         fps          { 12.0F };
    std::uint32_t frame_count  { 8u };
    std::uint32_t active_frame { 0u };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "FlipbookSprite2DElement"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "AnimatedSprite2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "02:52"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "FlipbookSprite2DElement + UvFlipbook"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<FlipbookSprite2DElement>(*this); }
};

} // namespace cd::scene
