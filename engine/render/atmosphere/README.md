# cd::atmosphere

**Purpose**: physically-based atmospheric scattering (Hillaire 2020). Single-scattering Rayleigh + Mie + multiple scattering LUT for daylight + golden-hour + sunset rendering. Used by the analytical sky material and the volumetric clouds composite.

**Namespace**: `cd::atmosphere`.

**Header**: `cd/atmosphere/Atmosphere.hpp` (header-only).

**Primary types + free functions**:
- `cd::atmosphere::Parameters` -- aggregate of altitude-dependent density profiles (Rayleigh, Mie, ozone) + Mie phase asymmetry; Hillaire-2020 Earth defaults.
- `cd::atmosphere::Lut2D` -- a CPU RGB float table (the offline-bake / test representation; production uploads RGBA16F).
- `bake_transmittance_lut(Parameters, width, height)` -- **implemented + tested** CPU bake of the view-ray transmittance LUT (40-step optical-depth integral). The matching GLSL compute kernel is `kTransmittanceCS`.
- `bake_multiscatter_lut(Parameters, transmittance, width, height)` -- **implemented + tested** CPU bake of the multiple-scattering LUT (Hillaire 2020 §5.3 / Eq. 10): 64 Fibonacci-sphere directions x 20-step single-scatter march, accumulating the `L_2nd` / `f_ms` pair and storing the geometric-series result `Psi = L_2nd / (1 - f_ms)`. Reads the sealed transmittance LUT read-only via `sample_transmittance`. The matching GLSL compute kernel is `kMultiScatterCS`. **OPT-IN** — no renderer bakes it today, so it cannot alter the default sky/golden frame.
- `bake_skyview_lut(Parameters, transmittance, multiscatter, sun_dir, view_altitude_km, width, height)` -- **implemented + tested** CPU bake of the sky-view LUT (Hillaire 2020 §5.4): for a fixed camera altitude + sun direction, a 30-step single-scatter view-ray march storing per-(azimuth, zenith) the total in-scattered luminance = phased direct sunlight (`rayleigh_phase` + `henyey_greenstein`) attenuated by the sealed transmittance LUT, plus the isotropic multiple-scatter term `Psi` from the sealed MS LUT. Both sealed LUTs are read read-only. The matching GLSL compute kernel is `kSkyViewCS`. **OPT-IN** — no renderer bakes it today, so it cannot alter the default sky/golden frame.
- `sample_transmittance(lut, Parameters, mu, altitude_km)` / `sample_multiscatter(lut, Parameters, mu_sun, altitude_km)` -- read-only nearest-texel fetches into a baked LUT (same parameterisation the matching baker uses), clamped to the grid.
- `rayleigh_phase(cos)` / `henyey_greenstein(cos, g)` -- the two scattering phase functions.

**Test command**: `ctest --preset ninja-debug -R cd_test_atmosphere --output-on-failure`.

**LUT roadmap (Hillaire 2020, 4 LUTs; SEALED transmittance-v1)**:

1. **transmittance** (view-zenith x altitude) -- DONE (`bake_transmittance_lut` + `kTransmittanceCS`).
2. **multi-scattering** (sun-zenith x altitude, isotropic 2nd+ bounce) -- DONE, opt-in (`bake_multiscatter_lut` + `kMultiScatterCS`); reads the transmittance LUT, off the default rendered path.
3. **sky-view** (azimuth x zenith) -- DONE, opt-in (`bake_skyview_lut` + `kSkyViewCS`); reads the transmittance + multi-scatter LUTs, off the default rendered path.
4. aerial-perspective (screen xy x depth froxels) -- **SEALED promote-on-need**: a 3D froxel LUT (image3D) that double-projects screen-space onto the volumetric-fog froxel grid (`cd::volumetric`); it only earns its keep with a live HDR-composite consumer that applies in-scatter vs. scene depth, plus a golden-image harness. See `docs/ADR/ADR-20260621-atmosphere-lut-scope.md`.

LUTs 1-3 are CPU-baked + tested. The aerial-perspective LUT + the GPU RHI dispatch land when a renderer wires the full sky pipeline; see `docs/ADR/ADR-20260621-atmosphere-lut-scope.md` (and the original `docs/ADR/ADR-20260616-band4-render-features-scope.md` §atmosphere) for the trigger.

**Notes**:
- The transmittance LUT is CPU-baked at boot; future GPU bake queued for the on-disk hot-reload pipeline (X5).
- Atmospheric perspective feeds the analytical sky material (cd::material::AnalyticalSkyMaterial) once the aerial-perspective LUT (LUT 4) + RHI dispatch land.
- hello_engine wires the sun direction through both atmosphere + IBL + shadow projection.
