// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiCanvasPinItem.hpp
// Standalone Concrete C++23 Construct Class #215: `cd::scene::UiCanvasPinItem`
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

// 215: GraphElement -> UiCanvasPinItem
class UiCanvasPinItem final
    : public ChromaConstructBase<215, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    std::uint32_t pin_id       { 1u };
    bool          is_output    { false };
    std::string   pin_type_tag { "vec4" };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiCanvasPinItem"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "GraphElement"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "32:29"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiCanvasPinItem + TypedSockets"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiCanvasPinItem>(*this); }
};

} // namespace cd::scene
