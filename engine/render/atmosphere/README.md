# cd::atmosphere

**Purpose**: physically-based atmospheric scattering (Hillaire 2020). Single-scattering Rayleigh + Mie + multiple scattering LUT for daylight + golden-hour + sunset rendering. Used by the analytical sky material and the volumetric clouds composite.

**Namespace**: `cd::atmosphere`.

**Headers**: `cd/atmosphere/{Atmosphere,TransmittanceLut,MultiScatteringLut,SkyViewLut}.hpp`.

**Primary types**:
- `cd::atmosphere::Atmosphere` -- aggregate of altitude-dependent density profiles (Rayleigh, Mie, ozone) + sun parameters.
- `cd::atmosphere::TransmittanceLut` -- 256x64 2D table of optical depths from any altitude to space.
- `cd::atmosphere::MultiScatteringLut` -- 32x32 isotropic multiple-scattering precompute.
- `cd::atmosphere::SkyViewLut` -- 192x108 sky-radiance LUT sampled by the sky material.

**Test command**: `ctest --preset ninja-debug -R cd_test_atmosphere --output-on-failure`.

**Notes**:
- LUTs are CPU-baked at boot; future GPU bake queued for the on-disk hot-reload pipeline (X5).
- Sun disk + atmospheric perspective both feed the analytical sky material (cd::material::AnalyticalSkyMaterial).
- hello_engine wires the sun direction through both atmosphere + IBL + shadow projection.
