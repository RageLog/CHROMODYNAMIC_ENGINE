// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/core/AsyncRestFetchCapsule.hpp
// Standalone Concrete C++23 Construct Class #15: `cd::scene::AsyncRestFetchCapsule`
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

// [15] HTTPRequest -> AsyncRestFetchCapsule (39:47)
class AsyncRestFetchCapsule final
    : public ChromaConstructBase<15, ConstructCategory::CoreSystem, ConstructRole::TraitCapsule>
{
public:
    float         timeout_sec { 10.0F };
    std::uint32_t max_retries { 3u };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "AsyncRestFetchCapsule"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "HTTPRequest"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "39:47"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "AsyncRestFetchCapsule + JsonParser"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<AsyncRestFetchCapsule>(*this); }
};

} // namespace cd::scene
