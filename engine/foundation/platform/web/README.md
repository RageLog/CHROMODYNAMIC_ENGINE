# CHROMODYNAMIC Web Platform (Emscripten + WebGPU)

**Phase 1 Design** — Stub for browser distribution via WebAssembly.

## Overview

This directory contains the **Emscripten integration layer** for CHROMODYNAMIC, enabling cross-compilation to WebAssembly (WASM) and browser execution via WebGPU.

- **Status**: Design-stage stub (T4.3 Phase 533)
- **Integration timeline**: T2.7 (WebGPU backend RHI) → T4.5 (full Emscripten integration, 4-6 weeks)
- **Related ADR**: [ADR-20260531-web-platform.md](../../docs/ADR/ADR-20260531-web-platform.md)

## Architecture

### Components

| File | Purpose |
|------|---------|
| `main_web.cpp` | Emscripten main loop stub (`emscripten_set_main_loop` wrapper); frame callback; deferred GPU initialization |
| `index.html` | Browser entry point; Canvas + WebGPU feature detection; WASM module loader (minimal styling) |
| `CMakeLists.txt` | Emscripten cross-compilation configuration; target `chromodynamic_web` executable |

### Build pipeline

```
Source (main_web.cpp) 
  ↓ [emcc/em++ compiler via CMake]
  ↓ [LLVM IR → WASM bytecode]
  ↓ [Emscripten linker → output.wasm + output.js]
  ↓
Artifacts: 
  - output.wasm (WebAssembly module)
  - output.js (Emscripten runtime + glue)
  - index.html (browser loader)
```

### Dependency chain

- **cd::core** — handle, log, memory
- **cd::platform** — base platform abstraction (Window, EventPump)
- **cd::math** — vectors, matrices
- **cd::rhi::webgpu** (post-T2.7) — WebGPU graphics API
- **Emscripten SDK** (≥3.1.51) — LLVM/Clang cross-compiler, browser bindings

## Building

### Prerequisites

1. **Emscripten SDK** installed:
   ```bash
   # Download from https://emscripten.org/docs/getting_started/downloads.html
   # Set EMSDK environment variable
   export EMSDK=/path/to/emsdk
   export EMSCRIPTEN=$EMSDK/upstream/emscripten
   ```

2. **CMake** 3.25+ (already required by project)

3. **Ninja** (cross-platform build tool, recommended)

### Configure

```bash
cd c:/UserFiles/Project/CHROMODYNAMIC_ENGINE
cmake --preset ninja-web-debug
```

This will:
- Detect Emscripten toolchain via CMakeModules/emscripten.cmake
- Configure `CD_PLATFORM_WEB=ON`
- Disable testing (WASM requires browser environment)
- Enable samples target

### Build

```bash
cmake --build --preset ninja-web-debug
```

Output:
- `build/ninja-web-debug/chromodynamic_web.wasm`
- `build/ninja-web-debug/chromodynamic_web.js`
- `build/ninja-web-debug/index.html`

### Deploy

Copy the three artifacts to a web server:

```bash
scp build/ninja-web-debug/{chromodynamic_web.wasm,chromodynamic_web.js,index.html} user@example.com:/var/www/html/
```

Open `https://example.com/index.html` in a modern browser (Chrome 113+, Firefox 123+, Safari 18.2+).

## Current Limitations (Phase 1 stub)

| Limitation | Reason | Resolution |
|---|---|---|
| No GPU rendering | Requires T2.7 WebGPU backend RHI | Stub tick loop only |
| No filesystem I/O | IDBFS mounting deferred | T4.5 integration |
| No audio | WebAudio bridge stub only | T4.5 sample app |
| Single-threaded | SharedArrayBuffer security policy | Phase 2: web worker pool |
| No editor integration | Phase 2 scope | T4.5+ feature |
| No hot-reload | Browser page reload cycle | Phase 2: websocket + delta updates |

## Roadmap

### T4.5 (Post-MVP, ~6 weeks from T2.7 completion)

- [ ] Integrate WebGPU backend (T2.7 RHI output)
- [ ] Mount IDBFS filesystem at `/data/`
- [ ] Implement WebAudio bridge (`cd::audio::WebAudioSink`)
- [ ] Create sample app: hello_webgpu (PBR sphere on Sponza)
- [ ] Asset manifest + etag cache validation
- [ ] Build time optimization (link-time optimization, -O3 defaults)

### Post-MVP (Phase 2)

- [ ] Browser-based editor (hot-reload, shader editing)
- [ ] Web worker thread pool (compute offload, asset loading)
- [ ] Networking: fetch() wrapper for HTTP/WebSocket asset streaming
- [ ] Progressive download + streaming (small initial .wasm, lazy modules)

## Testing

Phase 1 stub has **no automated tests** (browser environment required). Manual testing:

1. Build with `ninja-web-debug`
2. Open `build/ninja-web-debug/index.html` in browser
3. Check browser console (F12) for startup logs
4. Verify "WebAssembly loaded. Ready" message

Console should show:
```
[web] CHROMODYNAMIC Web Platform (Emscripten stub, Phase 1)
[web] Emscripten environment detected
[web] WebGPU support check (stub)
[web] Starting main loop (requestAnimationFrame mode)
[web] Frame 60
[web] Frame 120
...
```

## Dependencies

### Vendored

- **Emscripten SDK** (external; not vendored; user-installed)

### Post-T4.5 integrations

- `cd::rhi::webgpu` — WebGPU backend (T2.7)
- `cd::rhi::opengl_es` — WebGL 2.0 fallback (Phase 2)
- SDL2-Emscripten — window abstraction (optional; planned Phase 2)

## References

- **Emscripten documentation**: https://emscripten.org/
- **WebGPU W3C spec**: https://www.w3.org/TR/webgpu/
- **Dawn project**: https://dawn.googlesource.com/dawn
- **ADR-20260531**: Web platform architecture decision record

## Notes

- **Global state prohibition**: CLAUDE.md §7 forbids global state; `g_frame_loop` pointer in main_web.cpp is temporary Phase 1 design. Post-T4.5: replace with explicit context object passed to `cd::sample::SampleAppFramework`.
- **Toolchain flexibility**: `CMAKE_TOOLCHAIN_FILE` is configurable; users can override or provide custom Emscripten setup.
- **Cross-platform builds**: Desktop (Vulkan/D3D12) and Web builds coexist in the same CMake tree; setting `CD_PLATFORM_WEB=ON` is exclusive per preset.
- **Safari support**: WebGPU support in Safari is in tech preview (2024); fallback to WebGL 2.0 deferred to Phase 2.
