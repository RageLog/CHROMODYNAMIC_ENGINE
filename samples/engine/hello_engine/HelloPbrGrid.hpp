// =============================================================================
// CHROMODYNAMIC — samples/engine/hello_engine/HelloPbrGrid.hpp
//
// Phase 294 / Marathon Run 7 sub-N1F: 4x4 PBR sphere demo-grid spawn
// configuration extracted from hello_engine main.cpp.
//
// The grid lays out 16 chrome-albedo spheres in a column=metallic /
// row=roughness gradient (left col = chrome metal, right col =
// dielectric, top row = mirror, bottom row = matte). Each entry is a
// pure-data PbrGridSlot describing one sphere's world placement and
// material parameters. main.cpp consumes the span, constructs a
// SceneEntity per slot, sets its transform via the ECS scene
// transform component, and pushes it into its per-sample entity
// vector. Every other primitive in the scene goes through the same
// loop (same shader, same shadow path, same TLAS instance loop) so
// no separate grid-only render pipeline is needed.
//
// W8-AX numerics (sphere scale 0.80 / spacing 1.70 / y_base 0.95 /
// z=-4.5) are baked here so the visual remains pixel-identical
// across the N1F mechanical extract. The chrome albedo (0.95, 0.93,
// 0.88) carries forward the W8-AS lighting tweak.
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace cd::hello_engine
{

// ---- PbrGridSlot ----------------------------------------------------------
// One sphere's spawn record. `name` is a small std::string built at
// configure-time ("PBR M0R0".."PBR M3R3"); `position` is the world-
// space anchor; `scale` is the uniform sphere radius (0.80 = 1.6
// diameter); `tint` is the chrome albedo; `metallic` ramps from 1.0
// (left column) to 0.0 (right column); `roughness` ramps from 0.04
// (top mirror) to 1.0 (bottom matte).
struct PbrGridSlot
{
    std::string         name;
    cd::math::Vec3f     position;
    float               scale;
    cd::math::Vec3f     tint;
    float               metallic;
    float               roughness;
};

// ---- Grid layout constants ------------------------------------------------
// Matches the W8-AX visual lock: 4 columns x 4 rows, 1.70 m spacing,
// 0.80 m radius, 0.95 m baseline elevation, -4.5 m camera-side z so
// the bigger grid still fits the default FOV.
inline constexpr int   kPbrGridCols    = 4;
inline constexpr int   kPbrGridRows    = 4;
inline constexpr float kPbrGridSpacing = 1.70F;
inline constexpr float kPbrGridYBase   = 0.95F;
inline constexpr float kPbrGridZ       = -4.5F;
inline constexpr float kPbrGridScale   = 0.80F;
inline constexpr cd::math::Vec3f kPbrGridChromeAlbedo { 0.95F, 0.93F, 0.88F };

// ---- build_pbr_demo_grid --------------------------------------------------
// Compute the 16 PbrGridSlot records for the standard 4x4 metal/rough
// gradient grid used by hello_engine's PBR showcase. Returns by value;
// the array fits comfortably inside small-buffer optimization tiers.
// Pure function — no GPU resources, no scene mutation, no allocation
// beyond the std::array + per-slot std::string short-string buffer.
[[nodiscard]] inline std::array<PbrGridSlot, static_cast<std::size_t>(kPbrGridCols * kPbrGridRows)>
build_pbr_demo_grid() noexcept(false)
{
    std::array<PbrGridSlot, static_cast<std::size_t>(kPbrGridCols * kPbrGridRows)> slots {};
    std::size_t idx = 0;
    for (int row = 0; row < kPbrGridRows; ++row)
    {
        for (int col = 0; col < kPbrGridCols; ++col)
        {
            PbrGridSlot s;
            s.name = std::string { "PBR M" } + std::to_string(col) + "R" + std::to_string(row);
            s.scale = kPbrGridScale;
            s.tint = kPbrGridChromeAlbedo;
            s.metallic =
                1.0F - static_cast<float>(col) / static_cast<float>(kPbrGridCols - 1);
            s.roughness =
                0.04F + (1.0F - 0.04F) *
                            (static_cast<float>(row) /
                             static_cast<float>(kPbrGridRows - 1));
            const float x = (static_cast<float>(col) -
                             (static_cast<float>(kPbrGridCols - 1) * 0.5F)) *
                            kPbrGridSpacing;
            const float y = kPbrGridYBase +
                            static_cast<float>(row) * kPbrGridSpacing;
            s.position = { x, y, kPbrGridZ };
            slots[idx++] = std::move(s);
        }
    }
    return slots;
}

}  // namespace cd::hello_engine
