# cd::camera

**Purpose**: scene-camera + controller library. Provides the camera data model (transform + perspective lens + frustum) plus drop-in controllers (orbit, FPS, scripted path) so samples + the editor avoid rolling their own input handling.

**Namespace**: `cd::camera`.

**Headers**: `cd/camera/{Camera,Lens,Frustum,OrbitController,FirstPersonController,CameraPath,ViewportInfo}.hpp`.

**Primary types**:
- `cd::camera::Camera` -- { position, forward, up, lens } aggregate + helpers (view_matrix, view_projection, screen_to_world_ray).
- `cd::camera::Lens` -- perspective spec (fov_y, aspect, near, far) -> projection matrix; reverse-Z enabled by default.
- `cd::camera::Frustum` -- 6-plane frustum derived from a camera; `contains(AABB)` + sphere tests for culling.
- `cd::camera::OrbitController` -- mouse-orbit + scroll-zoom around a target; the editor default scene navigation.
- `cd::camera::FirstPersonController` -- right-mouse-look + WASD; used by samples for free-fly debug.
- `cd::camera::CameraPath` -- keyframed cubic-spline camera animation for cutscenes / demo recordings.

**Usage**:
```cpp
#include <cd/camera/Camera.hpp>
#include <cd/camera/OrbitController.hpp>

cd::camera::Camera cam {};
cam.position = { 0, 1, 3 };
cd::camera::OrbitController orbit { cam, { 0, 0, 0 } };
orbit.handle_mouse_drag(dx, dy);
const auto vp = cam.view_projection(viewport.aspect());
```

**Test command**: `ctest --preset ninja-debug -R cd_test_camera --output-on-failure`.

**Notes**:
- hello_engine extracts its free-look state into `cd_sample::FreeLookState` (HelloAppState.hpp).
- Reverse-Z by default (kLess depth compare assumed in pipelines).
- Frustum culling is the foundation for cluster / virtual-geometry libraries.
