# cd::post_dof

## Purpose
Depth of field post-process using Sousa 2013 hexagonal bokeh algorithm. Produces smooth, physically plausible out-of-focus blur by simulating camera aperture via stochastic sampling on accumulation frames or via hand-tuned hexagonal kernel blurs.

## Namespace
`cd::<render>::post_dof::`

## Public headers
- `include/cd/post_dof/Dof.hpp` — Aperture parameters and bokeh kernel evaluation
- `include/cd/post_dof/BlurPass.hpp` — Separable blur configuration

## Primary types
- `PostDof::ApertureParams` — Focus distance, f-number, blade count (aperture shape)
- `PostDof::BlurPass` — Kernel radius schedule, temporal sample count

## Usage example
```cpp
#include <cd/post_dof/Dof.hpp>

// Configure camera DOF.
cd::post_dof::ApertureParams aperture{
  .focus_distance = 5.0f,
  .f_number = 2.0f,  // larger = shallower DOF
  .blade_count = 6   // hexagon
};

// Apply post-process (via RHI framegraph).
// Output: color with depth-based bokeh blur.
```

## Build/Test
```bash
cmake --build --preset ninja-debug --target cd_post_dof
ctest --preset ninja-debug -R post_dof
```

## Dependencies
- `cd::core` — engine types
- `cd::math` — vector/matrix math

## References
- **Sousa 2013**, "Crysis 3 Graphics Gems: Depth-of-Field" (SIGGRAPH)
- Post-process pipeline: cd::post_composite

## Notes
- Header-only library.
- Hexagonal kernel approximates circular aperture efficiently.
- Supports focus bracketing (variable focus distance per frame) for focus peaking in editor.
- Pairs with cd::post_camera for full camera post-process chain.
