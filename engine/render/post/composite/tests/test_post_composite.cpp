// =============================================================================
// cd::post_composite unit tests — Push layout + GLSL source sanity.
// =============================================================================
#include <cd/post/composite/Composite.hpp>

#include <cstring>
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

}  // namespace

int main()
{
    if (!push_layout_is_256_bytes())           return 1;
    if (!binding_count_matches())               return 2;
    if (!vs_source_compiles_trivially())        return 3;
    if (!fs_source_declares_all_bindings())     return 4;
    if (!fs_writes_dual_mrt())                  return 5;
    if (!fs_gates_sky_on_sun_strength())        return 6;
    if (!fs_chromab_has_uniform_floor())        return 7;
    if (!fs_widened_cloud_smoothstep())         return 8;
    return 0;
}
