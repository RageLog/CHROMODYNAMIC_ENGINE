// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarFrameHistoryTap2D.hpp
// Standalone Concrete C++23 Construct Class #70: `cd::scene::PlanarFrameHistoryTap2D`
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

// [70] BackBufferCopy -> PlanarFrameHistoryTap2D (10:27)
class PlanarFrameHistoryTap2D final
    : public ChromaConstructBase<70, ConstructCategory::Planar2D, ConstructRole::TraitCapsule>
{
public:
    std::uint32_t mip_level        { 1u };
    float         refraction_ior   { 1.33F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarFrameHistoryTap2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "BackBufferCopy"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "10:27"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarFrameHistoryTap2D + MipRefraction"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarFrameHistoryTap2D>(*this); }
};

} // namespace cd::scene
