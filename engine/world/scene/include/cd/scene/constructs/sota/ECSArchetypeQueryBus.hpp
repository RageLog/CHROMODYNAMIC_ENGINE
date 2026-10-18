// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/sota/ECSArchetypeQueryBus.hpp
// Standalone Concrete C++23 Construct Class #249: `cd::scene::ECSArchetypeQueryBus`
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

// 249: ECSArchetypeQueryBus
class ECSArchetypeQueryBus final
    : public ChromaConstructBase<249, ConstructCategory::NextGenSOTA, ConstructRole::PulseBus>
{
public:
    std::uint32_t chunk_capacity_entities { 1024u };
    bool          parallel_work_stealing  { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "ECSArchetypeQueryBus"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "ECSArchetypeQueryBus"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "42:00+ (Beyond Godot SOTA)"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "ECSArchetypeQueryBus + ParallelSoADispatcher"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<ECSArchetypeQueryBus>(*this); }
};

} // namespace cd::scene
