// =============================================================================
// CHROMODYNAMIC — cd/imgui/D3D12Context.hpp
//
// Thin RAII wrapper around Dear ImGui's DirectX 12 + Win32 backends. The D3D12
// mirror of cd::imgui::Context (Vulkan). Use:
//
//   cd::imgui::D3D12InitDesc d{...};
//   auto ctx = cd::imgui::D3D12Context::create(d);
//   while (...) {
//     for (auto& e : events) ctx->handle_event(e);
//     ctx->new_frame();
//     ImGui::ShowDemoWindow();
//     // ... user ImGui calls ...
//     // Inside the engine's render pass (RTV bound):
//     ctx->render(cmd);
//   }
//
// `create` takes engine-side handles (Window + cd::rhi D3D12 IDevice + render-
// target colour format + frames-in-flight) and internally fetches the raw
// D3D12 handles via cd::rhi::d3d12::get_native. If the device is not a D3D12
// backend (e.g. a Vulkan device) create returns kBackendMismatch and the
// caller must use the Vulkan cd::imgui::Context wrapper.
//
// This Context OWNS the shader-visible SRV descriptor heap ImGui's DX12 backend
// needs (font texture + dynamic atlas, ImGui 1.92 RendererHasTextures). It is
// never aliased into the engine's resource registry.
//
// Windows-only — guard callers with #if defined(_WIN32).
// =============================================================================
#pragma once

#if !defined(_WIN32)
    #error "cd/imgui/D3D12Context.hpp is a Windows-only header."
#endif

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/imgui/Context.hpp>  // InitDesc, imgui_errors
#include <cd/platform/Window.hpp>
#include <cd/rhi/Format.hpp>

#include <cstdint>
#include <memory>

namespace cd::rhi
{
class IDevice;
class ICommandBuffer;
}  // namespace cd::rhi

namespace cd::imgui
{

/// D3D12 init parameters. Shares the error domain + codes with the Vulkan
/// Context (cd::imgui::imgui_errors). Structurally identical to InitDesc; a
/// distinct type keeps the two backends' init contracts independent.
struct D3D12InitDesc
{
    cd::platform::IWindow* window { nullptr };
    cd::rhi::IDevice* device { nullptr };
    /// Colour format the engine renders ImGui into (typically the swapchain
    /// format). ImGui needs this for its D3D12 pipeline's RTV format.
    cd::rhi::Format color_format { cd::rhi::Format::kBGRA8Unorm };
    /// Number of swapchain images / frames-in-flight. Sizes ImGui's per-frame
    /// vertex/index buffers.
    std::uint32_t frames_in_flight { 2 };
};

class D3D12Context
{
public:
    [[nodiscard]] static cd::core::Result<std::unique_ptr<D3D12Context>>
    create(const D3D12InitDesc& desc);

    ~D3D12Context();
    D3D12Context(const D3D12Context&) = delete;
    D3D12Context& operator=(const D3D12Context&) = delete;
    D3D12Context(D3D12Context&&) = delete;
    D3D12Context& operator=(D3D12Context&&) = delete;

    /// Forward a platform event to ImGui. Mouse, keyboard, scroll, resize are
    /// translated to ImGuiIO updates. Unknown event types are ignored.
    void handle_event(const cd::platform::OSEvent& event);

    /// Start a new ImGui frame. Call before user `ImGui::*` calls.
    void new_frame();

    /// Render ImGui draw data into the supplied command buffer. The caller is
    /// responsible for being inside an active render pass that targets
    /// `D3D12InitDesc::color_format` (RTV bound via begin_render_pass). This
    /// binds the owned SRV descriptor heap on the command list before issuing
    /// ImGui_ImplDX12_RenderDrawData.
    void render(cd::rhi::ICommandBuffer& cmd);

private:
    D3D12Context() noexcept = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cd::imgui
