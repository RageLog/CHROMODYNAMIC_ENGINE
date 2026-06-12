// =============================================================================
// CHROMODYNAMIC — cd/world_container/Level.hpp
// gap #18 foundation — Level = top-level streaming unit. Owns
// bounds + a layer set + sky / postfx selectors.
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>
#include <cd/world_container/Layer.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cd::world_container
{

struct LevelBounds
{
    cd::math::Vec3f min { -1000.0F, -100.0F, -1000.0F };
    cd::math::Vec3f max {  1000.0F,  100.0F,  1000.0F };
};

class Level
{
public:
    Level()
    {
        // Every level starts with a Default layer that's always
        // present. The editor can rename / delete it but never let
        // a level have zero layers.
        layers_.push_back(std::make_unique<Layer>("Default"));
    }
    explicit Level(std::string name) : Level()
    {
        name_ = std::move(name);
    }

    [[nodiscard]] std::string_view name() const noexcept { return name_; }
    void set_name(std::string n) { name_ = std::move(n); }

    [[nodiscard]] LevelBounds&       bounds()       noexcept { return bounds_; }
    [[nodiscard]] const LevelBounds& bounds() const noexcept { return bounds_; }

    [[nodiscard]] std::size_t layer_count() const noexcept { return layers_.size(); }

    Layer*       add_layer(std::string name)
    {
        layers_.push_back(std::make_unique<Layer>(std::move(name)));
        return layers_.back().get();
    }

    /// Erase by index. Returns false if i is out of range OR i is the
    /// last layer (a Level must always have at least one).
    bool remove_layer(std::size_t i)
    {
        if (i >= layers_.size() || layers_.size() <= 1) return false;
        layers_.erase(layers_.begin() + static_cast<std::ptrdiff_t>(i));
        return true;
    }

    Layer*       layer(std::size_t i)       noexcept { return i < layers_.size() ? layers_[i].get() : nullptr; }
    const Layer* layer(std::size_t i) const noexcept { return i < layers_.size() ? layers_[i].get() : nullptr; }

    /// Find a layer by name; nullptr if absent.
    [[nodiscard]] Layer* find_layer(std::string_view n) noexcept
    {
        for (auto& l : layers_)
            if (l && l->name() == n) return l.get();
        return nullptr;
    }

    /// phase1108: path (project-relative) to this level's entity
    /// payload — a .cdscene file in the cd::scene::Serializer format
    /// (the same family the hello_editor <-> hello_engine bridge
    /// reads). Empty = level has no baked content yet.
    [[nodiscard]] std::string_view scene_path() const noexcept { return scene_path_; }
    void set_scene_path(std::string p) { scene_path_ = std::move(p); }

    /// Index of the active (spawn-destination) layer. v1.6 editor
    /// surface uses this when the asset palette drops a new entity.
    [[nodiscard]] std::size_t active_layer() const noexcept { return active_layer_; }
    void set_active_layer(std::size_t i) noexcept
    {
        if (i < layers_.size()) active_layer_ = i;
    }

private:
    std::string                          name_         { "Untitled Level" };
    std::string                          scene_path_   {};
    LevelBounds                          bounds_       {};
    std::vector<std::unique_ptr<Layer>>  layers_;
    std::size_t                          active_layer_ { 0 };
};

}  // namespace cd::world_container
