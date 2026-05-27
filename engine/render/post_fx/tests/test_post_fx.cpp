// cd::post_fx umbrella — verify the aggregator header pulls in every
// member lib's main type/constant. Single TU compile check.
#include <cd/post_fx/post_fx.hpp>

#include <string_view>

int main()
{
    // One symbol referenced per member lib so the linker doesn't strip
    // the test before the includes are checked.
    if (cd::post_bloom::kDefaultMipCount      == 0) return 1;
    if (cd::post_composite::kBindingCount      == 0) return 2;
    if (cd::post_taa::Settings {}.jitter_phase_count == 0) return 3;
    if (cd::post_tonemap::glsl_for(cd::post_tonemap::Operator::kHill).empty()) return 4;
    return 0;
}
