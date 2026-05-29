# cd::rhi_metal

**Purpose**: Apple Metal backend implementation for the GPU hardware abstraction layer (RHI). Supports macOS and iOS. Gated by `#if defined(__APPLE__)`; on other platforms, factory returns nullptr stub.

**Namespace**: `cd::rhi_metal`.

**Platform**: macOS / iOS. Non-Apple platforms compile a stub that returns nullptr from the factory.

**Key Dependencies**:
- `Metal.framework` — Apple GPU API (macOS 10.11+, iOS 8.0+).
- `Foundation.framework` — Core Objective-C runtime.

**Primary Types** (Internal; exposed via `cd::rhi::make_native_device()`):
- `MetalDevice` — concrete `IDevice` impl; wraps MTLDevice + MTLCommandQueue.
- Swapchain uses `CAMetalLayer` provided by the caller via `SwapchainDesc::window_handle`.

**Build**:
```bash
cmake --build --preset ninja-debug --target cd_rhi_metal
ctest --preset ninja-debug -R rhi_metal --output-on-failure
```

**Maturity**:
- Phase 9 Sprint 1: full implementation (PSO, draw, buffer/texture).
- Phase 8 Wave 79: public header + factory stub (compiles everywhere, boots on Apple only).

**Symbol Resolution**:
- Apple frameworks linked PRIVATE so the downstream link line stays clean.
- Off-Apple: stub .cpp compiles; no framework linkage needed.

**User Integration**:
The application supplies a `CAMetalLayer*` via `SwapchainDesc::window_handle` so Metal can render directly into the Cocoa / UIKit view hierarchy.

**Notes**:
- All four RHI backends (Vulkan, D3D12, Metal, OpenGL) share the same interface.
- Metal command encoding is thread-safe; rendering can be parallelized via command queue scheduling.
