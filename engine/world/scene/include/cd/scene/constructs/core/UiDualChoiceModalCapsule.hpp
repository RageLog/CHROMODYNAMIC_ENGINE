// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/core/UiDualChoiceModalCapsule.hpp
// Standalone Concrete C++23 Construct Class #5: `cd::scene::UiDualChoiceModalCapsule`
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

// [5] ConfirmationDialog -> UiDualChoiceModalCapsule (37:56)
class UiDualChoiceModalCapsule final
    : public ChromaConstructBase<5, ConstructCategory::CoreSystem, ConstructRole::TraitCapsule>
{
public:
    bool  destructive_confirm { false };
    float spring_scale_in     { 1.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiDualChoiceModalCapsule"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "ConfirmationDialog"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "37:56"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiDualChoiceModalCapsule + Pulse<bool>"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiDualChoiceModalCapsule>(*this); }
};

} // namespace cd::scene
