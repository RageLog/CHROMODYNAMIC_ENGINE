// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiHyperlinkAction.hpp
// Standalone Concrete C++23 Construct Class #190: `cd::scene::UiHyperlinkAction`
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

// 190: LinkButton -> UiHyperlinkAction
class UiHyperlinkAction final
    : public ChromaConstructBase<190, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    std::string target_uri      { "cd://docs/ADR/index.md" };
    bool        underline_hover { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiHyperlinkAction"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "LinkButton"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "29:48"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiHyperlinkAction + AssetRouter"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiHyperlinkAction>(*this); }
};

} // namespace cd::scene
