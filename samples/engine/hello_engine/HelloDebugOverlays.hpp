// =============================================================================
// HelloDebugOverlays.hpp
// -----------------------------------------------------------------------------
// phase1044 — pure-line 3D viewport overlay builders, extracted from
// main()'s render loop (phases 1035-1041 shipped them inline; this
// header is the N-series extraction pass).
//
// Every overlay here is a PURE function of (HelloEngineFx, LineBatch):
// no command buffer, no device, no materials — they only append
// cd::debug_line geometry which main()'s single end-of-HDR-pass flush
// (phase 1031/1034) uploads and draws. That property is what makes
// them extractable as a unit and trivially testable.
//
// Anchors are scattered across the demo airspace above the scene so
// multiple overlays can be toggled simultaneously without overlap.
// =============================================================================
#pragma once

#include "HelloEngineFx.hpp"

#include <cd/atmosphere/Atmosphere.hpp>
#include <cd/brdf/sheen_clearcoat/SheenClearcoat.hpp>
#include <cd/debug_line/DebugLine.hpp>
#include <cd/ibl/BrdfLut.hpp>
#include <cd/math/Vector.hpp>
#include <cd/virtual_textures/VirtualTextures.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace cd_sample {

// phase1035-3d-viewport-phase-function-polar: polar plots of the two
// scattering phase functions in the XY plane at a fixed anchor:
// radius(theta) = normalised phase value, +X = forward scatter. Warm
// lobe = Henyey-Greenstein at the panel's g (stretches toward +X as
// g -> 1, flips to back-scatter for g < 0); cool dumbbell = Rayleigh;
// grey ring = isotropic reference under the HG normalisation.
inline void append_phase_function_polar(const HelloEngineFx& fx,
                                        cd::debug_line::LineBatch& lines)
{
    constexpr cd::math::Vec3f kAnchor { 3.5F, 2.5F, -3.0F };
    constexpr int kPolarSamples = 96;
    constexpr float kBaseR = 0.15F;
    constexpr float kSpanR = 1.05F;
    constexpr float kTwoPi = 6.28318530717958647692F;
    std::array<cd::math::Vec3f, kPolarSamples + 1> hg_pts {};
    std::array<cd::math::Vec3f, kPolarSamples + 1> ray_pts {};
    float hg_max = 1e-6F;
    float ray_max = 1e-6F;
    std::array<float, kPolarSamples> hg_v {};
    std::array<float, kPolarSamples> ray_v {};
    for (int i = 0; i < kPolarSamples; ++i)
    {
        const float th = kTwoPi * static_cast<float>(i) /
                         static_cast<float>(kPolarSamples);
        const float ct = std::cos(th);
        hg_v[static_cast<std::size_t>(i)] =
            cd::atmosphere::henyey_greenstein(ct, fx.atmo_phase_g);
        ray_v[static_cast<std::size_t>(i)] =
            cd::atmosphere::rayleigh_phase(ct);
        hg_max  = std::max(hg_max,  hg_v[static_cast<std::size_t>(i)]);
        ray_max = std::max(ray_max, ray_v[static_cast<std::size_t>(i)]);
    }
    for (int i = 0; i < kPolarSamples; ++i)
    {
        const float th = kTwoPi * static_cast<float>(i) /
                         static_cast<float>(kPolarSamples);
        const float ca = std::cos(th);
        const float sa = std::sin(th);
        const float hr = kBaseR + kSpanR *
            (hg_v[static_cast<std::size_t>(i)] / hg_max);
        const float rr = kBaseR + kSpanR *
            (ray_v[static_cast<std::size_t>(i)] / ray_max);
        hg_pts[static_cast<std::size_t>(i)] = {
            kAnchor.x + ca * hr, kAnchor.y + sa * hr, kAnchor.z };
        ray_pts[static_cast<std::size_t>(i)] = {
            kAnchor.x + ca * rr, kAnchor.y + sa * rr, kAnchor.z };
    }
    hg_pts[kPolarSamples]  = hg_pts[0];   // close the loops
    ray_pts[kPolarSamples] = ray_pts[0];
    lines.add_polyline(hg_pts,  { 0.95F, 0.60F, 0.20F, 1.0F });
    lines.add_polyline(ray_pts, { 0.30F, 0.60F, 0.95F, 1.0F });
    constexpr float kInv4Pi = 0.07957747F;  // 1 / (4 pi)
    const float iso_r = kBaseR + kSpanR * (kInv4Pi / hg_max);
    lines.add_circle(
        kAnchor, { 0.0F, 0.0F, 1.0F }, iso_r, 48,
        { 0.50F, 0.50F, 0.55F, 1.0F });
}

// phase1036-3d-viewport-audio-waveform: ~2.5 cycles of the tone-synth
// sine as a polyline ribbon: x = time across a 4 m span, y = sample *
// amplitude. The grey base line is the zero axis. Frequency
// compresses the ribbon (more cycles fit the fixed window scaled to
// 2.5 cycles of a 440 Hz reference), amplitude scales it.
inline void append_audio_waveform(const HelloEngineFx& fx,
                                  cd::debug_line::LineBatch& lines)
{
    constexpr cd::math::Vec3f kAnchor { 0.0F, 4.0F, 0.0F };
    constexpr float kSpanX = 4.0F;
    constexpr float kAmpY  = 0.8F;
    constexpr int kWaveSamples = 128;
    constexpr float kWindowSec = 2.5F / 440.0F;
    std::array<cd::math::Vec3f, kWaveSamples> wave {};
    for (int i = 0; i < kWaveSamples; ++i)
    {
        const float t01 = static_cast<float>(i) /
                          static_cast<float>(kWaveSamples - 1);
        const float t = t01 * kWindowSec;
        const float v = std::sin(2.0F * std::numbers::pi_v<float> *
                                 fx.audio_tone_hz * t) *
                        fx.audio_tone_amp;
        wave[static_cast<std::size_t>(i)] = {
            kAnchor.x - kSpanX * 0.5F + t01 * kSpanX,
            kAnchor.y + v * kAmpY,
            kAnchor.z };
    }
    lines.add_polyline(wave, { 0.25F, 0.90F, 0.65F, 1.0F });
    lines.add_line(
        { kAnchor.x - kSpanX * 0.5F, kAnchor.y, kAnchor.z },
        { kAnchor.x + kSpanX * 0.5F, kAnchor.y, kAnchor.z },
        { 0.45F, 0.45F, 0.50F, 1.0F });
}

// phase1037-3d-viewport-rng-histogram: 32 vertical columns (one per
// PCG32 bin) over a 3.2 m span, height = count / max * 1.2 m, plus a
// grey reference line at the expected-uniform height. With large N
// the column tops visibly converge onto the reference line.
inline void append_rng_histogram(const HelloEngineFx& fx,
                                 cd::debug_line::LineBatch& lines)
{
    constexpr cd::math::Vec3f kAnchor { -3.5F, 3.8F, 0.0F };
    constexpr float kSpanX = 3.2F;
    constexpr float kMaxH  = 1.2F;
    std::uint32_t hmax = 1;
    for (const auto c : fx.rng_hist) hmax = std::max(hmax, c);
    for (std::size_t bi = 0; bi < fx.rng_hist.size(); ++bi)
    {
        const float x = kAnchor.x - kSpanX * 0.5F + kSpanX *
            static_cast<float>(bi) /
            static_cast<float>(fx.rng_hist.size() - 1);
        const float h = kMaxH *
            static_cast<float>(fx.rng_hist[bi]) /
            static_cast<float>(hmax);
        lines.add_line(
            { x, kAnchor.y, kAnchor.z },
            { x, kAnchor.y + h, kAnchor.z },
            { 0.30F, 0.80F, 0.95F, 1.0F });
    }
    const float expected_h = kMaxH *
        (static_cast<float>(fx.rng_total) / 32.0F) /
        static_cast<float>(hmax);
    lines.add_line(
        { kAnchor.x - kSpanX * 0.5F, kAnchor.y + expected_h, kAnchor.z },
        { kAnchor.x + kSpanX * 0.5F, kAnchor.y + expected_h, kAnchor.z },
        { 0.55F, 0.55F, 0.60F, 1.0F });
}

// phase1038-3d-viewport-brdf-lut-surface: lazily bakes a 16x16x32
// split-sum LUT (one-time) and renders the SCALE channel as a
// wireframe height surface over the (n.v, roughness) plane: 16 + 16
// grid polylines, x = n.v, z = roughness, y = scale. A warm cross
// marks the panel's lookup point (Karis 2013 split-sum terrain).
inline void append_brdf_lut_surface(const HelloEngineFx& fx,
                                    cd::debug_line::LineBatch& lines)
{
    static const cd::ibl::BrdfLut s_lut3d =
        cd::ibl::bake_brdf_lut(16, 16, 32);
    constexpr cd::math::Vec3f kAnchor { 3.5F, 3.8F, 3.0F };
    constexpr float kSpan = 2.4F;
    constexpr float kHeight = 1.0F;
    const auto lut_scale_at = [&](std::uint32_t px, std::uint32_t py) {
        const std::size_t off =
            (static_cast<std::size_t>(py) * s_lut3d.width + px) * 2U;
        return s_lut3d.rg[off];
    };
    const auto surface_point = [&](std::uint32_t px, std::uint32_t py) {
        const float u = static_cast<float>(px) /
                        static_cast<float>(s_lut3d.width - 1U);
        const float v = static_cast<float>(py) /
                        static_cast<float>(s_lut3d.height - 1U);
        return cd::math::Vec3f {
            kAnchor.x - kSpan * 0.5F + u * kSpan,
            kAnchor.y + lut_scale_at(px, py) * kHeight,
            kAnchor.z - kSpan * 0.5F + v * kSpan };
    };
    std::array<cd::math::Vec3f, 16> row {};
    for (std::uint32_t py = 0; py < s_lut3d.height; ++py)
    {
        for (std::uint32_t px = 0; px < s_lut3d.width; ++px)
            row[px] = surface_point(px, py);
        lines.add_polyline(row, { 0.85F, 0.70F, 0.30F, 1.0F });
    }
    for (std::uint32_t px = 0; px < s_lut3d.width; ++px)
    {
        for (std::uint32_t py = 0; py < s_lut3d.height; ++py)
            row[py] = surface_point(px, py);
        lines.add_polyline(row, { 0.60F, 0.50F, 0.25F, 1.0F });
    }
    const auto mpx = static_cast<std::uint32_t>(std::clamp(
        fx.brdf_lut_nv * static_cast<float>(s_lut3d.width),
        0.0F, static_cast<float>(s_lut3d.width - 1U)));
    const auto mpy = static_cast<std::uint32_t>(std::clamp(
        fx.brdf_lut_r * static_cast<float>(s_lut3d.height),
        0.0F, static_cast<float>(s_lut3d.height - 1U)));
    lines.add_cross(
        surface_point(mpx, mpy), 0.12F,
        { 0.95F, 0.35F, 0.15F, 1.0F });
}

// phase1039-3d-viewport-brdf-lobes: textbook BRDF lobe diagram —
// half-angle theta in [-90, 90] deg, radius = normalised
// distribution value, lobes in the upper half-plane above a grey
// surface line with a vertical normal marker. Warm = Charlie sheen D
// (Estevez 2017); cool = clearcoat D*V (Filament).
inline void append_brdf_lobes(const HelloEngineFx& fx,
                              cd::debug_line::LineBatch& lines)
{
    constexpr cd::math::Vec3f kAnchor { 0.0F, 5.5F, -3.0F };
    constexpr int kLobeSamples = 64;
    constexpr float kLobeR = 1.3F;
    constexpr float kHalfPi = 1.57079632679489661923F;
    std::array<float, kLobeSamples> ch_v {};
    std::array<float, kLobeSamples> cc_v {};
    float ch_max = 1e-6F;
    float cc_max = 1e-6F;
    for (int i = 0; i < kLobeSamples; ++i)
    {
        const float th = -kHalfPi + 2.0F * kHalfPi *
            static_cast<float>(i) /
            static_cast<float>(kLobeSamples - 1);
        const float nh = std::cos(th);
        ch_v[static_cast<std::size_t>(i)] =
            cd::brdf::sheen_clearcoat::charlie_d(fx.sc_roughness, nh);
        cc_v[static_cast<std::size_t>(i)] =
            cd::brdf::sheen_clearcoat::clearcoat_d_v(
                fx.sc_roughness, nh, fx.sc_nv, fx.sc_nl);
        ch_max = std::max(ch_max, ch_v[static_cast<std::size_t>(i)]);
        cc_max = std::max(cc_max, cc_v[static_cast<std::size_t>(i)]);
    }
    std::array<cd::math::Vec3f, kLobeSamples> ch_pts {};
    std::array<cd::math::Vec3f, kLobeSamples> cc_pts {};
    for (int i = 0; i < kLobeSamples; ++i)
    {
        const float th = -kHalfPi + 2.0F * kHalfPi *
            static_cast<float>(i) /
            static_cast<float>(kLobeSamples - 1);
        const float sx = std::sin(th);
        const float cy = std::cos(th);
        const float chr = kLobeR *
            (ch_v[static_cast<std::size_t>(i)] / ch_max);
        const float ccr = kLobeR *
            (cc_v[static_cast<std::size_t>(i)] / cc_max);
        ch_pts[static_cast<std::size_t>(i)] = {
            kAnchor.x + sx * chr, kAnchor.y + cy * chr, kAnchor.z };
        cc_pts[static_cast<std::size_t>(i)] = {
            kAnchor.x + sx * ccr, kAnchor.y + cy * ccr, kAnchor.z };
    }
    lines.add_polyline(ch_pts, { 0.95F, 0.60F, 0.20F, 1.0F });
    lines.add_polyline(cc_pts, { 0.30F, 0.60F, 0.95F, 1.0F });
    lines.add_line(
        { kAnchor.x - kLobeR * 1.1F, kAnchor.y, kAnchor.z },
        { kAnchor.x + kLobeR * 1.1F, kAnchor.y, kAnchor.z },
        { 0.50F, 0.50F, 0.55F, 1.0F });   // surface plane
    lines.add_line(
        kAnchor,
        { kAnchor.x, kAnchor.y + kLobeR * 1.1F, kAnchor.z },
        { 0.70F, 0.70F, 0.75F, 1.0F });   // normal marker
}

// phase1040-3d-viewport-sss-falloff: the three per-channel Burley
// diffusion falloff curves OVERLAID on one baseline: x = scatter
// radius (0..4 mm over a 3 m span), y = R(r) via the unnormalised
// exponential pair. Red bleeding farther than green/blue — the
// reason skin glows red at shadow edges — reads from the separation.
inline void append_sss_falloff(const HelloEngineFx& fx,
                               cd::debug_line::LineBatch& lines)
{
    constexpr cd::math::Vec3f kAnchor { -3.5F, 5.5F, -3.0F };
    constexpr float kSpanX = 3.0F;
    constexpr float kAmpY  = 1.1F;
    constexpr int kCurveSamples = 64;
    const std::array<cd::math::Vec4f, 3> kChanCol {{
        { 0.95F, 0.25F, 0.20F, 1.0F },
        { 0.25F, 0.95F, 0.30F, 1.0F },
        { 0.30F, 0.50F, 0.95F, 1.0F } }};
    for (std::size_t ch = 0; ch < 3; ++ch)
    {
        const float d = std::max(fx.sss_mfp[ch], 1e-3F);
        std::array<cd::math::Vec3f, kCurveSamples> pts {};
        for (int i = 0; i < kCurveSamples; ++i)
        {
            const float t01 = static_cast<float>(i) /
                              static_cast<float>(kCurveSamples - 1);
            const float r = t01 * 4.0F;  // mm
            const float v = 0.25F *
                (std::exp(-r / (3.0F * d)) + std::exp(-r / d));
            pts[static_cast<std::size_t>(i)] = {
                kAnchor.x - kSpanX * 0.5F + t01 * kSpanX,
                kAnchor.y + v * kAmpY,
                kAnchor.z };
        }
        lines.add_polyline(pts, kChanCol[ch]);
    }
    lines.add_line(
        { kAnchor.x - kSpanX * 0.5F, kAnchor.y, kAnchor.z },
        { kAnchor.x + kSpanX * 0.5F, kAnchor.y, kAnchor.z },
        { 0.50F, 0.50F, 0.55F, 1.0F });   // baseline
}

// phase1041-3d-viewport-shafts-ring: the light-shafts cone-alignment
// sweep as a polar ring in the horizontal (XZ) plane around the
// vertical sun axis: radius(azimuth) = inline shaft intensity for a
// camera ray jittered toward that azimuth. Grey base ring = zero
// intensity; vertical line = sun axis.
inline void append_shafts_ring(const HelloEngineFx& fx,
                               cd::debug_line::LineBatch& lines)
{
    constexpr cd::math::Vec3f kAnchor { 0.0F, 5.5F, 3.0F };
    constexpr int kRingSamples = 96;
    constexpr float kBaseR = 0.5F;
    constexpr float kSpanR = 0.9F;
    constexpr float kTwoPi = 6.28318530717958647692F;
    const cd::math::Vec3f sun_axis { 0.0F, 1.0F, 0.0F };
    std::array<cd::math::Vec3f, kRingSamples + 1> ring {};
    for (int i = 0; i <= kRingSamples; ++i)
    {
        const float theta = kTwoPi * static_cast<float>(i % kRingSamples) /
                            static_cast<float>(kRingSamples);
        const float cam_x = fx.shafts_cam_dir[0] + std::cos(theta) * 0.3F;
        const float cam_y = fx.shafts_cam_dir[1] + std::sin(theta) * 0.3F;
        const float cam_z = fx.shafts_cam_dir[2];
        const float inv_len = 1.0F / std::max(std::sqrt(
            cam_x * cam_x + cam_y * cam_y + cam_z * cam_z), 1e-3F);
        const float cos_t = -(cam_x * inv_len * sun_axis.x +
                              cam_y * inv_len * sun_axis.y +
                              cam_z * inv_len * sun_axis.z);
        const float inten = std::max(0.0F, cos_t);
        const float r = kBaseR + kSpanR * inten;
        ring[static_cast<std::size_t>(i)] = {
            kAnchor.x + std::cos(theta) * r,
            kAnchor.y,
            kAnchor.z + std::sin(theta) * r };
    }
    lines.add_polyline(ring, { 0.95F, 0.85F, 0.35F, 1.0F });
    lines.add_circle(
        kAnchor, sun_axis, kBaseR, 48,
        { 0.50F, 0.50F, 0.55F, 1.0F });
    lines.add_line(
        { kAnchor.x, kAnchor.y - 0.6F, kAnchor.z },
        { kAnchor.x, kAnchor.y + 0.6F, kAnchor.z },
        { 0.70F, 0.70F, 0.75F, 1.0F });
}

// phase1053-3d-viewport-vt-atlas: the physical VT atlas as a 4x4
// wireframe grid. Resident slots are FILLED with an inner box tinted
// by the resident page's mip (warm = mip 0, cooling toward mip 4 —
// the classic VT debug palette where hot colours mean high detail);
// a white cross marks the slot holding the panel's requested page.
// Clicking "Request page" with a full atlas makes the FIFO eviction
// VISIBLE: a previously-filled cell empties as another fills.
inline void append_vt_atlas_overlay(
    const HelloEngineFx& fx,
    const cd::virtual_textures::PageTable& table,
    cd::debug_line::LineBatch& lines)
{
    constexpr cd::math::Vec3f kAnchor { 3.5F, 5.5F, -1.5F };
    constexpr float kCell = 0.5F;
    constexpr int kSlots = 4;  // 4x4 atlas (matches EngineState table)
    // Outer grid: 5 lines per direction in the XY plane.
    lines.add_grid(kAnchor,
                   { 1.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F },
                   2, kCell, { 0.55F, 0.55F, 0.60F, 1.0F });
    const auto cell_centre = [&](std::uint32_t sx, std::uint32_t sy) {
        return cd::math::Vec3f {
            kAnchor.x + (static_cast<float>(sx) - 1.5F) * kCell,
            kAnchor.y + (static_cast<float>(sy) - 1.5F) * kCell,
            kAnchor.z };
    };
    for (const auto& [pid, slot] : table.residents())
    {
        if (slot.slot_x >= kSlots || slot.slot_y >= kSlots)
            continue;
        const float mip_t =
            std::min(static_cast<float>(pid.mip) / 4.0F, 1.0F);
        const cd::math::Vec4f tint {
            0.95F - 0.65F * mip_t,
            0.55F - 0.25F * mip_t,
            0.20F + 0.75F * mip_t,
            1.0F };
        const auto c = cell_centre(slot.slot_x, slot.slot_y);
        constexpr float kInner = kCell * 0.38F;
        lines.add_aabb(
            { c.x - kInner, c.y - kInner, c.z - 0.02F },
            { c.x + kInner, c.y + kInner, c.z + 0.02F },
            tint);
    }
    cd::virtual_textures::PageId q {};
    q.x   = static_cast<std::uint16_t>(fx.vt_req[0]);
    q.y   = static_cast<std::uint16_t>(fx.vt_req[1]);
    q.mip = static_cast<std::uint8_t>(fx.vt_req[2]);
    if (const auto* slot = table.lookup(q); slot != nullptr)
    {
        lines.add_cross(cell_centre(slot->slot_x, slot->slot_y),
                        kCell * 0.30F, { 0.95F, 0.95F, 0.95F, 1.0F });
    }
}

/// Umbrella: appends every toggled-on pure-line overlay. Called once
/// per frame from main()'s render loop, just before the LineBatch
/// flush (phase 1031/1034).
inline void append_pure_line_overlays(const HelloEngineFx& fx,
                                      cd::debug_line::LineBatch& lines)
{
    if (fx.atmo_show_polar_3d)             append_phase_function_polar(fx, lines);
    if (fx.audio_show_wave_3d)             append_audio_waveform(fx, lines);
    if (fx.rng_show_hist_3d && fx.rng_total > 0)
                                           append_rng_histogram(fx, lines);
    if (fx.brdf_show_lut_3d)               append_brdf_lut_surface(fx, lines);
    if (fx.sc_show_lobes_3d)               append_brdf_lobes(fx, lines);
    if (fx.sss_show_falloff_3d)            append_sss_falloff(fx, lines);
    if (fx.shafts_show_ring_3d)            append_shafts_ring(fx, lines);
}

}  // namespace cd_sample
