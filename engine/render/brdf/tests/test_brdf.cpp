// cd::brdf umbrella — one symbol per member lib so the includes are
// linker-touched and the aggregator header compiles in one TU.
#include <cd/brdf/brdf.hpp>

#include <array>
#include <cmath>

int main()
{
    // LTC: matrix sampler returns sane values at midpoint.
    const auto m = cd::brdf::ltc::ltc_inverse_matrix(0.5F, 0.5F);
    if (m.a <= 0.0F) return 1;
    // Sheen: charlie_d at NoH=0.7 must be positive (NoH=1 → sin2=0 → 0).
    if (cd::brdf::sheen_clearcoat::charlie_d(0.5F, 0.7F) <= 0.0F) return 2;
    // SSS: burley profile non-negative at r=1.
    if (cd::brdf::sss::burley_diffusion_profile(1.0F, 0.5F) < 0.0F) return 3;

    // --- ADD-ONLY: the umbrella reaches every lobe's full surface in one TU ---
    // LTC polygon integral through the umbrella include (Heitz 2016).
    const std::array<cd::math::Vec3f, 4> quad {{
        { -0.5F, -0.5F, 2.0F },
        {  0.5F, -0.5F, 2.0F },
        {  0.5F,  0.5F, 2.0F },
        { -0.5F,  0.5F, 2.0F } }};
    const float e = cd::brdf::ltc::polygon_irradiance(quad, m);
    if (e < 0.0F || e > 1.0F) return 4;

    // Clearcoat + Neubelt reachable and finite via the umbrella (Filament).
    const float dv = cd::brdf::sheen_clearcoat::clearcoat_d_v(0.2F, 1.0F, 1.0F, 1.0F);
    if (!std::isfinite(dv) || dv <= 0.0F) return 5;
    if (cd::brdf::sheen_clearcoat::v_neubelt(0.5F, 0.5F) <= 0.0F) return 6;

    // SSS kernel baker reachable; normalised weights sum to 1 (Jimenez 2010).
    const auto k = cd::brdf::sss::make_burley_kernel(8, 1.0F, 5.0F);
    if (k.weights.empty()) return 7;
    float sum = k.weights[0];
    for (std::size_t i = 1; i < k.weights.size(); ++i) sum += 2.0F * k.weights[i];
    if (std::abs(sum - 1.0F) > 1e-3F) return 8;

    return 0;
}
