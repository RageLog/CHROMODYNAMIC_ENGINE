// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiBindlessSpriteQuad.hpp
// Standalone Concrete C++23 Construct Class #180: `cd::scene::UiBindlessSpriteQuad`
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

// 180: TextureRect -> UiBindlessSpriteQuad
class UiBindlessSpriteQuad final
    : public ChromaConstructBase<180, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    std::uint32_t texture_slot { 0u };
    float         uv_zoom      { 1.0F };
    bool          keep_aspect  { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiBindlessSpriteQuad"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "TextureRect"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "28:30"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiBindlessSpriteQuad + MultiAtlasSlot"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiBindlessSpriteQuad>(*this); }
};

} // namespace cd::scene
