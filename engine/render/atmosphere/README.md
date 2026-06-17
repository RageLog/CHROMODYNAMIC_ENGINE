# cd::atmosphere

**Purpose**: physically-based atmospheric scattering (Hillaire 2020). Single-scattering Rayleigh + Mie + multiple scattering LUT for daylight + golden-hour + sunset rendering. Used by the analytical sky material and the volumetric clouds composite.

**Namespace**: `cd::atmosphere`.

**Header**: `cd/atmosphere/Atmosphere.hpp` (header-only).

**Primary types + free functions**:
- `cd::atmosphere::Parameters` -- aggregate of altitude-dependent density profiles (Rayleigh, Mie, ozone) + Mie phase asymmetry; Hillaire-2020 Earth defaults.
- `cd::atmosphere::Lut2D` -- a CPU RGB float table (the offline-bake / test representation; production uploads RGBA16F).
- `bake_transmittance_lut(Parameters, width, height)` -- **implemented + tested** CPU bake of the view-ray transmittance LUT (40-step optical-depth integral). The matching GLSL compute kernel is `kTransmittanceCS`.
- `rayleigh_phase(cos)` / `henyey_greenstein(cos, g)` -- the two scattering phase functions.

**Test command**: `ctest --preset ninja-debug -R cd_test_atmosphere --output-on-failure`.

**LUT roadmap (Hillaire 2020, 4 LUTs; SEALED transmittance-v1)**:
1. **transmittance** (view-zenith x altitude) -- DONE (`bake_transmittance_lut` + `kTransmittanceCS`).
2. multi-scattering (zenith x altitude, isotropic 2nd+ bounce) -- promote-on-need.
3. sky-view (azimuth x zenith) -- promote-on-need.
4. aerial-perspective (screen xy x depth froxels) -- promote-on-need.
LUTs 2-4 + the RHI dispatch land when a renderer wires the full sky pipeline; see ADR-20260616-band4-render-features-scope §atmosphere for the trigger.

**Notes**:
- The transmittance LUT is CPU-baked at boot; future GPU bake queued for the on-disk hot-reload pipeline (X5).
- Atmospheric perspective feeds the analytical sky material (cd::material::AnalyticalSkyMaterial) once LUTs 2-4 land.
- hello_engine wires the sun direction through both atmosphere + IBL + shadow projection.
