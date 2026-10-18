// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiGridMatrixContainer.hpp
// Standalone Concrete C++23 Construct Class #169: `cd::scene::UiGridMatrixContainer`
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

// 169: GridContainer -> UiGridMatrixContainer
class UiGridMatrixContainer final
    : public ChromaConstructBase<169, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    std::uint16_t columns      { 3u };
    float         cell_gap_px  { 6.0F };
    bool          stretch_same { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiGridMatrixContainer"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "GridContainer"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "26:35"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiGridMatrixContainer + TableCells"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiGridMatrixContainer>(*this); }
};

} // namespace cd::scene
