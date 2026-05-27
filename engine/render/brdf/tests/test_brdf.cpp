// cd::brdf umbrella — one symbol per member lib so the includes are
// linker-touched and the aggregator header compiles in one TU.
#include <cd/brdf/brdf.hpp>

int main()
{
    // LTC: matrix sampler returns sane values at midpoint.
    const auto m = cd::brdf_ltc::ltc_inverse_matrix(0.5F, 0.5F);
    if (m.a <= 0.0F) return 1;
    // Sheen: charlie_d at NoH=0.7 must be positive (NoH=1 → sin2=0 → 0).
    if (cd::brdf_sheen_clearcoat::charlie_d(0.5F, 0.7F) <= 0.0F) return 2;
    // SSS: burley profile non-negative at r=1.
    if (cd::brdf_sss::burley_diffusion_profile(1.0F, 0.5F) < 0.0F) return 3;
    return 0;
}
