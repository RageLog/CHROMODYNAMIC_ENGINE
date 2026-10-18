// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/core/NestedStagePortal.hpp
// Standalone Concrete C++23 Construct Class #2: `cd::scene::NestedStagePortal`
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

// [2] SubViewport -> NestedStagePortal (37:27)
class NestedStagePortal final
    : public ChromaConstructBase<2, ConstructCategory::CoreSystem, ConstructRole::Stage>
{
public:
    std::uint32_t bindless_srv_slot { 1u };
    float         resolution_scale  { 1.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "NestedStagePortal"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "SubViewport"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "37:27"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "NestedStagePortal + BindlessRenderTarget"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<NestedStagePortal>(*this); }
};

} // namespace cd::scene
