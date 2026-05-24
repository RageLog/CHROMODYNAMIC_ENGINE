// =============================================================================
// CHROMODYNAMIC — cd/anim/BoneSocket.hpp
// Phase 91.A / Wave 259 — named attachment slot on a skeleton.
//
// A "socket" is a designer-placed transform anchored to a bone:
// "WeaponR_Slot" on the right hand, "Hat_Slot" on the head. The
// runtime composes `bone_world * socket_local` to find the slot's
// world-space transform every frame.
//
//   * `name` — string for editor; `hash` cached for fast lookup.
//   * `bone_index` — index into the Skeleton's bone array.
//   * `local_offset` — Transform relative to the bone.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Transform.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cd::anim
{

[[nodiscard]] constexpr std::uint32_t socket_hash(std::string_view s) noexcept
{
    std::uint32_t h = 0x811C9DC5u;
    for (char c : s)
    {
        h ^= static_cast<std::uint32_t>(static_cast<unsigned char>(c));
        h *= 0x01000193u;
    }
    return h;
}

struct BoneSocket
{
    std::string             name;
    std::uint32_t           hash         { 0 };
    std::uint32_t           bone_index   { 0 };
    cd::math::Transformf    local_offset {};
};

class BoneSocketSet
{
public:
    void add(std::string name, std::uint32_t bone_index,
             const cd::math::Transformf& local_offset = {})
    {
        BoneSocket s;
        s.hash = socket_hash(name);
        s.bone_index = bone_index;
        s.local_offset = local_offset;
        s.name = std::move(name);
        sockets_.push_back(std::move(s));
    }

    [[nodiscard]] const BoneSocket* find(std::string_view name) const noexcept
    {
        const auto h = socket_hash(name);
        for (const auto& s : sockets_)
            if (s.hash == h) return &s;
        return nullptr;
    }

    [[nodiscard]] std::size_t size() const noexcept { return sockets_.size(); }

    [[nodiscard]] const std::vector<BoneSocket>& all() const noexcept { return sockets_; }

    void clear() noexcept { sockets_.clear(); }

private:
    std::vector<BoneSocket> sockets_;
};

}  // namespace cd::anim
