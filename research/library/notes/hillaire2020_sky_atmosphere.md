# hillaire2020_sky_atmosphere

**A Scalable and Production Ready Sky and Atmosphere Rendering Technique.**
Sebastien Hillaire (Epic Games).
Computer Graphics Forum, Vol. 39, No. 4, 2020, pp. 13-22.
DOI: 10.1111/cgf.14050 (Eurographics Symposium on Rendering 2020)

## Ozet

- **Problem**: Prior real-time atmosphere methods (Bruneton 2008, Wronski 2014) use high-dimensional LUTs (4D scattering tables) that are expensive to precompute, consume large GPU memory, produce banding artifacts from LUT resolution limits, and cannot update dynamically when atmosphere parameters change (e.g. weather or artistic overrides).
- **Yontem**: Replace the high-dimensional LUT with three small 2D/3D tables: (1) a 256x64 Transmittance LUT (precomputed once), (2) a 32x32 Multi-Scattering LUT (approximate secondary bounces via a fixed-point iteration), and (3) a 192x108x16 Sky-View LUT (updated every frame). Aerial perspective is rendered via a lightweight 32x32x32 camera-volume LUT. All LUTs fit in < 1 MB total. Dynamic update cost is < 0.5 ms on a mid-range GPU.
- **Veri / Setup**: Compared against Bruneton 2017 reference at Earth-like, Mars-like and fictional planet configurations. Visual results shown in Fig. 1-5 (pp. 13-17). Performance breakdown Table 1, p. 19: full sky + aerial perspective at 0.3 ms on RTX 2080.
- **Sonuc**: Production deployed in Fortnite (Unreal Engine 5 sky system). Handles ground-to-space views, multiple scattering, dynamic time-of-day, and per-planet customization without heavy precomputation. HLSL source released publicly (linked from DOI page).
- **Bizim calismaya baglanti**: Replaces the Wronski 2014 volumetric fog approach in the `cd_atmosphere` / `HelloAtmosphere` subsystem. The three-LUT architecture maps directly to our `RenderTarget` infra (Hillaire's LUTs are standard 2D/3D textures). Aerial perspective LUT slots into the existing composite pass. Hillaire's HLSL listings (Appendix A, pp. 21-22) can be ported to GLSL with trivial changes.
- **Sayfa referansi**: LUT architecture §3, p. 15. Multi-scattering approximation §3.2, p. 16. Sky-View LUT §3.3, p. 16-17. Aerial perspective §3.4, p. 17. Performance Table 1, p. 19.

## Supersedes

`wronski2014volfog` for the atmosphere/sky rendering path. Wronski remains valid for dense volumetric fog (fog particles, not Rayleigh/Mie atmosphere); the two techniques are complementary for different scattering regimes.

## Implementation status

`cd_atmosphere` library (new library, not yet scaffolded). Highest complexity of the four D-F9 papers: requires three new render targets, a compute pass chain, and integration into the composite pass. Estimated effort: 3-5 days for a correct implementation. Recommend adding after Kavan DQS and Fdez-Aguera (lower risk, higher payoff-per-day).
