// =============================================================================
// CHROMODYNAMIC — cd/render2d/Renderer2D.hpp
// FAZ 34 (ADR-20261111) — High-Level 2D Scene & Studio Viewport Renderer
// Orchestrating Backdrop, Grid, Tilemap, Dynamic Soft-Shadow Lighting, 64-Bit
// Sorted Sprites & Debug Overlays.
// =============================================================================
#pragma once

#include <cd/render2d/Camera2D.hpp>
#include <cd/render2d/FormatLoaders2D.hpp>
#include <cd/render2d/Lighting2D.hpp>
#include <cd/render2d/ParallaxLayer2D.hpp>
#include <cd/render2d/SortKey2D.hpp>
#include <cd/render2d/SpriteBatch2D.hpp>
#include <cd/render2d/Tilemap2D.hpp>

#include <span>

namespace cd::render2d
{

struct Render2DDebugOptions
{
    bool       show_grid { true };
    bool       show_camera_frame { true };
    bool       show_greedy_colliders { false };
    bool       show_light_gizmos { true };
    ColorRgba8 sky_top_color { 14U, 20U, 36U, 255U };
    ColorRgba8 sky_bottom_color { 28U, 42U, 72U, 255U };
};

class Renderer2D
{
public:
    void render_scene(
        Canvas2D&                        out_canvas,
        const ViewportRect2D&            viewport,
        const Camera2D&                  camera,
        const Tilemap2D&                 tilemap,
        const LightingEnvironment2D&     lighting,
        std::span<const Sprite2D>        sprites,
        std::span<const CollisionRect2D> debug_colliders,
        const Render2DDebugOptions&      options = {},
        std::span<const ParallaxLayer2D> parallax_layers = {}) const;
};

}  // namespace cd::render2d
