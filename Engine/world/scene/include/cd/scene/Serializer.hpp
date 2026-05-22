// =============================================================================
// CHROMODYNAMIC — cd/scene/Serializer.hpp
//
// Scene ↔ JSON serializer. Walks every entity that carries a
// LocalTransform (the marker that "this entity is a scene node") and
// emits an object per node with translation, rotation, scale, optional
// parent reference.
//
// Output format (version 1):
//
// {
//   "version": 1,
//   "nodes": [
//     {
//       "id": 1,                // arbitrary stable id within this dump
//       "translation": [x, y, z],
//       "rotation":    [x, y, z, w],
//       "scale":       [x, y, z],
//       "parent": 0              // omitted if root
//     },
//     ...
//   ]
// }
//
// The `id` field is the Entity::id we read off the ECS at serialize time
// — same scene serialized in the same world produces the same ids
// (deterministic). After a round-trip into a fresh world the ids may
// differ because the new world starts assigning slots from 0; the
// deserializer therefore returns an `IdMap` so callers can re-wire any
// external references that pointed at the old ids.
//
// Header-only because the API is tiny and lives at the boundary
// between cd::scene and cd::asset_json — no good place to put a .cpp.
// =============================================================================
#pragma once

#include <cd/asset_json/Json.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/ecs/Entity.hpp>
#include <cd/ecs/World.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Transform.hpp>
#include <cd/math/Vector.hpp>
#include <cd/scene/Scene.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cd::scene
{

namespace serializer_errors
{
inline constexpr std::uint32_t kDomain = 0x0016;

enum class Code : std::uint32_t
{
    kOk = 0,
    kBadShape = 1,    ///< JSON root is not the expected object/array shape.
    kBadVersion = 2,  ///< version field unknown.
    kInvalidArgument = 3,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace serializer_errors

inline constexpr std::uint32_t kSceneJsonVersion = 1;

/// Convert a Vec3f to a JSON 3-element array.
[[nodiscard]] inline cd::asset_json::Value vec3_to_json(const cd::math::Vec3f& v)
{
    cd::asset_json::Array a;
    a.reserve(3);
    a.push_back(cd::asset_json::Value { static_cast<double>(v.x) });
    a.push_back(cd::asset_json::Value { static_cast<double>(v.y) });
    a.push_back(cd::asset_json::Value { static_cast<double>(v.z) });
    return cd::asset_json::Value { std::move(a) };
}

/// Convert a Quatf to a JSON 4-element array (x,y,z,w).
[[nodiscard]] inline cd::asset_json::Value quat_to_json(const cd::math::Quatf& q)
{
    cd::asset_json::Array a;
    a.reserve(4);
    a.push_back(cd::asset_json::Value { static_cast<double>(q.x) });
    a.push_back(cd::asset_json::Value { static_cast<double>(q.y) });
    a.push_back(cd::asset_json::Value { static_cast<double>(q.z) });
    a.push_back(cd::asset_json::Value { static_cast<double>(q.w) });
    return cd::asset_json::Value { std::move(a) };
}

/// Read a numeric JSON array of length `N` into a Vec or Quat. Returns
/// kBadShape on mismatch.
[[nodiscard]] inline cd::core::Result<cd::math::Vec3f> json_to_vec3(const cd::asset_json::Value& v)
{
    if (!v.is_array() || v.as_array().size() != 3)
        return std::unexpected(serializer_errors::make(serializer_errors::Code::kBadShape, "vec3 array shape"));
    const auto& a = v.as_array();
    if (!a[0].is_number() || !a[1].is_number() || !a[2].is_number())
        return std::unexpected(serializer_errors::make(serializer_errors::Code::kBadShape, "vec3 numeric"));
    return cd::math::Vec3f {
        static_cast<float>(a[0].as_number()),
        static_cast<float>(a[1].as_number()),
        static_cast<float>(a[2].as_number()),
    };
}

[[nodiscard]] inline cd::core::Result<cd::math::Quatf> json_to_quat(const cd::asset_json::Value& v)
{
    if (!v.is_array() || v.as_array().size() != 4)
        return std::unexpected(serializer_errors::make(serializer_errors::Code::kBadShape, "quat array shape"));
    const auto& a = v.as_array();
    if (!a[0].is_number() || !a[1].is_number() || !a[2].is_number() || !a[3].is_number())
        return std::unexpected(serializer_errors::make(serializer_errors::Code::kBadShape, "quat numeric"));
    return cd::math::Quatf {
        static_cast<float>(a[0].as_number()),
        static_cast<float>(a[1].as_number()),
        static_cast<float>(a[2].as_number()),
        static_cast<float>(a[3].as_number()),
    };
}

/// Serialize every entity with a LocalTransform into a JSON Value.
[[nodiscard]] inline cd::asset_json::Value serialize_scene(const Scene& scene)
{
    cd::asset_json::Object root;
    root["version"] = cd::asset_json::Value { static_cast<int>(kSceneJsonVersion) };

    cd::asset_json::Array nodes;
    auto& w = const_cast<cd::ecs::World&>(scene.world());  // for_each<T> exposes both const & mut
    w.for_each<LocalTransform>(
        [&](cd::ecs::Entity e, LocalTransform& lt) {
            cd::asset_json::Object obj;
            obj["id"]          = cd::asset_json::Value { static_cast<std::int64_t>(e.id) };
            obj["translation"] = vec3_to_json(lt.value.position);
            obj["rotation"]    = quat_to_json(lt.value.rotation);
            obj["scale"]       = vec3_to_json(lt.value.scale);
            const auto parent = scene.parent_of(e);
            if (parent.is_valid())
                obj["parent"]  = cd::asset_json::Value { static_cast<std::int64_t>(parent.id) };
            nodes.push_back(cd::asset_json::Value { std::move(obj) });
        }
    );
    root["nodes"] = cd::asset_json::Value { std::move(nodes) };
    return cd::asset_json::Value { std::move(root) };
}

/// Mapping from JSON-id → newly created Entity in the target scene's
/// world. Useful when external references (asset ids, etc.) need to
/// rewire after a round-trip.
using IdMap = std::unordered_map<std::uint64_t, cd::ecs::Entity>;

/// Recreate scene nodes from `json`. Adds new entities to `scene` (does
/// not destroy existing ones). Returns the IdMap so the caller can
/// resolve `json["id"]` back to the new Entity.
[[nodiscard]] inline cd::core::Result<IdMap>
deserialize_scene(Scene& scene, const cd::asset_json::Value& json)
{
    if (!json.is_object())
        return std::unexpected(serializer_errors::make(serializer_errors::Code::kBadShape, "scene JSON must be an object"));

    const auto& obj = json.as_object();
    auto ver_it = obj.find("version");
    if (ver_it == obj.end() || !ver_it->second.is_number())
        return std::unexpected(serializer_errors::make(serializer_errors::Code::kBadShape, "missing version field"));
    const auto ver = static_cast<std::uint32_t>(ver_it->second.as_number());
    if (ver != kSceneJsonVersion)
        return std::unexpected(serializer_errors::make(serializer_errors::Code::kBadVersion, "unsupported scene JSON version"));

    auto nodes_it = obj.find("nodes");
    if (nodes_it == obj.end() || !nodes_it->second.is_array())
        return std::unexpected(serializer_errors::make(serializer_errors::Code::kBadShape, "missing 'nodes' array"));

    // First pass: create entities and remember the old → new mapping. We
    // can't apply Parent right away because a node may reference a parent
    // that hasn't been visited yet.
    IdMap id_map;
    id_map.reserve(nodes_it->second.as_array().size());
    struct PendingNode
    {
        cd::ecs::Entity entity;
        cd::math::Transformf local;
        std::uint64_t json_parent { 0 };
        bool has_parent { false };  ///< parent.id==0 is a valid root child, so we
                                    ///< can't use "json_parent==0" as a sentinel.
    };
    std::vector<PendingNode> pending;
    pending.reserve(nodes_it->second.as_array().size());

    for (const auto& node_val : nodes_it->second.as_array())
    {
        if (!node_val.is_object())
            return std::unexpected(serializer_errors::make(serializer_errors::Code::kBadShape, "node must be object"));
        const auto& node = node_val.as_object();
        auto id_it = node.find("id");
        if (id_it == node.end() || !id_it->second.is_number())
            return std::unexpected(serializer_errors::make(serializer_errors::Code::kBadShape, "node missing id"));
        const auto json_id = static_cast<std::uint64_t>(id_it->second.as_number());

        cd::math::Transformf xf;
        if (auto t = node.find("translation"); t != node.end())
        {
            auto v = json_to_vec3(t->second);
            if (!v.has_value())
                return std::unexpected(v.error());
            xf.position = *v;
        }
        if (auto r = node.find("rotation"); r != node.end())
        {
            auto q = json_to_quat(r->second);
            if (!q.has_value())
                return std::unexpected(q.error());
            xf.rotation = *q;
        }
        if (auto s = node.find("scale"); s != node.end())
        {
            auto v = json_to_vec3(s->second);
            if (!v.has_value())
                return std::unexpected(v.error());
            xf.scale = *v;
        }

        const auto ent = scene.create_node();
        scene.local(ent)->value = xf;
        id_map[json_id] = ent;

        PendingNode pn;
        pn.entity = ent;
        pn.local  = xf;
        if (auto p = node.find("parent"); p != node.end() && p->second.is_number())
        {
            pn.json_parent = static_cast<std::uint64_t>(p->second.as_number());
            pn.has_parent  = true;
        }
        pending.push_back(pn);
    }

    // Second pass: wire up parent links now that every node exists.
    for (const auto& pn : pending)
    {
        if (!pn.has_parent)
            continue;
        auto it = id_map.find(pn.json_parent);
        if (it == id_map.end())
            continue;  // dangling parent reference — silently leave as root
        scene.attach(pn.entity, it->second);
    }

    return id_map;
}

}  // namespace cd::scene
