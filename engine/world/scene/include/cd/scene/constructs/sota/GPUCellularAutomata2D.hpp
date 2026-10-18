// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/sota/GPUCellularAutomata2D.hpp
// Standalone Concrete C++23 Construct Class #246: `cd::scene::GPUCellularAutomata2D`
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

// 246: GPUCellularAutomata2D
class GPUCellularAutomata2D final
    : public ChromaConstructBase<246, ConstructCategory::NextGenSOTA, ConstructRole::Element>
{
public:
    std::array<std::uint32_t, 2> grid_resolution { 512u, 512u };
    std::uint32_t                steps_per_frame { 2u };
    bool                         extract_marching_squares { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "GPUCellularAutomata2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "GPUCellularAutomata2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "42:00+ (Beyond Godot SOTA)"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "GPUCellularAutomata2D + CheckerboardCompute"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<GPUCellularAutomata2D>(*this); }
};

} // namespace cd::scene
