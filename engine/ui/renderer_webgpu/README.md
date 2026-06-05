# cd::ui_renderer_webgpu

**Purpose**: Bridges `cd::ui::renderer::DrawBatcher` (pure CPU) to a WebGPU device (Dawn). Phase 5 of ADR-20260530-ui-widget-library (phase522). Mirrors `cd::ui_renderer_rhi` (Phase 1.2b / Vulkan) but targets the `WGPUDevice` / `WGPUCommandEncoder` API.

**Namespace**: `cd::ui::renderer_webgpu`.

**Headers**: `cd/ui/renderer_webgpu/Submitter.hpp`.

---

## Integration status

| State | How |
| ----- | --- |
| **STUB (auto-probe)** | `CD_ENABLE_WEBGPU=ON` (default since phase753). CMake probes for Dawn; falls back to stub if not found. Opaque handles, no-op GPU calls. All four build-only tests pass on every CI tier. |
| **REAL (requires Dawn)** | Run `vcpkg install "chromodynamic[webgpu]"`, then reconfigure. `find_package(Dawn)` resolves `Dawn::webgpu_cpp`; `CreateBuffer` / `Queue::WriteBuffer` paths activate. No extra CMake flag needed. |

Dawn is available in the project's vcpkg baseline (version `20251202.213730`, BSD-3-Clause) but is **not installed by default** because it requires Abseil + Python host tooling and adds ~800 MB to the build.

### Dawn vcpkg promote status (phase753)

Dawn remains in the `webgpu` **feature gate** in `vcpkg.json` (not promoted to top-level `dependencies`) for the following reasons:

| Constraint | Detail |
| ---------- | ------ |
| **Build size** | Abseil (~30 MB) + Python tooling (~150 MB) + Dawn source (~300 MB). D3D12 + Vulkan: **~800 MB** Release, **~1.4 GB** Debug+Release. |
| **Build time** | Cold build on 8-core Windows: **25–45 min**. Standard CI tiers would time out without a pre-cached Dawn sysroot. |
| **Platform deps** | D3D12 needs `directx-dxc`; Vulkan needs `vulkan-headers`. Both are auto-fetched by vcpkg — intrusive for all-developer installs. |
| **Promotion criteria** | Promote to top-level when: (a) self-hosted CI runner with cached Dawn sysroot is live, (b) `hello_ui_webgpu` Phase 5.5 requires Dawn unconditionally. |

**`CD_ENABLE_WEBGPU` is now ON by default** so configure always probes and reports the backend state. Developers with Dawn installed see "real WebGPU backend enabled"; others see "falling back to stub" — no hard error either way.

The `Submitter::create` factory is the **default-OK path**: it succeeds in both stub and real modes. `create_with_dawn` (future Phase 5.5 factory) will require an actual `wgpu::Device` and is intentionally deferred until the `hello_ui_webgpu` sample ships a real surface.

---

## API

```cpp
#include <cd/ui/renderer_webgpu/Submitter.hpp>

// --- Stub device (compile-time test / headless CI) ---
cd::ui::renderer_webgpu::WgpuDevice stub_dev {};  // opaque 0

// --- Real device (Dawn) ---
// wgpu::Device dawn_dev = adapter.CreateDevice(&desc);
// cd::ui::renderer_webgpu::WgpuDevice dev { ... };  // alias to wgpu::Device

cd::ui::renderer_webgpu::SubmitterCreateInfo info {};
info.max_vertices = 65535U;
info.max_indices  = 65535U * 6U;
info.debug_label  = "ui_main";

auto sub = cd::ui::renderer_webgpu::Submitter::create(stub_dev, info);
if (!sub) { /* handle error */ }

// per frame:
batcher.begin_frame();
// ... batcher.quad / textured_quad / glyph ...
if (!sub->upload(batcher)) { /* overflow */ }

cd::ui::renderer_webgpu::WgpuCommandEncoder enc {};  // from wgpu::Device::CreateCommandEncoder
sub->record(enc, { 1920U, 1080U });
```

---

## Primary type: `cd::ui::renderer_webgpu::Submitter`

Move-only owning handle.

| Method | Description |
|--------|-------------|
| `create(device, info)` | Allocates ring vb + ib (Dawn: `CreateBuffer`; stub: no-op). Returns `Result<Submitter>`. |
| `upload(batcher)` | Copies vertex/index spans (Dawn: `Queue::WriteBuffer`; stub: metadata only). Returns `false` on overflow. |
| `record(encoder, extent)` | Issues `SetVertexBuffer + SetIndexBuffer + DrawIndexed` per `DrawCommand` (Dawn); stub iterates only. |
| `destroy()` | Releases GPU buffers (Dawn: `Buffer::Destroy`). Idempotent; called automatically on destruction. |
| `is_valid()` | True between `create` and `destroy`. |
| `vertex_count() / index_count() / command_count()` | Per-frame stats from last `upload()`. |

---

## Lifecycle

```
Once at boot:  Submitter::create(device, info)   — vb + ib allocation
Per frame:     upload(batcher)                   — CPU → GPU copy
               record(encoder, extent)           — draw commands
At shutdown:   destroy() / dtor                  — GPU resource release
```

---

## Enabling real Dawn

```bash
# 1. Install Dawn via vcpkg
vcpkg install chromodynamic[webgpu]

# 2. Configure with the Dawn backend enabled
cmake --preset ninja-debug -DCD_ENABLE_WEBGPU=ON

# 3. Build + test
cmake --build --preset ninja-debug
ctest --preset ninja-debug -R cd_test_webgpu_submitter --output-on-failure
```

---

## Test command (stub — no GPU required)

```bash
ctest --preset ninja-debug -R cd_test_webgpu_submitter --output-on-failure
```

Covers 4 cases: `CreateReturnsValidHandle`, `UploadSignatureCompiles`, `RecordSignatureCompiles`, `DestroyIsIdempotent`.

---

## Out of Phase 5.0 scope (Phase 5.5 `hello_ui_webgpu` owns these)

- WGSL vertex + fragment shader modules compilation.
- Render-pass descriptor creation (colour attachment, load/store ops).
- Pipeline layout + bind group for the atlas texture.
- Push-constant (or uniform buffer) projection matrix upload.
- End-to-end correctness on a real WebGPU surface.

---

## Notes

- **Stub types**: In stub mode `WgpuDevice` and `WgpuCommandEncoder` are plain structs with a single `uintptr_t opaque` field. They compare equal via `operator==` (defaulted). No Dawn headers are needed.
- **Real types**: When `CD_UI_WEBGPU_HAVE_DAWN == 1`, both types resolve to their `wgpu::` counterparts via `webgpu/webgpu_cpp.h`. The public API is identical.
- **One atlas per submitter**: Same design as `cd::ui_renderer_rhi`. Multiple atlas textures = multiple Submitters. A bindless variant is Phase 5.2.
- **16-bit index**: Same as the RHI submitter; max 65 535 vertices per frame. Auto-split on overflow is Phase 5.2.
