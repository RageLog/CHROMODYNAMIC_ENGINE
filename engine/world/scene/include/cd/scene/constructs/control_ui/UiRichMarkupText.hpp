// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiRichMarkupText.hpp
// Standalone Concrete C++23 Construct Class #178: `cd::scene::UiRichMarkupText`
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

// 178: RichTextLabel -> UiRichMarkupText
class UiRichMarkupText final
    : public ChromaConstructBase<178, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    std::string markup_text    { "[b]ChromaUI[/b] Rich Markup" };
    float       visible_ratio  { 1.0F };
    bool        enable_badges  { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiRichMarkupText"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "RichTextLabel"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "28:15"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiRichMarkupText + InlineBadges"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiRichMarkupText>(*this); }
};

} // namespace cd::scene
