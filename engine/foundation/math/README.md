# cd::math

**Purpose**: tier-0 linear algebra + small numerical helpers used by every renderer / asset / scene library.

**Namespace**: `cd::math`.

**Headers**: `cd/math/Vector.hpp`, `cd/math/Matrix.hpp`, `cd/math/Random.hpp`, plus small accessory headers (`Transform.hpp`, `Quaternion.hpp` where promoted).

**Primary types**:
- `cd::math::Vec2f`, `Vec3f`, `Vec4f` — column-major float vector PoDs.
- `cd::math::Mat3f`, `Mat4f` — column-major float matrix PoDs.
- `cd::math::Random` — small PCG32 RNG with float / Vec3 / quaternion samplers.
- `cd::math::to_mat4(Transform)` — Transform -> Mat4 column-major conversion (ADR-017 P4).

**Conventions**:
- Column-major. Matches GLSL default + Filament reference + cd::rhi descriptor layout. Vulkan AS row-major transpose handled in `cd::hello_engine::make_accel_instance` (HelloRayQuery.hpp).
- Right-handed; +Y up.
- Floats only at the math layer; double precision lives at the simulation layer when needed (none today).

**Usage example**:
```cpp
#include <cd/math/Matrix.hpp>
#include <cd/math/Vector.hpp>

const cd::math::Vec3f forward { 0.0F, 0.0F, -1.0F };
const auto rot = cd::math::Mat4f::rotation_y(0.5F);
const auto rotated = rot * cd::math::Vec4f { forward.x, forward.y, forward.z, 0.0F };
```

**Test command**: `ctest --preset ninja-debug -R cd_test_math --output-on-failure`.

**Notes**:
- Header-only.
- Marathon Run 11 phase B2 normalised the bugprone-misplaced-widening-cast pattern across the asset/Primitives helper.
- Future work: `cd::math::SimdVec4f` SIMD path tracked in ADR-005 foundation policy bundle.
