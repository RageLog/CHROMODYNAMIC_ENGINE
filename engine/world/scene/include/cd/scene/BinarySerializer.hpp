// =============================================================================
// CHROMODYNAMIC — cd/scene/BinarySerializer.hpp
// Phase 5 / S4.7.b — fast binary scene format (.cdscene).
//
// Companion to the existing JSON serializer. Use case:
//   * JSON  → human-readable, version-friendly, git-diffable. Editor
//             save-as default.
//   * .cdscene → 5-10× smaller, 30-50× faster to load. Shipped game
//             data, save games, deterministic snapshots.
//
// Wire format (little-endian):
//   ┌──────────────── HEADER (16 B) ───────────────┐
//   │ magic[4]   "CDSN"                             │
//   │ version    u32 = 1                            │
//   │ node_count u32                                │
//   │ flags      u32 (reserved)                     │
//   └───────────────────────────────────────────────┘
//   Payload: node_count records, each:
//     u32 id              (Entity::id at serialize time)
//     u8  has_parent      (0 or 1)
//     u32 parent_id       (only if has_parent)
//     f32 translation[3]
//     f32 rotation[4]     (quaternion x,y,z,w)
//     f32 scale[3]
//   Total per-node bytes: 4 + 1 + (4) + 12 + 16 + 12 = 49 (with parent)
//   or 45 (without). Padding-free; reader does its own alignment.
//
// Header-only because the API is tiny and depends only on cd::scene +
// cd::ecs + cd::math.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/ecs/World.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Vector.hpp>
#include <cd/scene/Scene.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cd::scene
{

namespace bin_errors
{
inline constexpr std::uint32_t kDomain = 0x0017;

enum class Code : std::uint32_t
{
    kOk = 0,
    kMagicMismatch = 1,
    kVersionMismatch = 2,
    kCorrupt = 3,
    kInvalidArgument = 4,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace bin_errors

inline constexpr std::uint32_t kBinaryVersion = 1;
inline constexpr std::size_t kBinaryHeaderSize = 16;

namespace bin_detail
{

inline void put_u32(std::vector<std::byte>& out, std::uint32_t v)
{
    const std::uint8_t b[4] = { static_cast<std::uint8_t>(v & 0xFFu),
                                static_cast<std::uint8_t>((v >> 8u) & 0xFFu),
                                static_cast<std::uint8_t>((v >> 16u) & 0xFFu),
                                static_cast<std::uint8_t>((v >> 24u) & 0xFFu) };
    out.insert(out.end(),
               reinterpret_cast<const std::byte*>(b),
               reinterpret_cast<const std::byte*>(b) + 4);
}

inline void put_f32(std::vector<std::byte>& out, float v)
{
    std::uint32_t u = 0;
    std::memcpy(&u, &v, 4);
    put_u32(out, u);
}

[[nodiscard]] inline std::uint32_t read_u32(const std::byte* p) noexcept
{
    const auto* b = reinterpret_cast<const std::uint8_t*>(p);
    return static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8)
         | (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
}

[[nodiscard]] inline float read_f32(const std::byte* p) noexcept
{
    const auto u = read_u32(p);
    float f = 0.0F;
    std::memcpy(&f, &u, 4);
    return f;
}

}  // namespace bin_detail

/// Serialize every LocalTransform-bearing entity to a byte buffer in
/// the .cdscene wire format. Deterministic order: iterates the ECS in
/// stable component-storage order.
[[nodiscard]] inline std::vector<std::byte> serialize_scene_binary(const Scene& scene)
{
    // First pass: count and collect.
    struct NodeRec
    {
        std::uint32_t id;
        bool has_parent;
        std::uint32_t parent_id;
        cd::math::Vec3f translation;
        cd::math::Quatf rotation;
        cd::math::Vec3f scale;
    };
    std::vector<NodeRec> nodes;
    const auto& w = scene.world();
    w.for_each<LocalTransform>([&](cd::ecs::Entity e, const LocalTransform& lt) {
        NodeRec rec {};
        rec.id = e.id;
        const auto parent = scene.parent_of(e);
        rec.has_parent = parent.is_valid();
        rec.parent_id = parent.is_valid() ? parent.id : 0u;
        rec.translation = lt.value.position;
        rec.rotation = lt.value.rotation;
        rec.scale = lt.value.scale;
        nodes.push_back(rec);
    });

    std::vector<std::byte> out;
    out.reserve(kBinaryHeaderSize + nodes.size() * 50);

    // Header.
    constexpr char kMagic[4] = { 'C', 'D', 'S', 'N' };
    out.insert(out.end(),
               reinterpret_cast<const std::byte*>(kMagic),
               reinterpret_cast<const std::byte*>(kMagic) + 4);
    bin_detail::put_u32(out, kBinaryVersion);
    bin_detail::put_u32(out, static_cast<std::uint32_t>(nodes.size()));
    bin_detail::put_u32(out, 0u);  // flags reserved

    // Records.
    for (const auto& n : nodes)
    {
        bin_detail::put_u32(out, n.id);
        out.push_back(std::byte { static_cast<std::uint8_t>(n.has_parent ? 1u : 0u) });
        if (n.has_parent)
            bin_detail::put_u32(out, n.parent_id);
        bin_detail::put_f32(out, n.translation.x);
        bin_detail::put_f32(out, n.translation.y);
        bin_detail::put_f32(out, n.translation.z);
        bin_detail::put_f32(out, n.rotation.x);
        bin_detail::put_f32(out, n.rotation.y);
        bin_detail::put_f32(out, n.rotation.z);
        bin_detail::put_f32(out, n.rotation.w);
        bin_detail::put_f32(out, n.scale.x);
        bin_detail::put_f32(out, n.scale.y);
        bin_detail::put_f32(out, n.scale.z);
    }
    return out;
}

/// Mapping from .cdscene id → newly-created Entity in the target scene.
using BinaryIdMap = std::unordered_map<std::uint32_t, cd::ecs::Entity>;

[[nodiscard]] inline cd::core::Result<BinaryIdMap>
deserialize_scene_binary(Scene& scene, const std::byte* bytes, std::size_t size)
{
    if (bytes == nullptr || size < kBinaryHeaderSize)
        return std::unexpected(bin_errors::make(bin_errors::Code::kCorrupt, "buffer too small"));

    if (bytes[0] != std::byte { 'C' } || bytes[1] != std::byte { 'D' } ||
        bytes[2] != std::byte { 'S' } || bytes[3] != std::byte { 'N' })
    {
        return std::unexpected(bin_errors::make(bin_errors::Code::kMagicMismatch, "bad magic"));
    }
    const auto version = bin_detail::read_u32(bytes + 4);
    if (version != kBinaryVersion)
        return std::unexpected(bin_errors::make(bin_errors::Code::kVersionMismatch, "version"));
    const auto node_count = bin_detail::read_u32(bytes + 8);
    // flags at +12 reserved.

    std::size_t cursor = kBinaryHeaderSize;
    BinaryIdMap id_map;
    id_map.reserve(node_count);

    struct PendingNode
    {
        cd::ecs::Entity entity;
        bool has_parent;
        std::uint32_t parent_id;
    };
    std::vector<PendingNode> pending;
    pending.reserve(node_count);

    for (std::uint32_t i = 0; i < node_count; ++i)
    {
        if (cursor + 5 > size)
            return std::unexpected(bin_errors::make(bin_errors::Code::kCorrupt, "truncated record"));
        const auto id = bin_detail::read_u32(bytes + cursor);
        cursor += 4;
        const bool has_parent = bytes[cursor] != std::byte { 0 };
        cursor += 1;
        std::uint32_t parent_id = 0;
        if (has_parent)
        {
            if (cursor + 4 > size)
                return std::unexpected(bin_errors::make(bin_errors::Code::kCorrupt, "truncated parent"));
            parent_id = bin_detail::read_u32(bytes + cursor);
            cursor += 4;
        }
        const std::size_t payload = 40;  // 3 translation + 4 rotation + 3 scale floats
        if (cursor + payload > size)
            return std::unexpected(bin_errors::make(bin_errors::Code::kCorrupt, "truncated payload"));

        cd::math::Transformf xf;
        xf.position.x = bin_detail::read_f32(bytes + cursor + 0);
        xf.position.y = bin_detail::read_f32(bytes + cursor + 4);
        xf.position.z = bin_detail::read_f32(bytes + cursor + 8);
        xf.rotation.x = bin_detail::read_f32(bytes + cursor + 12);
        xf.rotation.y = bin_detail::read_f32(bytes + cursor + 16);
        xf.rotation.z = bin_detail::read_f32(bytes + cursor + 20);
        xf.rotation.w = bin_detail::read_f32(bytes + cursor + 24);
        xf.scale.x = bin_detail::read_f32(bytes + cursor + 28);
        xf.scale.y = bin_detail::read_f32(bytes + cursor + 32);
        xf.scale.z = bin_detail::read_f32(bytes + cursor + 36);
        cursor += payload;

        const auto ent = scene.create_node();
        scene.local(ent)->value = xf;
        id_map[id] = ent;
        pending.push_back({ ent, has_parent, parent_id });
    }

    // Wire parents in a second pass.
    for (const auto& pn : pending)
    {
        if (!pn.has_parent)
            continue;
        auto it = id_map.find(pn.parent_id);
        if (it == id_map.end())
            continue;
        scene.attach(pn.entity, it->second);
    }
    return id_map;
}

}  // namespace cd::scene
