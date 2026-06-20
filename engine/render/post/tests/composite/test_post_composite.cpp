// =============================================================================
// cd::post_composite unit tests — Push layout + GLSL source sanity.
//
// Return-code harness (no gtest dep — INTERFACE lib + std-only). Each
// predicate is a host-side contract check that NEVER alters rendered output:
// push-constant size/offset, binding-slot enum freeze, and GLSL string
// presence of every documented operator/effect path.
// =============================================================================
#include <cd/post/composite/Composite.hpp>

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

namespace
{

[[nodiscard]] bool push_layout_is_256_bytes() noexcept
{
    return sizeof(cd::post::composite::Push) == 256U;
}

[[nodiscard]] bool binding_count_matches() noexcept
{
    return cd::post::composite::kBindingCount == 6U;
}

// Each vec4 block is 16 bytes; the FS reads them positionally, so the C++
// Push struct must lay them out in the exact same order at 16-byte strides.
// A reorder or padding insertion here silently mis-feeds the shader.
[[nodiscard]] bool push_block_offsets_are_16byte_strided() noexcept
{
    using cd::post::composite::Push;
    return offsetof(Push, fx)             ==   0U &&
           offsetof(Push, ao)             ==  16U &&
           offsetof(Push, dof)            ==  32U &&
           offsetof(Push, shafts)         ==  48U &&
           offsetof(Push, sun_col)        ==  64U &&
           offsetof(Push, atmo)           ==  80U &&
           offsetof(Push, lens)           ==  96U &&
           offsetof(Push, cam_right)      == 112U &&
           offsetof(Push, cam_up)         == 128U &&
           offsetof(Push, cam_fwd)        == 144U &&
           offsetof(Push, cam_pos)        == 160U &&
           offsetof(Push, ssr)            == 176U &&
           offsetof(Push, prev_cam_right) == 192U &&
           offsetof(Push, prev_cam_up)    == 208U &&
           offsetof(Push, prev_cam_fwd)   == 224U &&
           offsetof(Push, prev_cam_pos)   == 240U;
}

[[nodiscard]] bool binding_slot_enum_frozen() noexcept
{
    using cd::post::composite::BindingSlot;
    return static_cast<unsigned>(BindingSlot::kHdrColor)     == 0U &&
           static_cast<unsigned>(BindingSlot::kBloomMip0)    == 1U &&
           static_cast<unsigned>(BindingSlot::kDepth)        == 2U &&
           static_cast<unsigned>(BindingSlot::kGbufNormal)   == 3U &&
           static_cast<unsigned>(BindingSlot::kHistoryPrev)  == 4U &&
           static_cast<unsigned>(BindingSlot::kGbufVelocity) == 5U;
}

[[nodiscard]] bool fs_declares_all_five_tonemap_operators() noexcept
{
    // The FS rounds pc.fx.x to op IDs 0..4 (Narkowicz / Hill / Hable / AGX /
    // HDR10 PQ). Each branch must be present so the operator menu stays whole.
    const std::string_view fs { cd::post::composite::kCompositeFS };
    return fs.find("op == 0") != std::string_view::npos &&
           fs.find("op == 1") != std::string_view::npos &&
           fs.find("op == 2") != std::string_view::npos &&
           fs.find("op == 3") != std::string_view::npos &&
           fs.find("hdr10")   != std::string_view::npos;
}

[[nodiscard]] bool fs_short_circuits_each_effect_by_epsilon() noexcept
{
    // Every effect dial gates on a small epsilon so the cost is opt-in and a
    // zeroed push keeps the frame == raw HDR + bloom. Spot-check the AO,
    // DOF, SSR, shafts, vignette, grain guards.
    const std::string_view fs { cd::post::composite::kCompositeFS };
    return fs.find("pc.dof.x > 0.001")   != std::string_view::npos &&
           fs.find("pc.ssr.x > 0.001")   != std::string_view::npos &&
           fs.find("pc.shafts.z > 0.0")  != std::string_view::npos &&
           fs.find("pc.atmo.z > 0.001")  != std::string_view::npos &&
           fs.find("pc.atmo.w > 0.001")  != std::string_view::npos;
}

[[nodiscard]] bool gi_hook_fs_default_off_matches_no_macro() noexcept
{
    // With both flags off the helper returns the GI-hook FS unchanged: no
    // DDGI / ReSTIR binding is declared, so the default render is unaffected.
    const std::string out =
        cd::post::composite::make_composite_fs_source_with_gi_hooks(false, false);
    return out == std::string(cd::post::composite::kCompositeFSWithGiHooks) &&
           out.find("#define CD_COMPOSITE_USE_DDGI")   == std::string::npos &&
           out.find("#define CD_COMPOSITE_USE_RESTIR") == std::string::npos;
}

[[nodiscard]] bool vs_source_compiles_trivially() noexcept
{
    // Spot-check: VS source declares the fullscreen-triangle UV out
    // varying and uses gl_VertexIndex without extra inputs.
    const std::string_view vs { cd::post::composite::kCompositeVS };
    return vs.find("gl_VertexIndex") != std::string_view::npos &&
           vs.find("v_uv") != std::string_view::npos;
}

[[nodiscard]] bool fs_source_declares_all_bindings() noexcept
{
    const std::string_view fs { cd::post::composite::kCompositeFS };
    return fs.find("cd_hdr_color") != std::string_view::npos &&
           fs.find("cd_bloom_mip0") != std::string_view::npos &&
           fs.find("cd_depth") != std::string_view::npos &&
           fs.find("cd_gbuf_normal") != std::string_view::npos &&
           fs.find("cd_history_prev") != std::string_view::npos &&
           fs.find("cd_gbuf_velocity") != std::string_view::npos;
}

[[nodiscard]] bool fs_writes_dual_mrt() noexcept
{
    const std::string_view fs { cd::post::composite::kCompositeFS };
    return fs.find("out_color") != std::string_view::npos &&
           fs.find("out_history") != std::string_view::npos;
}

// W5-E: regression guards for the W4-C night-gate and W4-G cloud
// soften so future composite edits don't silently revert the visible
// bug fixes the user verified.
[[nodiscard]] bool fs_gates_sky_on_sun_strength() noexcept
{
    const std::string_view fs { cd::post::composite::kCompositeFS };
    // The sun_amt sentinel proves the all-lights-off bright-sky fix is
    // still wired in; the cloud_lit *= mix() proves the cloud overlay
    // is darkened at night; horizon_base = mix(night_horizon, ..., sun_amt)
    // proves the fog is too.
    return fs.find("float sun_amt") != std::string_view::npos &&
           fs.find("cloud_lit *= mix(") != std::string_view::npos &&
           fs.find("night_horizon") != std::string_view::npos;
}

[[nodiscard]] bool fs_chromab_has_uniform_floor() noexcept
{
    // W4-I: ChromAB now uses a uniform + r^2 mix so the RGB split
    // shows near screen-centre too. Catches accidental regression to
    // a pure radial r^2 (which only showed at corners).
    const std::string_view fs { cd::post::composite::kCompositeFS };
    return fs.find("4.0 + 28.0 * r * r") != std::string_view::npos;
}

[[nodiscard]] bool fs_widened_cloud_smoothstep() noexcept
{
    // W4-G: cloud band widened to 0.42..0.82 from 0.50..0.78 so the
    // edges read as soft cover instead of pixelated tiles.
    const std::string_view fs { cd::post::composite::kCompositeFS };
    return fs.find("smoothstep(0.42") != std::string_view::npos &&
           fs.find(", 0.82, density)") != std::string_view::npos;
}

[[nodiscard]] bool fs_wires_wronski_volumetric_fog() noexcept
{
    // phase512-volumetric-fog-wire: the composite FS now branches on
    // the sign of pc.atmo.x — negative = Wronski 2014 integrated
    // single-scatter (16 quadratic-warped slices with Beer-Lambert
    // transmittance + HG phase), positive = legacy single-tap exp
    // fog. Catches regression to a pure-positive single-tap path.
    const std::string_view fs { cd::post::composite::kCompositeFS };
    return fs.find("vol_fog_on") != std::string_view::npos &&
           fs.find("kVolSlices") != std::string_view::npos &&
           fs.find("Wronski") != std::string_view::npos;
}

}  // namespace

int main()
{
    if (!push_layout_is_256_bytes())                  return 1;
    if (!binding_count_matches())                      return 2;
    if (!vs_source_compiles_trivially())               return 3;
    if (!fs_source_declares_all_bindings())            return 4;
    if (!fs_writes_dual_mrt())                         return 5;
    if (!fs_gates_sky_on_sun_strength())               return 6;
    if (!fs_chromab_has_uniform_floor())               return 7;
    if (!fs_widened_cloud_smoothstep())                return 8;
    if (!fs_wires_wronski_volumetric_fog())            return 9;
    if (!push_block_offsets_are_16byte_strided())      return 10;
    if (!binding_slot_enum_frozen())                   return 11;
    if (!fs_declares_all_five_tonemap_operators())     return 12;
    if (!fs_short_circuits_each_effect_by_epsilon())   return 13;
    if (!gi_hook_fs_default_off_matches_no_macro())    return 14;
    return 0;
}
