// =============================================================================
// CHROMODYNAMIC — cd/physics/ContactPoint.hpp
// Phase 87.B / Wave 255 — collision manifold contact-point struct.
//
// When two primitives intersect, the solver receives one or more
// contact points describing where + how deep the penetration is:
//
//   * `world_position` — contact in world space.
//   * `normal`         — surface normal from B's perspective (pointing
//                        out of B, towards A).
//   * `penetration`    — depth (meters, ≥ 0).
//   * `tangent_friction` — optional store for solver friction state.
//
// `ContactManifold` is a tiny fixed-array of up to 4 points — enough
// for most box-box / box-sphere contacts.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <array>
#include <cstddef>

namespace cd::physics
{

struct ContactPoint
{
    cd::math::Vec3f world_position    {};
    cd::math::Vec3f normal            { 0.0F, 1.0F, 0.0F };
    float           penetration       { 0.0F };
    cd::math::Vec3f tangent_friction  {};
};

class ContactManifold
{
public:
    static constexpr std::size_t kCapacity = 4;

    bool add(const ContactPoint& p) noexcept
    {
        if (count_ >= kCapacity) return false;
        points_[count_++] = p;
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] bool        empty() const noexcept { return count_ == 0; }

    [[nodiscard]] const ContactPoint& at(std::size_t i) const noexcept
    {
        return points_[i];
    }

    void clear() noexcept { count_ = 0; }

private:
    std::array<ContactPoint, kCapacity> points_ {};
    std::size_t                         count_ { 0 };
};

}  // namespace cd::physics
