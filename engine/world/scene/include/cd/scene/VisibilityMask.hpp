// =============================================================================
// CHROMODYNAMIC — cd/scene/VisibilityMask.hpp
// Phase 37.B / Wave 205 — per-entity visibility bits.
//
// A 32-bit bitmask identifying which "visibility channels" an entity
// participates in. The renderer's camera carries a complementary mask;
// the entity is drawn iff `(entity.mask & camera.mask) != 0`.
//
// Use cases:
//   * Editor-only gizmos (mask bit 31) hidden from main viewport.
//   * Shadow-pass-only proxies (mask bit 0 = main, 1 = shadow).
//   * Multi-camera setups (security camera vs main camera).
//
// Bits 0..7 are reserved for engine-defined channels (see below).
// Bits 8..31 are caller-defined. Default mask `kAll` participates in
// every channel so legacy entities just work.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>

namespace cd::scene
{

struct VisibilityMask
{
    std::uint32_t bits { 0xFFFFFFFFu };  // kAll

    friend constexpr bool operator==(VisibilityMask, VisibilityMask) noexcept = default;
};

inline constexpr std::uint32_t kVisAll          = 0xFFFFFFFFu;
inline constexpr std::uint32_t kVisNone         = 0u;
inline constexpr std::uint32_t kVisMainCamera   = 1u << 0;
inline constexpr std::uint32_t kVisShadowPass   = 1u << 1;
inline constexpr std::uint32_t kVisReflection   = 1u << 2;
inline constexpr std::uint32_t kVisEditorGizmo  = 1u << 31;

[[nodiscard]] constexpr bool is_visible(VisibilityMask entity, VisibilityMask camera) noexcept
{
    return (entity.bits & camera.bits) != 0u;
}

[[nodiscard]] constexpr VisibilityMask with_bit(VisibilityMask m, std::uint32_t bit) noexcept
{
    return VisibilityMask { m.bits | bit };
}

[[nodiscard]] constexpr VisibilityMask without_bit(VisibilityMask m, std::uint32_t bit) noexcept
{
    return VisibilityMask { m.bits & ~bit };
}

}  // namespace cd::scene
