// =============================================================================
// CHROMODYNAMIC — cd/editor/ui/AssetPalette.hpp
// Production editor — drag-source asset palette (Lessons §P6).
//
// Palette = category-grouped entries; entries describe what gets
// spawned + a thumbnail handle. Editor's right panel iterates these
// and renders drag-source buttons; viewport drop site consumes the
// active entry id + raycast hit position to issue a SpawnCommand.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cd::editor::ui
{

enum class PaletteCategory : std::uint8_t
{
    kPrimitiveMesh = 0,
    kLight         = 1,
    kAudioSource   = 2,
    kScenePreset   = 3,
    kImportedAsset = 4,
};

struct PaletteEntry
{
    std::string     id;            ///< stable identifier, used in SpawnCommand
    std::string     display_name;  ///< shown on the drag button
    std::string     category_tag;  ///< secondary grouping inside the category
    PaletteCategory category { PaletteCategory::kPrimitiveMesh };
    /// Opaque handle into the thumbnail atlas; 0 = no thumbnail.
    std::uint32_t   thumbnail_id { 0 };
};

class PaletteRegistry
{
public:
    void add(PaletteEntry entry) { entries_.push_back(std::move(entry)); }

    /// Lookup by stable id; returns nullptr when missing.
    [[nodiscard]] const PaletteEntry* find(std::string_view id) const
    {
        for (const auto& e : entries_)
            if (e.id == id) return &e;
        return nullptr;
    }

    [[nodiscard]] std::vector<const PaletteEntry*>
    filter(PaletteCategory category) const
    {
        std::vector<const PaletteEntry*> out;
        for (const auto& e : entries_)
            if (e.category == category) out.push_back(&e);
        return out;
    }

    [[nodiscard]] const std::vector<PaletteEntry>& all() const noexcept
    {
        return entries_;
    }

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    /// Seed the default content-pack: 5 primitive meshes + 4 light
    /// types + 1 audio source + 1 PBR scene preset. Matches the
    /// lessons doc §P6 "default palette" recommendation.
    void seed_defaults()
    {
        add({ "mesh.cube",     "Cube",     "mesh",      PaletteCategory::kPrimitiveMesh });
        add({ "mesh.sphere",   "Sphere",   "mesh",      PaletteCategory::kPrimitiveMesh });
        add({ "mesh.cone",     "Cone",     "mesh",      PaletteCategory::kPrimitiveMesh });
        add({ "mesh.cylinder", "Cylinder", "mesh",      PaletteCategory::kPrimitiveMesh });
        add({ "mesh.torus",    "Torus",    "mesh",      PaletteCategory::kPrimitiveMesh });
        add({ "light.directional", "Directional Light", "light", PaletteCategory::kLight });
        add({ "light.point",       "Point Light",       "light", PaletteCategory::kLight });
        add({ "light.spot",        "Spot Light",        "light", PaletteCategory::kLight });
        add({ "light.rect_area",   "Rect Area Light",   "light", PaletteCategory::kLight });
        add({ "audio.source",  "Positional Audio", "audio", PaletteCategory::kAudioSource });
        add({ "scene.pbr_cornell", "PBR Cornell Box", "preset", PaletteCategory::kScenePreset });
    }

private:
    std::vector<PaletteEntry> entries_;
};

}  // namespace cd::editor::ui
