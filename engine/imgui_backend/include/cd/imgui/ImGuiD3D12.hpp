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
// Vulkan and Win32 backends. Callers link cd::imgui_backend and call
// ImGui_ImplDX12_Init / ImGui_ImplDX12_NewFrame /
// ImGui_ImplDX12_RenderDrawData / ImGui_ImplDX12_Shutdown directly.
// A higher-level cd::imgui::D3D12Context RAII wrapper is deferred to
// the M4H sample phase.
//
// -----------------------------------------------------------------------------
// SEALED — thin/unverified, scope-deferred (band4 §imgui_backend).
//   This header is a pure include-facade: it adds NO cd:: type, NO RAII
//   wrapper, NO host-testable surface of its own. It exists only so engine
//   consumers reach the upstream DX12 backend through a stable cd/ include
//   path. There is intentionally no cd:: API to test here yet — the DX12
//   draw path is verified through the D3D12 backend's own pixel-parity
//   capstone, not through this facade. The RAII wrapper (and its tests)
//   land together in the M4H sample phase. Do not add a placeholder wrapper
//   or stub test ahead of that; this facade stays frozen until then.
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
