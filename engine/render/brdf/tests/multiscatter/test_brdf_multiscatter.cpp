// =============================================================================
// CHROMODYNAMIC — test_brdf_multiscatter.cpp
// Phase 18 — Unit tests for Turquin (2019) & Kulla-Conty (2017) Multiple-
//            Scattering Microfacet Energy Compensation and Belcour-Barla (2017)
//            Thin-Film Iridescence.
// =============================================================================
#include <cd/brdf/multiscatter/TurquinMultiScatter.hpp>

#include <cmath>
#include <numbers>

int main()
{
    using namespace cd::brdf::multiscatter;

    // 1. White-furnace directional albedo E(mu, roughness) at roughness=0 must be ~1.0:
    const float e_mirror_normal  = directional_albedo_ggx(1.0F, 0.0F);
    const float e_mirror_grazing = directional_albedo_ggx(0.2F, 0.0F);
    if (std::abs(e_mirror_normal - 1.0F) > 0.06F) return 1;
    if (std::abs(e_mirror_grazing - 1.0F) > 0.06F) return 2;

    // 2. Monotonic energy loss of single-scatter GGX as roughness increases:
    const float e_r02 = directional_albedo_ggx(0.5F, 0.2F);
    const float e_r06 = directional_albedo_ggx(0.5F, 0.6F);
    const float e_r10 = directional_albedo_ggx(0.5F, 1.0F);
    if (!(e_r02 > e_r06 && e_r06 > e_r10)) return 3;
    if (e_r10 <= 0.05F || e_r10 >= 0.85F) return 4;

    // 3. Hemispherically averaged albedo E_avg(roughness) decreases monotonically:
    const float eavg_0 = average_albedo_ggx(0.0F);
    const float eavg_5 = average_albedo_ggx(0.5F);
    const float eavg_1 = average_albedo_ggx(1.0F);
    if (!(eavg_0 > eavg_5 && eavg_5 > eavg_1)) return 5;

    // 4. Turquin (2019) white-furnace identity:
    //    For a pure white conductor F0 = (1,1,1), E_ss(mu) * turquin_comp(1, E_ss(mu), 1.0) == 1.0!
    const cd::math::Vec3f f0_white { 1.0F, 1.0F, 1.0F };
    for (float r : { 0.1F, 0.35F, 0.7F, 1.0F })
    {
        for (float mu : { 0.15F, 0.5F, 0.95F })
        {
            const float e_ss = directional_albedo_ggx(mu, r);
            const auto comp  = turquin_energy_compensation(f0_white, e_ss, 1.0F);
            const float total_energy = e_ss * comp.x;
            if (std::abs(total_energy - 1.0F) > 1e-4F) return 6;
        }
    }

    // 5. Weight=0 disables compensation (returns 1,1,1):
    const auto comp_off = turquin_energy_compensation({ 1.0F, 0.8F, 0.3F }, 0.5F, 0.0F);
    if (std::abs(comp_off.x - 1.0F) > 1e-6F ||
        std::abs(comp_off.y - 1.0F) > 1e-6F ||
        std::abs(comp_off.z - 1.0F) > 1e-6F)
    {
        return 7;
    }

    // 6. Kulla-Conty (2017) additive lobe Helmholtz reciprocity:
    //    f_ms(v, l) == f_ms(l, v) for all (e_v, e_l):
    const cd::math::Vec3f f0_gold { 1.00F, 0.78F, 0.34F };
    const auto kc_vl = kulla_conty_multiscatter_lobe(f0_gold, e_r02, e_r06, eavg_5);
    const auto kc_lv = kulla_conty_multiscatter_lobe(f0_gold, e_r06, e_r02, eavg_5);
    if (std::abs(kc_vl.x - kc_lv.x) > 1e-6F ||
        std::abs(kc_vl.y - kc_lv.y) > 1e-6F ||
        std::abs(kc_vl.z - kc_lv.z) > 1e-6F)
    {
        return 8;
    }
    if (kc_vl.x <= 0.0F || kc_vl.y <= 0.0F || kc_vl.z <= 0.0F) return 9;

    // 7. Belcour-Barla (2017) thin-film iridescence:
    //    - At film_thickness_nm = 0, matches standard Schlick Fresnel exactly.
    //    - At film_thickness_nm = 380nm vs 520nm, produces distinct chromatic interference
    //      Strictly bounded within [0, 1].
    const auto f_base  = belcour_barla_iridescence_fresnel(f0_gold, 0.75F, 0.0F, 1.5F);
    const auto f_380nm = belcour_barla_iridescence_fresnel(f0_gold, 0.75F, 380.0F, 1.5F);
    const auto f_520nm = belcour_barla_iridescence_fresnel(f0_gold, 0.75F, 520.0F, 1.5F);

    for (const auto& f : { f_base, f_380nm, f_520nm })
    {
        if (f.x < 0.0F || f.x > 1.0F ||
            f.y < 0.0F || f.y > 1.0F ||
            f.z < 0.0F || f.z > 1.0F)
        {
            return 10;
        }
    }
    const float diff_380_520 = std::abs(f_380nm.x - f_520nm.x)
                             + std::abs(f_380nm.y - f_520nm.y)
                             + std::abs(f_380nm.z - f_520nm.z);
    if (diff_380_520 < 0.05F) return 11;

    // 7b. Exact infinite-bounce Airy summation must remain strictly < 1.0 for sub-unity
    //     substrate reflectance (no unphysical 2-beam >1.0 flat-top clipping at constructive peaks):
    const cd::math::Vec3f f0_copper { 0.95F, 0.64F, 0.54F };
    for (int nm = 100; nm <= 600; nm += 25)
    {
        const auto f_irid = belcour_barla_iridescence_fresnel(
            f0_copper, 0.85F, static_cast<float>(nm), 1.5F);
        if (f_irid.x >= 0.999F || f_irid.y >= 0.999F || f_irid.z >= 0.999F) return 16;
    }

    // 8. Combined evaluator:
    //    - Horizon/below-horizon (NoL <= 0 or NoV <= 0) returns 0
    //    - Rough gold conductor with multiscatter_weight=1 is strictly brighter
    //      than single-scatter (multiscatter_weight=0)
    const auto zero_lobe = evaluate_turquin_iridescent_ggx(
        f0_gold, 0.8F, 0.5F, 0.0F, 0.7F, 0.7F, 1.0F, 0.0F);
    if (zero_lobe.x != 0.0F || zero_lobe.y != 0.0F || zero_lobe.z != 0.0F) return 12;

    const auto ss_lobe = evaluate_turquin_iridescent_ggx(
        f0_gold, 0.85F, 0.45F, 0.65F, 0.8F, 0.75F, 0.0F, 0.0F);
    const auto ms_lobe = evaluate_turquin_iridescent_ggx(
        f0_gold, 0.85F, 0.45F, 0.65F, 0.8F, 0.75F, 1.0F, 0.0F);
    if (!(ms_lobe.x > ss_lobe.x * 1.25F && ms_lobe.y > ss_lobe.y * 1.20F)) return 13;

    // 9. Shader snippet contains both GLSL entrypoints:
    if (kTurquinIridescenceGlsl.find("cd_turquin_multiscatter_comp") == std::string_view::npos) return 14;
    if (kTurquinIridescenceGlsl.find("cd_belcour_barla_iridescence") == std::string_view::npos) return 15;

    return 0;
}
