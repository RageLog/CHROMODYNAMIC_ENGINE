// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/core/AsyncVfsPakStreamer.hpp
// Standalone Concrete C++23 Construct Class #18: `cd::scene::AsyncVfsPakStreamer`
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

// [18] ResourcePreloader -> AsyncVfsPakStreamer (40:10)
class AsyncVfsPakStreamer final
    : public ChromaConstructBase<18, ConstructCategory::CoreSystem, ConstructRole::TraitCapsule>
{
public:
    std::uint32_t staging_budget_mb { 256u };
    bool          async_transfer    { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "AsyncVfsPakStreamer"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "ResourcePreloader"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "40:10"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "AsyncVfsPakStreamer + LruStagingQueue"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<AsyncVfsPakStreamer>(*this); }
};

} // namespace cd::scene
