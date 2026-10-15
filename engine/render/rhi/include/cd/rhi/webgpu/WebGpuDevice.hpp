// =============================================================================
// CHROMODYNAMIC — cd/rhi/webgpu/WebGpuDevice.hpp
// FAZ 9 Wave 9.2 — WebGPU (Dawn & Emscripten WebAssembly) RHI Backend Parity.
//
// Provides `cd::rhi::webgpu::create_webgpu_device(WebGpuCreateInfo)` backed by
// Dawn (desktop) or Emscripten (`navigator.gpu` / `emscripten_webgpu_get_device`),
// plus GLSL/SPIR-V -> WGSL shader module validation and full resource lifecycle
// parity across Buffer, Texture, Sampler, BindGroup, RenderPipeline,
// ComputePipeline, Swapchain (Canvas Context), and Readback.
// =============================================================================
#pragma once

#include <cd/core/Result.hpp>
#include <cd/rhi/IDevice.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace cd::rhi::webgpu
{

enum class WebGpuRuntimeKind : std::uint8_t
{
    kDawnDesktop = 0,
    kEmscriptenBrowser = 1,
};

struct WebGpuCreateInfo
{
    std::string app_name { "CHROMODYNAMIC" };
    std::string canvas_selector { "#canvas" };
    WebGpuRuntimeKind runtime { WebGpuRuntimeKind::kDawnDesktop };
    bool enable_timestamp_query { true };
    bool enable_float32_filterable { true };
};

/// Translate or wrap a GLSL/WGSL shader snippet into a valid WGSL entry point
/// string for `wgpuDeviceCreateShaderModule`.
[[nodiscard]] std::string translate_to_wgsl_source(
    std::string_view input_source,
    cd::rhi::ShaderStage stage
);

/// Create a WebGPU-backed `cd::rhi::IDevice` instance (Dawn or Emscripten).
[[nodiscard]] cd::core::Result<std::unique_ptr<cd::rhi::IDevice>>
create_webgpu_device(WebGpuCreateInfo info = {});

}  // namespace cd::rhi::webgpu
