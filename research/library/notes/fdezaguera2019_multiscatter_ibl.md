# fdezaguera2019_multiscatter_ibl

**A Multiple-Scattering Microfacet Model for Real-Time Image-Based Lighting.**
Carmelo J. Fdez-Aguera. Journal of Computer Graphics Techniques, Vol. 8, No. 1, 2019, pp. 45-55.
URL: https://jcgt.org/published/0008/01/03/

## Ozet

- **Problem**: Karis 2013 split-sum IBL approximation handles only single-scattering. At low roughness for metallic surfaces, energy is lost between microfacet bounces, making metals appear darker than physically correct. The error reaches ~10-15% luminance loss for polished copper.
- **Yontem**: The missing energy equals the difference between incident irradiance and the single-scattering BRDF output. Since the precomputed BRDF LUT (the split-sum G2 + Fresnel term integral) already encodes how much energy escapes the microsurface, that complement can be recycled as an isotropic diffuse-like term. A second lookup into the BRDF LUT with a constant F0=1 yields the multiple-scattering compensation factor. Total shader cost: one extra LUT fetch + a few multiplies.
- **Veri / Setup**: Validated against reference path-traced renders of copper, gold and polished dielectrics (Fig. 1-3). Energy conservation demonstrated analytically in Section 2-3; Fresnel-weighted extension for arbitrary conductors in Section 3.
- **Sonuc**: Near-zero run-time overhead (single texture lookup), perfect energy conservation for GGX-Smith split-sum IBL, trivially extended to other precomputed-integral BRDF models. The technique is adopted in Filament 2019+ and glTF-Sample-Renderer.
- **Bizim calismaya baglanti**: Direct drop-in upgrade for the IBL bake consumer in `cd_ibl` / `HelloIbl`. Replace the current Karis single-scatter evaluate with the two-term form from Eq. 10 (p. 51): `F_ss * brdf_lut.x + brdf_lut.y` remains unchanged; add `kS_ms * E_ms` where `kS_ms` is fetched from the same LUT with `F0 = 1`. Closes ADR-W8-AZ residual energy gap.
- **Sayfa referansi**: Core formula §3, Eq. 10, p. 51. LUT reuse argument §2, p. 47. Copper comparison Fig. 1, p. 45.

## Supersedes

`karis2013realshading` (split-sum IBL, single scattering only). Fdez-Aguera extends it without breaking the existing LUT layout.

## Implementation status

`cd_ibl` library — shader-side only change. BRDF LUT bake unchanged; one extra `texture2D(brdfLut, vec2(NdotV, roughness))` call with `F0 = vec3(1.0)` to compute `E_avg`. Estimated effort: half-day (2 shader edits + smoke-test comparison against reference).
