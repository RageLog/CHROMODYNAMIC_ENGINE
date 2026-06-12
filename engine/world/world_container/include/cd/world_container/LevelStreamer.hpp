// =============================================================================
// CHROMODYNAMIC — cd/world_container/LevelStreamer.hpp
//
// phase1114 — v1.7 streaming, first shippable slice. The streamer owns
// the RESIDENCY of level content: activate(level) unloads the previous
// level's entities and loads the new level's scene file, while
// entities on a PERSISTENT layer (Layer::persistent — player, UI,
// persistent NPCs) survive the switch. This is the level-switching
// half of streaming; distance-based chunk streaming builds on the same
// tracking later.
//
// Contract:
//   * The streamer tracks ONLY the entities it loaded (plus survivors
//     it re-adopted across switches). Entities the caller spawned
//     outside activate() are never touched.
//   * Persistence is evaluated against the OUTGOING level's layer set:
//     an entity survives iff its LayerMember names a layer that level
//     marks persistent. Everything else is destroy_node()'d (children
//     cascade through the scene graph).
//   * Scene files are the cd::scene::Serializer JSON family — the same
//     .cdscene the editor bridge writes. The caller's ReadExtras hook
//     receives every loaded entity for sample-side metadata; the
//     streamer itself restores LayerMember from the "layer" field so
//     persistence keeps working without caller cooperation.
// =============================================================================
#pragma once

#include <cd/asset/json/Json.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/scene/Scene.hpp>
#include <cd/scene/Serializer.hpp>
#include <cd/world_container/LayerMember.hpp>
#include <cd/world_container/Project.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cd::world_container
{

namespace level_streamer_errors
{
inline constexpr std::uint32_t kDomain = 0x001A;

enum class Code : std::uint32_t
{
    kOk = 0,
    kBadLevel = 1,    ///< level index out of range or empty scene_path.
    kIoFailure = 2,   ///< scene file missing/unreadable.
    kBadScene = 3,    ///< scene json failed to parse/deserialize.
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace level_streamer_errors

class LevelStreamer
{
public:
    LevelStreamer(cd::ecs::World& world, cd::scene::Scene& scene) noexcept
        : world_ { &world }
        , scene_ { &scene }
    {
    }

    [[nodiscard]] std::size_t active_index() const noexcept { return active_; }
    [[nodiscard]] bool has_active() const noexcept { return active_valid_; }
    [[nodiscard]] std::size_t tracked_count() const noexcept { return tracked_.size(); }

    /// Switch residency to `level_idx` of `proj`. `root` resolves the
    /// level's project-relative scene_path. `extras` is invoked as
    /// void(Entity, const cd::asset::json::Object&) for every loaded
    /// entity (sample metadata hook); pass a no-op lambda when unused.
    template <class ReadExtras>
    [[nodiscard]] cd::core::Result<void>
    activate(const Project& proj, std::size_t level_idx,
             const std::filesystem::path& root, ReadExtras&& extras)
    {
        using namespace level_streamer_errors;
        const Level* next = proj.level(level_idx);
        if (next == nullptr || next->scene_path().empty())
            return std::unexpected(make(Code::kBadLevel,
                                        "level missing or has no scene_path"));

        // ---- unload the outgoing level ----------------------------------
        // Survivors = tracked entities whose LayerMember names a layer
        // the OUTGOING level marks persistent.
        std::vector<cd::ecs::Entity> survivors;
        if (active_valid_)
        {
            std::unordered_set<std::string> persistent;
            if (const Level* prev = proj.level(active_); prev != nullptr)
            {
                for (std::size_t i = 0; i < prev->layer_count(); ++i)
                {
                    const Layer* lay = prev->layer(i);
                    if (lay != nullptr && lay->persistent())
                        persistent.emplace(lay->name());
                }
            }
            for (const auto e : tracked_)
            {
                if (scene_->local(e) == nullptr)
                    continue;  // already gone (caller deleted it)
                const auto lay = layer_of(*world_, e);
                if (persistent.contains(std::string { lay }))
                    survivors.push_back(e);
                else
                    scene_->destroy_node(e);
            }
        }
        tracked_ = std::move(survivors);

        // ---- load the incoming level -------------------------------------
        const auto path = root / std::filesystem::path {
            std::string { next->scene_path() } };
        auto json = cd::asset::json::load(path.string());
        if (!json.has_value())
            return std::unexpected(make(Code::kIoFailure,
                                        "scene file missing/unreadable"));
        auto loaded = cd::scene::deserialize_scene_with(
            *scene_, *json,
            [&](cd::ecs::Entity e, const cd::asset::json::Object& obj)
            {
                tracked_.push_back(e);
                // Restore membership so the NEXT switch can evaluate
                // persistence without caller cooperation.
                if (auto it = obj.find("layer");
                    it != obj.end() && it->second.is_string() &&
                    it->second.as_string() != kDefaultLayerName)
                {
                    assign_layer(*world_, e, it->second.as_string());
                }
                extras(e, obj);
            });
        if (!loaded.has_value())
            return std::unexpected(make(Code::kBadScene,
                                        "scene deserialize failed"));

        active_       = level_idx;
        active_valid_ = true;
        return {};
    }

    /// Unload everything non-persistent and forget the active level.
    void deactivate(const Project& proj)
    {
        if (!active_valid_) return;
        std::unordered_set<std::string> persistent;
        if (const Level* prev = proj.level(active_); prev != nullptr)
        {
            for (std::size_t i = 0; i < prev->layer_count(); ++i)
            {
                const Layer* lay = prev->layer(i);
                if (lay != nullptr && lay->persistent())
                    persistent.emplace(lay->name());
            }
        }
        std::vector<cd::ecs::Entity> survivors;
        for (const auto e : tracked_)
        {
            if (scene_->local(e) == nullptr) continue;
            if (persistent.contains(std::string { layer_of(*world_, e) }))
                survivors.push_back(e);
            else
                scene_->destroy_node(e);
        }
        tracked_      = std::move(survivors);
        active_valid_ = false;
    }

private:
    cd::ecs::World*               world_;
    cd::scene::Scene*             scene_;
    std::vector<cd::ecs::Entity>  tracked_;
    std::size_t                   active_ { 0 };
    bool                          active_valid_ { false };
};

}  // namespace cd::world_container
