// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/core/PlanarDrawElement.hpp
// Standalone Concrete C++23 Construct Class #10: `cd::scene::PlanarDrawElement`
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

// [10] CanvasItem -> PlanarDrawElement (38:44)
class PlanarDrawElement final
    : public ChromaConstructBase<10, ConstructCategory::CoreSystem, ConstructRole::Element>
{
public:
    std::int32_t sort_order      { 0 };
    bool         clip_children   { false };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarDrawElement"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "CanvasItem"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "38:44"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarDrawElement + DrawBatcher"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarDrawElement>(*this); }
};

} // namespace cd::scene
