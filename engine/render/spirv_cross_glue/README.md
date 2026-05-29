# cd::spirv_cross_glue

SPIR-V cross-compilation glue library (B-infra1). Wraps the [KhronosGroup/SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) C++ API to translate SPIR-V binary modules into HLSL (D3D12), MSL (Metal), or GLSL (round-trip / OpenGL).

## Purpose

The M4 D3D12 parity sprint requires Vulkan-style GLSL shaders to run on the D3D12 backend. The canonical pipeline is:

```text
GLSL source  →  cd::shader::GlslangCompiler  →  SPIR-V
SPIR-V       →  cd::spirv_cross_glue::translate()  →  HLSL
HLSL         →  DXC  →  DXIL  →  D3D12 runtime
```

The MSL output path exists to support the Apple Metal backend in a later sprint.

## Namespace

`cd::spirv_cross_glue`

## Public API

```cpp
// engine/render/spirv_cross_glue/include/cd/spirv_cross_glue/Translate.hpp

namespace cd::spirv_cross_glue {

enum class Target : std::uint8_t { kGlsl, kHlsl, kMsl };

struct TranslateResult {
    std::string source;  // non-empty on success
    std::string error;   // non-empty on failure
    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] TranslateResult translate(
    std::span<const std::uint32_t> spirv,
    Target                         target,
    std::uint32_t                  version = 0   // 0 = auto-pick
);

} // namespace cd::spirv_cross_glue
```

### `version` parameter

| Target | 0 (auto)        | Example explicit  |
|--------|-----------------|-------------------|
| kGlsl  | GLSL 450        | 460               |
| kHlsl  | SM 6.0 (60)     | 51 for SM 5.1     |
| kMsl   | MSL 2.2 (20200) | 30000 for MSL 3.0 |

## Usage Example

```cpp
#include <cd/shader/Compiler.hpp>
#include <cd/spirv_cross_glue/Translate.hpp>

// 1. Compile GLSL → SPIR-V
auto glslang = cd::shader::make_glslang_compiler();
cd::shader::CompileDesc desc;
desc.source = my_glsl_source;
desc.stage  = cd::shader::ShaderStage::kVertex;
auto spirv_result = glslang->compile(desc);
// ... check spirv_result.has_value()

// 2. Translate SPIR-V → HLSL
auto tr = cd::spirv_cross_glue::translate(
    std::span<const std::uint32_t>{ spirv_result->spirv },
    cd::spirv_cross_glue::Target::kHlsl
);
if (!tr.ok()) {
    // tr.error contains the SPIRV-Cross error message
}
// tr.source is ready for DXC
```

## CMake dependency

```cmake
target_link_libraries(my_target PRIVATE cd::spirv_cross_glue)
```

### DAG position

```text
cd::core  →  cd_spirv_cross_glue  (nothing higher in the engine DAG)
```

`cd_spirv_cross_glue` does NOT depend on `cd::shader`, `cd::rhi`, or any application layer. The caller is responsible for obtaining SPIR-V (e.g. via `cd::shader`).

## Vendor resolution

SPIRV-Cross is resolved via FetchContent at tag `vulkan-sdk-1.3.268.0` when vcpkg is disabled (`CD_DISABLE_VCPKG=ON`, the default for this project). When vcpkg is active the `spirv-cross` manifest entry drives `find_package(spirv_cross_core CONFIG)` resolution instead.

Only the GLSL, HLSL, and MSL backends are enabled; the C API, CPP emitter, reflection, and CLI are OFF to keep configure and build time lean.
