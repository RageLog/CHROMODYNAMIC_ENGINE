// =============================================================================
// CHROMODYNAMIC — cd/imgui/ImGuiD3D12.hpp
//
// Thin include-facade for Dear ImGui's DirectX 12 renderer backend.
// Exposes `imgui_impl_dx12.h` from within the cd header tree so that
// engine consumers use a consistent include path:
//
//   #include <cd/imgui/ImGuiD3D12.hpp>
//
// instead of reaching into the imgui backends/ directory directly.
//
// Windows-only. Guard callers with #if defined(_WIN32).
//
// The DX12 backend is compiled into cd_imgui_backend.lib alongside the
// Vulkan and Win32 backends. Callers wanting a managed surface should use the
// RAII wrapper cd::imgui::D3D12Context (cd/imgui/D3D12Context.hpp); this facade
// remains for callers that drive ImGui_ImplDX12_Init / _NewFrame /
// _RenderDrawData / _Shutdown directly.
//
// -----------------------------------------------------------------------------
// STATUS — M4H delivered: the RAII wrapper now EXISTS and is device-tested.
//   This header is still a pure include-facade (no cd:: type of its own), but
//   the previously-deferred higher-level wrapper has landed:
//     * cd::imgui::D3D12Context (cd/imgui/D3D12Context.hpp + src/
//       ImGuiD3D12Context.cpp) — RAII over ImGui_ImplDX12_* + ImGui_ImplWin32,
//       fed by cd::rhi D3D12 native handles (cd/rhi/d3d12/NativeHandles.hpp),
//       owning its shader-visible SRV descriptor heap. Returns kBackendMismatch
//       on a non-D3D12 IDevice (mirrors the Vulkan cd::imgui::Context).
//     * cd_test_imgui_backend_d3d12_device — device-gated GPU-draw test that
//       drives D3D12Context::render(cmd) -> ImGui_ImplDX12_RenderDrawData onto a
//       REAL ID3D12GraphicsCommandList in an offscreen render pass, asserting
//       a non-empty draw (TotalVtxCount > 0). GTEST_SKIPs with no adapter/window.
//   Both the Vulkan AND D3D12 ImGui draw paths are now device-tested.
// -----------------------------------------------------------------------------
//
// Dear ImGui — MIT licence — Copyright (c) 2014-present Omar Cornut
// See https://github.com/ocornut/imgui for the full licence text.
// =============================================================================
#pragma once

#if !defined(_WIN32)
    #error "cd/imgui/ImGuiD3D12.hpp is a Windows-only header."
#endif

#include <backends/imgui_impl_dx12.h>
