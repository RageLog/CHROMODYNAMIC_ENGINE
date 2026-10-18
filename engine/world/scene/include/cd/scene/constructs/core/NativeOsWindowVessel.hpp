// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/core/NativeOsWindowVessel.hpp
// Standalone Concrete C++23 Construct Class #3: `cd::scene::NativeOsWindowVessel`
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

// [3] Window -> NativeOsWindowVessel (37:41)
class NativeOsWindowVessel final
    : public ChromaConstructBase<3, ConstructCategory::CoreSystem, ConstructRole::Stage>
{
public:
    float dpi_scale  { 1.0F };
    bool  borderless { false };
    bool  vsync      { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "NativeOsWindowVessel"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Window"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "37:41"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "NativeOsWindowVessel + SwapchainFence"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<NativeOsWindowVessel>(*this); }
};

} // namespace cd::scene
