// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiIconCommandButton.hpp
// Standalone Concrete C++23 Construct Class #189: `cd::scene::UiIconCommandButton`
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

// 189: TextureButton -> UiIconCommandButton
class UiIconCommandButton final
    : public ChromaConstructBase<189, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    std::uint8_t vector_icon_id { 0u };
    float        icon_size_px   { 18.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiIconCommandButton"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "TextureButton"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "29:35"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiIconCommandButton + 14VectorIcons"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiIconCommandButton>(*this); }
};

} // namespace cd::scene
