# cd::ibl_gpu

**Purpose**: GPU upload + sampler binding for the IBL chain baked by cd::ibl. Promotes CPU CubeMapRgbF / BrdfLut into RHI textures with the correct mip layout, sampler addressing, and view types for shader binding.

**Namespace**: `cd::ibl_gpu`.

**Headers**: `cd/ibl_gpu/Upload.hpp`.

**Primary types**:
- `cd::ibl_gpu::GpuCubemap` -- { TextureHandle image, TextureViewHandle view } for env / diff / spec cubes.
- `cd::ibl_gpu::GpuLut2D` -- 2D texture + view pair for the BRDF integration LUT.
- `cd::ibl_gpu::upload_env(...)`, `upload_diff(...)`, `upload_spec(...)`, `upload_brdf_lut(...)` -- single-call uploaders that return populated GpuCubemap / GpuLut2D.

**Test command**: `ctest --preset ninja-debug -R cd_test_ibl_gpu --output-on-failure`.

**Notes**:
- Serial upload by design -- cd::rhi::IDevice is not documented as thread-safe today (see ADR-20260528).
- hello_engine couples cd::ibl + cd::ibl_gpu via `cd_sample::bake_ibl_cpu` + `cd_sample::upload_ibl_gpu` (HelloIbl.hpp / Marathon Run 11 phase N12).
- Specular cube has 6 mips by W8-AW chrome-mirror quality target.
