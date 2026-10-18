// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/OpenXrSpatialAnchor3D.hpp
// Standalone Concrete C++23 Construct Class #157: `cd::scene::OpenXrSpatialAnchor3D`
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

// 157: XRAnchor3D -> OpenXrSpatialAnchor3D
class OpenXrSpatialAnchor3D final
    : public ChromaConstructBase<157, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    std::string anchor_uuid          { "00000000-0000-0000-0000-000000000157" };
    bool        persist_to_cdscene   { true };
    bool        tracking_valid       { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "OpenXrSpatialAnchor3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "XRAnchor3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "25:08"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "OpenXrSpatialAnchor3D +PersistentUUID";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<OpenXrSpatialAnchor3D>(*this);
    }
};

} // namespace cd::scene
