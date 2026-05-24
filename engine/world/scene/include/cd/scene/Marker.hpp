// =============================================================================
// CHROMODYNAMIC — cd/scene/Marker.hpp
// Phase 59.B / Wave 227 — named transform anchor.
//
// `Marker` is a named (string + hash) transform a designer placed in
// the scene: spawn points, cutscene focal anchors, weapon attach slots
// on a skeleton.
//
// `MarkerSet` is a flat keyed collection. `find(name)` returns the
// Transform if present. The hash field lets serializers store the
// 32-bit hash instead of the full string for binary save format.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Transform.hpp>
#include <cd/scene/NameRegistry.hpp>   // for name_hash (FNV-1a 32)

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace cd::scene
{

struct Marker
{
    std::string               name;
    std::uint32_t             hash { 0 };
    cd::math::Transformf      transform {};
};

class MarkerSet
{
public:
    void add(std::string name, const cd::math::Transformf& t)
    {
        const auto h = name_hash(name);
        markers_[h] = Marker { std::move(name), h, t };
    }

    [[nodiscard]] const Marker* find(std::string_view name) const noexcept
    {
        const auto h = name_hash(name);
        auto it = markers_.find(h);
        return (it != markers_.end()) ? &it->second : nullptr;
    }

    [[nodiscard]] const Marker* find_by_hash(std::uint32_t h) const noexcept
    {
        auto it = markers_.find(h);
        return (it != markers_.end()) ? &it->second : nullptr;
    }

    void remove(std::string_view name)
    {
        markers_.erase(name_hash(name));
    }

    [[nodiscard]] std::size_t size() const noexcept { return markers_.size(); }

    [[nodiscard]] const std::unordered_map<std::uint32_t, Marker>& all() const noexcept
    {
        return markers_;
    }

private:
    std::unordered_map<std::uint32_t, Marker> markers_;
};

}  // namespace cd::scene
