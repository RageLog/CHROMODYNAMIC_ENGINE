# cd::rhi_opengl

**Purpose**: OpenGL 4.6 backend implementation for the GPU hardware abstraction layer (RHI). Targets legacy desktop applications and platforms where Vulkan/D3D12 are unavailable (older Linux drivers, industrial OS images). Boots with device enumeration; resource creation (buffer/texture/pipeline) follows incrementally.

**Namespace**: `cd::rhi_opengl`.

**Platform Support**:
- **Windows**: wgl (Windows OpenGL) via opengl32.lib + gdi32.lib.
- **macOS**: Apple OpenGL framework (agl).
- **Linux**: GLX / EGL via libGL.

**Primary Types** (Internal; exposed via `cd::rhi::make_native_device()`):
- `OpenGLDevice` — concrete `IDevice` impl; wraps OpenGL context + reports adapter info via glGetString.

**Build**:
```bash
cmake --build --preset ninja-debug --target cd_rhi_opengl
ctest --preset ninja-debug -R rhi_opengl --output-on-failure
```

**Maturity**:
- Phase 18.B: boot-only (device creation + context setup + adapter enumeration).
- Phase 18.C+: resource creation (buffer, texture, swapchain), PSO (vertex/fragment assembly).
- Symbol loading: v0.49.0 uses system loader; GLAD vendored loader joins in follow-up.

**Design Rationale**:
Per user direction (2026-05-24), OpenGL is the 3rd API priority behind Vulkan and D3D12. It unblocks legacy desktop integration and platforms where modern APIs lack reliable driver support.

**Notes**:
- All four RHI backends (Vulkan, D3D12, Metal, OpenGL) share the same `IDevice` / `ICommandBuffer` interface.
- OpenGL's immediate-mode rendering maps to deferred command recording via an internal CPU-side buffer.
