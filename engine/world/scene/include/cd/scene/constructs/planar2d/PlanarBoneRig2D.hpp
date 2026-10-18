// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarBoneRig2D.hpp
// Standalone Concrete C++23 Construct Class #64: `cd::scene::PlanarBoneRig2D`
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

// [64] Skeleton2D -> PlanarBoneRig2D (09:42)
class PlanarBoneRig2D final
    : public ChromaConstructBase<64, ConstructCategory::Planar2D, ConstructRole::Element>
{
public:
    std::uint32_t ik_iterations { 8u };
    bool          fabrik_solver { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarBoneRig2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Skeleton2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "09:42"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarBoneRig2D + FabrikIK2D"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarBoneRig2D>(*this); }
};

} // namespace cd::scene
