// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_material_preview/src/MaterialPreview.cpp
//
// phase678 — cd::editor::panel::material_preview implementation.
// phase738 — Sprint-2: real PBR sphere via apps/editor RT callback.
//
// Sphere swatch now branches on preview_texture_:
//   * Valid handle  → emit a DrawBatcher::textured_quad over the sphere area.
//                     The lower 32 bits of the handle's raw value are forwarded
//                     as the texture_slot (same convention as
//                     cd::editor::panel::viewport::Viewport and the
//                     debug_viz overlays).
//   * Null handle   → keep the Sprint-1 dark background + base_color tint
//                     overlay so headless tests + boot frames stay legible.
//
// Cache invalidation:
//   compute_hash_() folds base_color (3 channels), metallic, roughness,
//   alpha_mode, and alpha_cutoff into a single 64-bit fingerprint via the
//   FNV-1a body of every IEEE-754 float bit-pattern. set_material() resets
//   has_rendered_ so the host pays the next PBR render unconditionally;
//   draw() additionally flips has_rendered_ off whenever the live hash
//   drifts from the last clear_dirty() snapshot (catches in-place mutation
//   of the AuthoredMaterial backing struct).
// =============================================================================
#include <cd/editor/panel_material_preview/MaterialPreview.hpp>

#include <cd/material/AlphaMode.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

namespace cd::editor::panel::material_preview
{

namespace
{

// FNV-1a 64-bit constants — standard, dependency-free fingerprint primitive.
inline constexpr std::uint64_t kFnvOffsetBasis = 0xCBF29CE484222325ULL;
inline constexpr std::uint64_t kFnvPrime       = 0x100000001B3ULL;

[[nodiscard]] inline std::uint64_t hash_fold_u32(std::uint64_t acc,
                                                 std::uint32_t v) noexcept
{
    // Byte-wise FNV-1a fold (little-endian byte order; deterministic).
    for (int i = 0; i < 4; ++i)
    {
        const auto byte = static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFU);
        acc ^= static_cast<std::uint64_t>(byte);
        acc *= kFnvPrime;
    }
    return acc;
}

[[nodiscard]] inline std::uint64_t hash_fold_float(std::uint64_t acc,
                                                   float v) noexcept
{
    // Treat NaN as zero so two different NaN bit patterns don't desync the
    // hash for materials that compare equal under the artist's intent.
    if (std::isnan(v))
    {
        return hash_fold_u32(acc, 0U);
    }
    return hash_fold_u32(acc, std::bit_cast<std::uint32_t>(v));
}

}  // namespace

// ---------------------------------------------------------------------------
// State API
// ---------------------------------------------------------------------------

void MaterialPreview::set_material(
    const cd::asset::material_authoring::AuthoredMaterial* mat) noexcept
{
    mat_ = mat;
    // Force a re-render on the next is_dirty() poll: the host has to refresh
    // the RT before clear_dirty() resumes returning false.
    has_rendered_ = false;
}

const cd::asset::material_authoring::AuthoredMaterial*
MaterialPreview::material() const noexcept
{
    return mat_;
}

// ---------------------------------------------------------------------------
// RT cache API (phase738)
// ---------------------------------------------------------------------------

void MaterialPreview::set_preview_texture(cd::rhi::TextureHandle handle) noexcept
{
    preview_texture_ = handle;
}

cd::rhi::TextureHandle MaterialPreview::preview_texture() const noexcept
{
    return preview_texture_;
}

bool MaterialPreview::is_dirty() const noexcept
{
    // No material bound → nothing to render; do not strand the host in a
    // permanent dirty state.
    if (mat_ == nullptr)
    {
        return false;
    }
    // First-ever poll → host must pay the initial PBR render.
    if (!has_rendered_)
    {
        return true;
    }
    // Otherwise compare fingerprints; any drift flips us dirty.
    return compute_hash_() != last_rendered_hash_;
}

void MaterialPreview::clear_dirty() noexcept
{
    if (mat_ == nullptr)
    {
        // No bound material — snapshot stays zero and has_rendered_ stays
        // false so the next set_material() starts from a clean baseline.
        return;
    }
    last_rendered_hash_ = compute_hash_();
    has_rendered_       = true;
}

std::uint64_t MaterialPreview::compute_hash_() const noexcept
{
    if (mat_ == nullptr) { return 0U; }

    std::uint64_t acc = kFnvOffsetBasis;
    acc = hash_fold_float(acc, mat_->base_color[0]);
    acc = hash_fold_float(acc, mat_->base_color[1]);
    acc = hash_fold_float(acc, mat_->base_color[2]);
    acc = hash_fold_float(acc, mat_->metallic);
    acc = hash_fold_float(acc, mat_->roughness);
    acc = hash_fold_u32(acc,   static_cast<std::uint32_t>(mat_->alpha_mode));
    acc = hash_fold_float(acc, mat_->alpha_cutoff);
    return acc;
}

// ---------------------------------------------------------------------------
// DrawBatcher path
// ---------------------------------------------------------------------------

void MaterialPreview::draw(cd::ui::renderer::DrawBatcher& batcher,
                           const cd::ui::widgets::Theme&  theme,
                           const cd::ui::widgets::Rect&   bounds) const
{
    // ---- 1. Background fill -------------------------------------------------
    batcher.quad(bounds.x, bounds.y, bounds.w, bounds.h,
                 cd::ui::renderer::Color {
                     theme.surface.r,
                     theme.surface.g,
                     theme.surface.b,
                     theme.surface.a });

    if (!bounds.is_valid())
        return;

    constexpr float kPad  = 6.0F;
    constexpr float kBarH = 4.0F;
    const float     row_w = bounds.w - 2.0F * kPad;

    // ---- 2. Accent separator bar --------------------------------------------
    batcher.quad(bounds.x + kPad, bounds.y + kPad,
                 row_w, kBarH,
                 cd::ui::renderer::Color {
                     theme.accent.r,
                     theme.accent.g,
                     theme.accent.b,
                     theme.accent.a });

    float cursor_y = bounds.y + kPad * 2.0F + kBarH;

    // Early out if no material is bound.
    if (mat_ == nullptr)
        return;

    // ---- Drift detection (phase738) ----------------------------------------
    // If the live AuthoredMaterial's hash drifted from the last clear_dirty()
    // ack we flip has_rendered_ off so the next is_dirty() poll catches the
    // change. This handles in-place mutation of the backing struct (the
    // inspector edits a slider directly on *mat_).
    if (has_rendered_ && compute_hash_() != last_rendered_hash_)
    {
        has_rendered_ = false;
    }

    // ---- Helpers ------------------------------------------------------------

    // Convert a normalised float [0..1] to a uint8 channel.
    auto to_u8 = [](float v) -> std::uint8_t
    {
        const long rounded = std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F);
        return static_cast<std::uint8_t>(std::clamp(rounded, 0L, 255L));
    };

    constexpr float kRowH = 20.0F;

    // Draw a labelled fill-bar row (track + fill).
    //   cursor_y is advanced by (kRowH + kPad) after the call.
    auto draw_fill_bar = [&](float value,
                             cd::ui::renderer::Color fill_col)
    {
        // Label accent pip on the left.
        batcher.quad(bounds.x + kPad, cursor_y,
                     6.0F, kRowH,
                     cd::ui::renderer::Color {
                         theme.accent.r,
                         theme.accent.g,
                         theme.accent.b,
                         200U });

        const float track_x = bounds.x + kPad + 6.0F + kPad * 0.5F;
        const float track_w = row_w - 6.0F - kPad;

        // Track background.
        batcher.quad(track_x, cursor_y,
                     track_w, kRowH,
                     cd::ui::renderer::Color {
                         theme.surface_hover.r,
                         theme.surface_hover.g,
                         theme.surface_hover.b,
                         theme.surface_hover.a });

        // Fill (non-zero values only).
        const float fill_w = track_w * std::clamp(value, 0.0F, 1.0F);
        if (fill_w > 0.0F)
        {
            batcher.quad(track_x, cursor_y,
                         fill_w, kRowH,
                         fill_col);
        }

        cursor_y += kRowH + kPad;
    };

    // ---- 3. Sphere swatch ---------------------------------------------------
    // phase738: when a valid preview RT handle is bound, sample it via a
    // textured_quad — that's the apps/editor-rendered PBR sphere shaded with
    // the AuthoredMaterial's metallic + roughness + base_color factors.
    // Null handle = Sprint-1 base_color-tinted fallback (headless / boot-frame).
    constexpr float kSphereH = 80.0F;
    const float     sphere_x = bounds.x + kPad;
    const float     sphere_w = row_w;

    if (preview_texture_.is_valid() && sphere_w > 0.0F && kSphereH > 0.0F)
    {
        // PBR RT path — sample the 256x256 sphere RT covering the full swatch
        // area (UV 0..1). Tint stays white so the RT's PBR shading reaches
        // the user 1:1.
        const auto tex_slot =
            static_cast<std::uint32_t>(preview_texture_.value() & 0xFFFFFFFFU);
        static constexpr cd::ui::renderer::AtlasUv kFullUv {
            0.0F, 0.0F, 1.0F, 1.0F
        };
        batcher.textured_quad(sphere_x, cursor_y,
                              sphere_w, kSphereH,
                              tex_slot, kFullUv,
                              cd::ui::renderer::Color { 255U, 255U, 255U, 255U });
    }
    else
    {
        // Sprint-1 fallback ---------------------------------------------------
        // Dark background so even a pure-black tint is distinguishable.
        batcher.quad(sphere_x, cursor_y,
                     sphere_w, kSphereH,
                     cd::ui::renderer::Color {
                         theme.background.r,
                         theme.background.g,
                         theme.background.b,
                         theme.background.a });

        // Tint overlay at alpha=210 — the darker background bleeds through to
        // give a crude sense of the sphere "edge" fade.
        const std::uint8_t tint_r = to_u8(mat_->base_color[0]);
        const std::uint8_t tint_g = to_u8(mat_->base_color[1]);
        const std::uint8_t tint_b = to_u8(mat_->base_color[2]);
        batcher.quad(sphere_x, cursor_y,
                     sphere_w, kSphereH,
                     cd::ui::renderer::Color { tint_r, tint_g, tint_b, 210U });
    }

    cursor_y += kSphereH + kPad;

    // ---- 4. Metallic bar (accent_success tint) -------------------------------
    // accent_success: green-ish — full bar = fully metallic (conductor).
    draw_fill_bar(mat_->metallic,
                  cd::ui::renderer::Color { 80U, 200U, 120U, 220U });

    // ---- 5. Roughness bar (accent_warning tint) ------------------------------
    // accent_warning: amber-ish — full bar = maximally rough (Lambertian).
    draw_fill_bar(mat_->roughness,
                  cd::ui::renderer::Color { 220U, 160U, 60U, 220U });

    // ---- 6. Alpha cutoff bar (error tint — kMask only) ----------------------
    if (mat_->alpha_mode == cd::material::AlphaMode::kMask)
    {
        // Error tint: red-ish — conveys "fragments below this threshold are cut".
        draw_fill_bar(mat_->alpha_cutoff,
                      cd::ui::renderer::Color { 210U, 60U, 60U, 220U });
    }

    // ---- 7. Texture path pills ----------------------------------------------
    // Each path is shown as a thin quad: present = accent colour, absent = grey.
    constexpr float kPillH  = 10.0F;
    constexpr float kPillGap = 4.0F;

    auto draw_path_pill = [&](bool present)
    {
        const cd::ui::renderer::Color col = present
            ? cd::ui::renderer::Color { theme.accent.r, theme.accent.g,
                                        theme.accent.b, 180U }
            : cd::ui::renderer::Color { 90U, 90U, 90U, 120U };

        batcher.quad(bounds.x + kPad, cursor_y, row_w, kPillH, col);
        cursor_y += kPillH + kPillGap;
    };

    draw_path_pill(!mat_->albedo_texture_path.empty());
    draw_path_pill(!mat_->normal_texture_path.empty());
    draw_path_pill(!mat_->mr_texture_path.empty());
}

}  // namespace cd::editor::panel::material_preview
