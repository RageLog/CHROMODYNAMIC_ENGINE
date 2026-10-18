// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/core/HudStageLayer.hpp
// Standalone Concrete C++23 Construct Class #11: `cd::scene::HudStageLayer`
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

// [11] CanvasLayer -> HudStageLayer (38:58)
class HudStageLayer final
    : public ChromaConstructBase<11, ConstructCategory::CoreSystem, ConstructRole::Stage>
{
public:
    std::int32_t layer_index             { 1 };
    bool         follow_viewport_camera  { false };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "HudStageLayer"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "CanvasLayer"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "38:58"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "HudStageLayer + WorldSpaceProjector"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<HudStageLayer>(*this); }
};

} // namespace cd::scene
