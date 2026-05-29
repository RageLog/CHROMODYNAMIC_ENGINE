# duff2017_onb_revisited

**Building an Orthonormal Basis, Revisited.**
Tom Duff, James Burgess, Per Christensen, Christophe Hery, Andrew Kensler, Max Liani, Ryusuke Villemin (Pixar).
Journal of Computer Graphics Techniques, Vol. 6, No. 1, 2017, pp. 1-8.
URL: https://jcgt.org/published/0006/01/01/

## Ozet

- **Problem**: Frisvad 2012 builds a tangent frame from a single unit normal using only additions and multiplications (no sqrt). However, it loses ~4 decimal digits of precision near the south pole (n.z close to -1) due to catastrophic cancellation in the denominator `1 + n.z`. The RMS deviation from orthogonality reaches 1e-3 in that region (Fig. 1b, p. 2).
- **Yontem**: Replace the problematic branch with a sign-based conditional that symmetrically handles both poles. The revised formula uses `s = copysign(1, n.z)` and `a = -1 / (s + n.z)` to avoid the cancellation. Numerical precision is ~1e-7 everywhere (Fig. 1b), a four-order-of-magnitude improvement. Total cost: the same as Frisvad (no division by sqrt), just two extra operations (copysign + sign-conditional multiply).
- **Veri / Setup**: Tested on a hemisphere grid of 10^6 unit vectors; RMS orthogonality error plotted as heat map (Fig. 1, p. 2). GLSL/HLSL and C++ listings provided (§4, p. 5-6). Performance difference from Frisvad: negligible (one extra `copysign`).
- **Sonuc**: Numerically robust ONB construction at Frisvad cost. Adopted by PBRT-v4 and Filament's tangent frame utilities. Eliminates visible tangent-frame discontinuities in normal-mapped geometry near z=-1 orientations.
- **Bizim calismaya baglanti**: Drop-in replacement for any `build_onb(normal)` call in `cd_math` (tangent-frame utilities) and in GLSL shader code for GTAO, planar shadow projection, and LTC area-light tangent frames. Replace Frisvad's formula with Duff Eq. 1 (p. 3). The GLSL listing on p. 6 can be copied verbatim into `cd_rhi` shader utils.
- **Sayfa referansi**: Revised formula §3, Eq. 1, p. 3. Precision comparison Fig. 1, p. 2. GLSL listing §4.1, p. 5-6.

## Supersedes

`frisvad2012onb` for production use. Frisvad 2012 remains valid as the conceptual baseline; Duff 2017 is the numerically correct implementation to actually ship.

## Implementation status

### DONE — phase402-D-F11 (2026-05-29)

- `engine/foundation/math/include/cd/math/Onb.hpp` — `cd::math::duff_branchless_onb(Vec3f n)` implemented with citation comment referencing JCGT 6:1. Frisvad 2012 kept as a `[[deprecated]]` alias forwarding to the Duff implementation.
- `engine/foundation/math/tests/test_math.cpp` — 5 new `DuffOnb.*` gtest cases: NorthPole, SouthPole (was degenerate under Frisvad), HostileNearSouthPole, ArbitraryNormal (6 octants), PrecisionComparison (verifies Duff < Frisvad error near n.z=-1).
- GLSL sites audited: hello_engine prim.frag.glsl previously used Frisvad for LTC tangent frame derivation; W8-N (phase-W8) migrated it to an uploaded CPU tangent. No inline Frisvad GLSL remains in live shader code. GLSL equivalent provided in Onb.hpp header comment for future use.
- All 106 ctest targets PASS. Build clean (no warnings).
