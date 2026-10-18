// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/core/ChromaElement.hpp
// Standalone Concrete C++23 Construct Class #0: `cd::scene::ChromaElement`
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

// [0] Node -> ChromaElement (02:06)
class ChromaElement final
    : public ChromaConstructBase<0, ConstructCategory::CoreSystem, ConstructRole::Element>
{
public:
    std::uint64_t entity_generation { 1u };
    bool          pulse_bus_enabled { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "ChromaElement"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Node"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "02:06"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "ChromaElement + Reflection + PulseBus"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<ChromaElement>(*this); }
};

} // namespace cd::scene
