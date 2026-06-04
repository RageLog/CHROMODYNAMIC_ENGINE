// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_material_preview/MaterialPreview.hpp
//
// phase678 — cd::editor::panel::material_preview  (panel_material_preview library)
//
// Sprint-1 material preview panel: provides a DrawBatcher-based live preview of
// an AuthoredMaterial for the DockSpace shell (apps/editor).
//
// phase738 — Sprint-2: real PBR sphere via render-target callback API.
//   * set_preview_texture(handle) — bind a 256x256 RT the apps/editor host
//     just rendered into using cd::material's PBR variant (sphere primitive
//     shaded with the AuthoredMaterial's metallic + roughness + base_color
//     factors). When the handle is valid the panel emits a
//     DrawBatcher::textured_quad over the sphere swatch area (256x256
//     bounded to the panel's available row width) — TRUE WYSIWYG.
//   * is_dirty() / clear_dirty() — cache invalidation contract. The panel
//     fingerprints (base_color + metallic + roughness + alpha_mode +
//     alpha_cutoff) on every set_material() call and on every draw() pass
//     where the bound material's hash drifted. apps/editor polls is_dirty()
//     each frame and re-renders the RT only when set, then calls
//     clear_dirty() to ack the refresh.
//   * Null texture handle = fall back to the Sprint-1 base_color-tinted
//     2D swatch so headless tests + boot-frame paths stay legible.
//
// Renders (2D projection, no GPU pipeline required by default):
//
//   * Panel background fill.
//   * Separator bar under the title area (accent colour).
//   * 2D-projected sphere: a circular gradient quad tinted by base_color
//     (Sprint-1 fallback) OR a textured_quad sampling the bound 256x256
//     PBR RT (Sprint-2, phase738).
//   * Metallic bar (accent_success tint — full bar = fully metallic).
//   * Roughness bar (accent_warning tint — full bar = maximally rough).
//   * Alpha cutoff row (shown only when alpha_mode == kMask).
//   * Texture path list: albedo / normal / MR paths listed as pill rows.
//     Empty paths render as greyed-out "—" placeholders.
//
// State API:
//   set_material(const AuthoredMaterial*)  — bind/update the preview target.
//                                            Pass nullptr to detach.
//                                            Marks the cache dirty.
//   material() const                       — returns the bound pointer (may be
//                                            nullptr).
//
// RT cache API (phase738):
//   set_preview_texture(handle)            — bind a live cd::rhi::TextureHandle
//                                            (256x256 RT painted by apps/editor
//                                            using the cd::material PBR variant).
//                                            Pass a null handle to fall back to
//                                            the Sprint-1 2D swatch.
//   preview_texture() const                — returns the currently bound handle.
//   is_dirty() const                       — true when the bound material's
//                                            fingerprint differs from the last
//                                            clear_dirty() snapshot; apps/editor
//                                            uses this as the "needs re-render"
//                                            signal.
//   clear_dirty()                          — snapshot the bound material's
//                                            fingerprint as "rendered into RT
//                                            successfully". Called by apps/editor
//                                            after kicking the PBR draw.
//
// Lifetime contract:
//   MaterialPreview is default-constructible and holds a non-owning raw pointer.
//   The caller must ensure the AuthoredMaterial outlives the panel, or call
//   set_material(nullptr) before the material is destroyed.
//
// MOMENT: A material artist tweaks `metallic` in the inspector. The panel's
// fingerprint hash drifts on the very next draw(), is_dirty() flips true,
// apps/editor re-renders the 256x256 PBR sphere into the cached RT, calls
// clear_dirty(), and the next frame the preview swatch shows the new metallic
// response — true WYSIWYG inside the editor. No engine rebuild, no asset
// pipeline round-trip.
// =============================================================================
#pragma once

#include <cd/asset/material_authoring/MaterialAuthoring.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <cstdint>

namespace cd::editor::panel::material_preview
{

// ---------------------------------------------------------------------------
// Constants (phase738)
// ---------------------------------------------------------------------------

/// Canonical preview render-target size in pixels (single-frame, square).
/// apps/editor allocates a 256x256 cd::rhi::TextureHandle of this size and
/// renders the cd::material PBR sphere into it every time
/// MaterialPreview::is_dirty() returns true.
inline constexpr std::uint32_t kPreviewRtSize = 256U;

// ---------------------------------------------------------------------------
// MaterialPreview
// ---------------------------------------------------------------------------
class MaterialPreview
{
public:
    // Default-constructible; starts with no bound material (nullptr).
    MaterialPreview() noexcept = default;

    // ---- State API ----------------------------------------------------------

    /// Bind to an AuthoredMaterial. The panel holds a non-owning pointer;
    /// pass nullptr to detach. The next draw() call will reflect the new
    /// material state. Marks the RT cache dirty so apps/editor's next-frame
    /// poll triggers a fresh PBR render into the bound texture.
    void set_material(const cd::asset::material_authoring::AuthoredMaterial* mat) noexcept;

    /// Returns the currently bound AuthoredMaterial pointer (nullptr if none).
    [[nodiscard]] const cd::asset::material_authoring::AuthoredMaterial*
    material() const noexcept;

    // ---- RT cache API (phase738) -------------------------------------------

    /// Bind the live cd::rhi::TextureHandle the apps/editor host just rendered
    /// the PBR sphere into. Pass a default-constructed (null) handle to clear
    /// the binding and fall back to the Sprint-1 base_color-tinted swatch.
    ///
    /// The handle's lower 32 bits are forwarded to DrawBatcher::textured_quad
    /// as the texture_slot — same convention as cd::editor::panel::viewport
    /// and cd::editor::debug_viz::DebugVizOverlay.
    void set_preview_texture(cd::rhi::TextureHandle handle) noexcept;

    /// Returns the currently bound preview RT handle (null when none was set).
    [[nodiscard]] cd::rhi::TextureHandle preview_texture() const noexcept;

    /// True when the bound material's PBR-relevant fingerprint
    /// (base_color + metallic + roughness + alpha_mode + alpha_cutoff)
    /// differs from the last clear_dirty() snapshot. apps/editor polls this
    /// per-frame and re-renders the 256x256 RT only when set.
    ///
    /// Also true while no clear_dirty() has ever been issued (first-frame
    /// guarantees the artist sees a fresh sphere), and while no material is
    /// bound (so the host doesn't get stuck refreshing a stale RT).
    [[nodiscard]] bool is_dirty() const noexcept;

    /// Snapshot the bound material's fingerprint as "rendered into the
    /// preview RT successfully". After this call is_dirty() returns false
    /// until the next set_material() / hash drift on draw().
    ///
    /// No-op when no material is bound.
    void clear_dirty() noexcept;

    // ---- DrawBatcher path (DockSpace / apps/editor) -------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    ///
    /// Renders (top-to-bottom within bounds):
    ///   1. Panel background quad.
    ///   2. Accent separator bar.
    ///   3. Sphere swatch: textured_quad sampling the bound preview RT when
    ///      preview_texture() is valid, OR a 2D base_color-tinted quad
    ///      (Sprint-1 fallback) when the handle is null.
    ///   4. Metallic bar row (accent_success tint).
    ///   5. Roughness bar row (accent_warning tint).
    ///   6. Alpha cutoff bar row (error tint) — only when alpha_mode == kMask.
    ///   7. Texture path pills: albedo / normal / MR.
    ///
    /// When no material is bound (material() == nullptr), only the background
    /// and separator are drawn.
    ///
    /// Side effect: when a bound material's PBR fingerprint drifts from the
    /// last snapshot, draw() flips the dirty flag so the next is_dirty() poll
    /// reflects the change.
    ///
    /// Thread-safety: must be called from the render thread only.
    void draw(cd::ui::renderer::DrawBatcher&  batcher,
              const cd::ui::widgets::Theme&   theme,
              const cd::ui::widgets::Rect&    bounds) const;

private:
    const cd::asset::material_authoring::AuthoredMaterial* mat_ { nullptr };

    // phase738 RT cache --------------------------------------------------
    cd::rhi::TextureHandle preview_texture_ {};

    // 64-bit fingerprint of the last (base_color + metallic + roughness +
    // alpha_mode + alpha_cutoff) tuple acked by clear_dirty(). When this
    // differs from the bound material's current fingerprint we report
    // is_dirty() == true to the apps/editor host.
    mutable std::uint64_t last_rendered_hash_ { 0U };
    // false until clear_dirty() is first called — so is_dirty() defaults to
    // true and the host pays the very first PBR render unconditionally.
    mutable bool          has_rendered_       { false };

    /// Compute the PBR-relevant fingerprint of the bound material.
    /// Returns 0 when mat_ == nullptr.
    [[nodiscard]] std::uint64_t compute_hash_() const noexcept;
};

}  // namespace cd::editor::panel::material_preview
