// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiMultilineDocumentBox.hpp
// Standalone Concrete C++23 Construct Class #198: `cd::scene::UiMultilineDocumentBox`
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

// 198: TextEdit -> UiMultilineDocumentBox
class UiMultilineDocumentBox final
    : public ChromaConstructBase<198, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    std::string   document_text    { "" };
    std::uint32_t visible_line_cap { 128u };
    bool          read_only        { false };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiMultilineDocumentBox"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "TextEdit"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "30:38"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiMultilineDocumentBox + VirtualizedLines"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiMultilineDocumentBox>(*this); }
};

} // namespace cd::scene
