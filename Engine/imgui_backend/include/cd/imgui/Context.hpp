// =============================================================================
// CHROMODYNAMIC — cd/imgui/Context.hpp
//
// Thin RAII wrapper around Dear ImGui's Vulkan + Win32 backends. Use:
//
//   cd::imgui::InitDesc d{...};
//   auto ctx = cd::imgui::Context::create(d);
//   while (...) {
//     for (auto& e : events) ctx->handle_event(e);
//     ctx->new_frame();
//     ImGui::ShowDemoWindow();
//     // ... user ImGui calls ...
//     // Inside the engine's render pass:
//     ctx->render(cmd);
//   }
//
// `create` takes engine-side handles (Window + IDevice + swapchain color
// format) and internally fetches the raw Vulkan handles via
// cd::rhi_vulkan::get_native. If the device is not a Vulkan backend
// (e.g. a future DX12 device) Create returns kBackendMismatch and the
// caller must use a DX12-specific ImGui wrapper.
//
// ImGui owns its descriptor pool + font texture + pipeline; we never
// alias those into the engine's resource registry.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/platform/Window.hpp>
#include <cd/rhi/Format.hpp>

#include <cstdint>
#include <memory>
#include <string_view>

namespace cd::rhi
{
class IDevice;
class ICommandBuffer;
}

namespace cd::imgui
{

namespace imgui_errors
{
inline constexpr std::uint32_t kDomain = 0x0014;

enum class Code : std::uint32_t
{
    kOk = 0,
    kBackendMismatch = 1,  ///< IDevice is not Vulkan-backed.
    kInitFailed = 2,
    kInvalidArgument = 3,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace imgui_errors

struct InitDesc
{
    cd::platform::IWindow* window { nullptr };
    cd::rhi::IDevice* device { nullptr };
    /// Color format the engine renders ImGui into (typically the swapchain
    /// format). ImGui needs this for dynamic-rendering pipeline creation.
    cd::rhi::Format color_format { cd::rhi::Format::kBGRA8Unorm };
    /// Number of swapchain images / frames-in-flight. Used to size ImGui's
    /// internal descriptor pool and per-frame command buffer fences.
    std::uint32_t frames_in_flight { 2 };
};

class Context
{
public:
    [[nodiscard]] static cd::core::Result<std::unique_ptr<Context>> create(const InitDesc& desc);

    ~Context();
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Context(Context&&) = delete;
    Context& operator=(Context&&) = delete;

    /// Forward a platform event to ImGui. Mouse, keyboard, scroll, resize
    /// are translated to ImGuiIO updates. Unknown event types are ignored.
    void handle_event(const cd::platform::OSEvent& event);

    /// Start a new ImGui frame. Call before user `ImGui::*` calls.
    void new_frame();

    /// Render ImGui draw data into the supplied command buffer. The
    /// caller is responsible for being inside an active dynamic-rendering
    /// pass that targets `InitDesc::color_format`.
    void render(cd::rhi::ICommandBuffer& cmd);

private:
    Context() noexcept = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cd::imgui
