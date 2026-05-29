# cd::ibl

**Purpose**: image-based lighting bake pipeline. Equirect-to-cubemap projection, irradiance convolution, prefiltered specular roughness chain, BRDF integration LUT. CPU baker that the engine can run at boot to produce IBL inputs for any PBR pipeline.

**Namespace**: `cd::ibl`.

**Headers**: `cd/ibl/{Cubemap,EquirectToCube,IrradianceConvolution,PrefilteredSpecular,BrdfLut}.hpp`.

**Primary types**:
- `cd::ibl::CubeMapRgbF` -- 6-face cubemap of RGB f32 pixels (typical face = 128x128). Output of EquirectToCube + IrradianceConvolution.
- `cd::ibl::PrefilteredSpecularCube` -- multi-mip cubemap (typ. 6 mips, base 128) baked from a roughness sweep over the env map. Cook-Torrance prefilter sum (Karis).
- `cd::ibl::BrdfLut` -- 2D LUT (typ. 64x64, RG f16) storing the split-sum BRDF integral pre-computed across (NdotV, roughness).
- `cd::ibl::equirect_to_cube(...)`, `cd::ibl::convolve_irradiance(...)`, `cd::ibl::bake_prefiltered(...)`, `cd::ibl::bake_brdf_lut(...)` -- the four bake steps.

**Usage**:
```cpp
#include <cd/ibl/EquirectToCube.hpp>
#include <cd/ibl/IrradianceConvolution.hpp>
#include <cd/ibl/PrefilteredSpecular.hpp>
#include <cd/ibl/BrdfLut.hpp>

auto env = cd::ibl::equirect_to_cube(equirect_pixels, /*face_size=*/128);
auto diff = cd::ibl::convolve_irradiance(env, /*face_size=*/16, /*samples=*/16);
auto spec = cd::ibl::bake_prefiltered(env, /*base=*/128, /*mips=*/6, /*samples=*/32);
auto lut = cd::ibl::bake_brdf_lut(/*size=*/64, /*samples=*/256);
```

**Test command**: `ctest --preset ninja-debug -R cd_test_ibl --output-on-failure`.

**Notes**:
- CPU baker; companion library `cd::ibl_gpu` uploads results to RHI textures + samplers.
- hello_engine bakes IBL on a `WorkStealingThreadPool` boot graph (see HelloIbl.hpp / Marathon Run 11 phase N12).
- W8-AW chrome-mirror quality params: env 128, spec base 128 / 6 mips / 32 samples, diff 16 / 16 samples, BRDF 64x64 / 256 samples.
