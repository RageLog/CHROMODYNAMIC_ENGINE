// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiShaderGraphBlock.hpp
// Standalone Concrete C++23 Construct Class #216: `cd::scene::UiShaderGraphBlock`
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

// 216: GraphNode -> UiShaderGraphBlock
class UiShaderGraphBlock final
    : public ChromaConstructBase<216, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    std::string          block_title { "TurquinPBRMaster" };
    std::array<float, 2> canvas_pos  { 120.0F, 80.0F };
    bool                 selected    { false };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiShaderGraphBlock"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "GraphNode"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "32:29"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiShaderGraphBlock + DraggableHeader"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiShaderGraphBlock>(*this); }
};

} // namespace cd::scene
