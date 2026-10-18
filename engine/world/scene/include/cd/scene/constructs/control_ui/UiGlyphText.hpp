// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiGlyphText.hpp
// Standalone Concrete C++23 Construct Class #177: `cd::scene::UiGlyphText`
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

// 177: Label -> UiGlyphText
class UiGlyphText final
    : public ChromaConstructBase<177, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    std::string text          { "CHROMODYNAMIC" };
    float       font_scale    { 1.0F };
    bool        autowrap_text { false };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiGlyphText"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Label"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "28:09"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiGlyphText + TrueTypeAtlas + UTF8"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiGlyphText>(*this); }
};

} // namespace cd::scene
