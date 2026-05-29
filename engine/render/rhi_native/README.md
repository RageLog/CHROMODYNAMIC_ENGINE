# cd::rhi_native

**Purpose**: Unified GPU backend dispatcher. Provides `cd::rhi::make_native_device()` which selects the best available GPU backend for the host platform at runtime, hiding backend selection complexity from the application layer.

**Namespace**: `cd::rhi_native`.

**Selection Logic**:
- **Windows**: Prefers D3D12 if available, falls back to Vulkan, then Metal stub.
- **macOS**: Prefers Metal, falls back to Vulkan.
- **Linux**: Vulkan (primary).
- Stubs (no-op factory) on platforms where the concrete backend is unavailable.

**Linked Backends**:
- `cd::rhi_vulkan` — Vulkan 1.3+ (always linked if available at configure time).
- `cd::rhi_d3d12` — Direct3D 12 (built on all platforms; stub on non-Windows).
- `cd::rhi_metal` — Metal (built on all platforms; stub on non-Apple).

**Public API**:
- `cd::rhi::make_native_device()` — Returns `std::unique_ptr<IDevice>` using the best backend for the host.

**Build**:
```bash
cmake --build --preset ninja-debug --target cd_rhi_native
ctest --preset ninja-debug -R rhi_native --output-on-failure
```

**Usage**:
```cpp
#include <cd/rhi/NativeDevice.hpp>
auto device = cd::rhi::make_native_device();
if (device) {
  // Render using the platform's best GPU backend
}
```

**Notes**:
- Each backend uses its own symbol loader (volk for Vulkan, OS SDK for D3D12/Metal).
- The dispatcher is lightweight; actual backend initialization happens only when the selected device is created.
- VMA vendoring in rhi_vulkan is excluded from install via EXCLUDE_FROM_INSTALL.
