// cd::volumetric_fx umbrella — touches one symbol per member lib.
#include <cd/volumetric/fx/volumetric_fx.hpp>

int main()
{
    // volumetric_fog: Beer-Lambert at σ=0 returns 1 (no extinction).
    if (cd::volumetric::fog::beer_lambert(0.0F, 1.0F) != 1.0F) return 1;
    // volumetric_clouds: remap basic check.
    if (cd::volumetric::clouds::remap(0.5F, 0.0F, 1.0F, 0.0F, 10.0F) != 5.0F) return 2;
    return 0;
}
