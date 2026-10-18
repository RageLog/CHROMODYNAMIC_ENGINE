// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/core/UiModalConfirmCapsule.hpp
// Standalone Concrete C++23 Construct Class #4: `cd::scene::UiModalConfirmCapsule`
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

// [4] AcceptDialog -> UiModalConfirmCapsule (37:56)
class UiModalConfirmCapsule final
    : public ChromaConstructBase<4, ConstructCategory::CoreSystem, ConstructRole::TraitCapsule>
{
public:
    std::string dialog_title      { "Confirm Action" };
    float       backdrop_blur_px  { 12.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiModalConfirmCapsule"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "AcceptDialog"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "37:56"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiModalConfirmCapsule + BackdropBlur"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiModalConfirmCapsule>(*this); }
};

} // namespace cd::scene
