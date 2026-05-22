// =============================================================================
// CHROMODYNAMIC — cd/imgui/Context.cpp
// =============================================================================
#include <cd/imgui/Context.hpp>

#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi_vulkan/NativeHandles.hpp>

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

#if defined(_WIN32)
// imgui_impl_win32 expects raw Win32 messages. We only own the HWND right
// now and translate OSEvent to ImGuiIO updates directly — no native message
// pump hook. This loses things like IME composition (acceptable for a v1).
// `NOMINMAX` is project-wide on MSYS2 GCC's toolchain (via CDStandardSettings),
// so guard the define to avoid -Werror=macro-redefined.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <backends/imgui_impl_win32.h>
#endif

#include <cstring>
#include <memory>

namespace cd::imgui
{

namespace
{

// Map cd::rhi::Format → VkFormat for ImGui's dynamic-rendering pipeline.
// Only the subset we actually use as render target is required here.
[[nodiscard]] VkFormat to_vk_format(cd::rhi::Format f) noexcept
{
    switch (f)
    {
        case cd::rhi::Format::kBGRA8Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
        case cd::rhi::Format::kBGRA8Srgb: return VK_FORMAT_B8G8R8A8_SRGB;
        case cd::rhi::Format::kRGBA8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
        case cd::rhi::Format::kRGBA8Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
        default: return VK_FORMAT_B8G8R8A8_UNORM;
    }
}

[[nodiscard]] ImGuiKey to_imgui_key(cd::platform::KeyCode k) noexcept
{
    using K = cd::platform::KeyCode;
    switch (k)
    {
        case K::kEscape: return ImGuiKey_Escape;
        case K::kEnter: return ImGuiKey_Enter;
        case K::kSpace: return ImGuiKey_Space;
        case K::kTab: return ImGuiKey_Tab;
        case K::kBackspace: return ImGuiKey_Backspace;
        case K::kLeft: return ImGuiKey_LeftArrow;
        case K::kRight: return ImGuiKey_RightArrow;
        case K::kUp: return ImGuiKey_UpArrow;
        case K::kDown: return ImGuiKey_DownArrow;
        default: return ImGuiKey_None;
    }
}

[[nodiscard]] int to_imgui_mouse_button(cd::platform::MouseButton b) noexcept
{
    using M = cd::platform::MouseButton;
    switch (b)
    {
        case M::kLeft: return 0;
        case M::kRight: return 1;
        case M::kMiddle: return 2;
        default: return 0;
    }
}

}  // namespace

struct Context::Impl
{
    cd::platform::IWindow* window { nullptr };
    cd::rhi::IDevice* device { nullptr };
    cd::rhi_vulkan::NativeHandles handles {};
    VkDescriptorPool desc_pool { VK_NULL_HANDLE };
    VkFormat color_format { VK_FORMAT_B8G8R8A8_UNORM };

    ~Impl()
    {
        if (handles.device != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(handles.device);
            ImGui_ImplVulkan_Shutdown();
#if defined(_WIN32)
            ImGui_ImplWin32_Shutdown();
#endif
            if (desc_pool != VK_NULL_HANDLE)
                vkDestroyDescriptorPool(handles.device, desc_pool, nullptr);
            if (ImGui::GetCurrentContext() != nullptr)
                ImGui::DestroyContext();
        }
    }
};

cd::core::Result<std::unique_ptr<Context>> Context::create(const InitDesc& desc)
{
    if (desc.window == nullptr || desc.device == nullptr)
    {
        return std::unexpected(imgui_errors::make(imgui_errors::Code::kInvalidArgument, "window or device is null"));
    }

    auto nh = cd::rhi_vulkan::get_native(*desc.device);
    if (!nh.has_value())
    {
        return std::unexpected(
            imgui_errors::make(imgui_errors::Code::kBackendMismatch, "IDevice is not Vulkan-backed")
        );
    }

    // Dear ImGui's Vulkan backend wants its own descriptor pool. Sizes are
    // the canonical "big enough" defaults from imgui_impl_vulkan example.
    constexpr VkDescriptorPoolSize kPoolSizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
    };
    VkDescriptorPoolCreateInfo pool_ci {};
    pool_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_ci.maxSets = 1000;
    pool_ci.poolSizeCount = static_cast<std::uint32_t>(std::size(kPoolSizes));
    pool_ci.pPoolSizes = kPoolSizes;

    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(nh->device, &pool_ci, nullptr, &pool) != VK_SUCCESS)
    {
        return std::unexpected(imgui_errors::make(imgui_errors::Code::kInitFailed, "vkCreateDescriptorPool failed"));
    }

    // Boot ImGui core. The application can later call ImGui::StyleColorsDark
    // / ImGui::GetIO().ConfigFlags etc.; we set a neutral default.
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Multi-viewport requires a Platform_CreateVkSurface handler we do
    // not provide in v1 — it would mean letting cd::platform create
    // additional native windows on the fly, which is a sprint of its
    // own. Explicitly mask it off so a future ImGui default change can't
    // re-enable it behind our back and trigger the
    //   imgui_impl_vulkan.cpp:2351
    //   "Platform needs to setup the CreateVkSurface handler"
    // assert.
    io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
    // Docking is available in this branch but disabled by default —
    // applications opt in if they want it.

    auto impl = std::make_unique<Context::Impl>();
    impl->window = desc.window;
    impl->device = desc.device;
    impl->handles = *nh;
    impl->desc_pool = pool;
    impl->color_format = to_vk_format(desc.color_format);

#if defined(_WIN32)
    auto* hwnd = static_cast<HWND>(desc.window->native_window_handle());
    if (!ImGui_ImplWin32_Init(hwnd))
    {
        vkDestroyDescriptorPool(nh->device, pool, nullptr);
        ImGui::DestroyContext();
        return std::unexpected(imgui_errors::make(imgui_errors::Code::kInitFailed, "ImGui_ImplWin32_Init failed"));
    }
    // ImGui_ImplWin32_Init sets BackendFlags_PlatformHasViewports
    // unconditionally. ImGui_ImplVulkan_Init then asserts that
    // Platform_CreateVkSurface is set when that flag is on. We don't
    // wire viewport surfaces in v1 — clear the flag so the assert is
    // satisfied. (Pairs with the ViewportsEnable mask above.)
    io.BackendFlags &= ~ImGuiBackendFlags_PlatformHasViewports;
#endif

    // Dynamic-rendering setup — the engine doesn't expose a VkRenderPass
    // because Renderer uses VK_KHR_dynamic_rendering. ImGui v1.90+ supports
    // this via PipelineRenderingCreateInfo.
    VkPipelineRenderingCreateInfoKHR pri {};
    pri.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    pri.colorAttachmentCount = 1;
    pri.pColorAttachmentFormats = &impl->color_format;

    ImGui_ImplVulkan_InitInfo init {};
    init.Instance = nh->instance;
    init.PhysicalDevice = nh->physical_device;
    init.Device = nh->device;
    init.QueueFamily = nh->graphics_family;
    init.Queue = nh->graphics_queue;
    init.DescriptorPool = pool;
    init.MinImageCount = desc.frames_in_flight;
    init.ImageCount = desc.frames_in_flight;
    init.UseDynamicRendering = true;
    // Since 2025/09/26 the ImGui docking branch moved MSAA / RenderPass /
    // PipelineRenderingCreateInfo into the nested PipelineInfoMain struct.
    init.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init.PipelineInfoMain.PipelineRenderingCreateInfo = pri;
    init.PipelineInfoMain.RenderPass = VK_NULL_HANDLE;
    // UseDynamicRendering only lives on the outer InitInfo — the nested
    // PipelineInfo struct picks it up via that flag.

    if (!ImGui_ImplVulkan_Init(&init))
    {
#if defined(_WIN32)
        ImGui_ImplWin32_Shutdown();
#endif
        vkDestroyDescriptorPool(nh->device, pool, nullptr);
        ImGui::DestroyContext();
        return std::unexpected(imgui_errors::make(imgui_errors::Code::kInitFailed, "ImGui_ImplVulkan_Init failed"));
    }

    // Font texture is now created lazily on first NewFrame in v1.90+
    // (CreateFontsTexture happens internally). No-op here.

    auto ctx = std::unique_ptr<Context> { new Context };
    ctx->impl_ = std::move(impl);
    return ctx;
}

Context::~Context() = default;

void Context::handle_event(const cd::platform::OSEvent& event)
{
    if (impl_ == nullptr || ImGui::GetCurrentContext() == nullptr)
        return;
    ImGuiIO& io = ImGui::GetIO();
    using K = cd::platform::OSEventKind;
    switch (event.kind)
    {
        case K::kMouseMove:
            io.AddMousePosEvent(event.mouse_x, event.mouse_y);
            break;
        case K::kMouseButtonDown:
            io.AddMouseButtonEvent(to_imgui_mouse_button(event.mouse_button), true);
            break;
        case K::kMouseButtonUp:
            io.AddMouseButtonEvent(to_imgui_mouse_button(event.mouse_button), false);
            break;
        case K::kMouseWheel:
            io.AddMouseWheelEvent(0.0F, event.wheel);
            break;
        case K::kKeyDown:
        {
            const auto k = to_imgui_key(event.key);
            if (k != ImGuiKey_None)
                io.AddKeyEvent(k, true);
        }
        break;
        case K::kKeyUp:
        {
            const auto k = to_imgui_key(event.key);
            if (k != ImGuiKey_None)
                io.AddKeyEvent(k, false);
        }
        break;
        case K::kResize:
            io.DisplaySize = ImVec2 { static_cast<float>(event.width), static_cast<float>(event.height) };
            break;
        default:
            break;
    }
}

void Context::new_frame()
{
    if (impl_ == nullptr || ImGui::GetCurrentContext() == nullptr)
        return;
    // Update display size from the current window — handle_event keeps it
    // in sync, but on the very first frame we may not have seen a resize.
    if (impl_->window != nullptr)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2 { static_cast<float>(impl_->window->width()),
                                  static_cast<float>(impl_->window->height()) };
        // ImGui needs a non-zero DeltaTime; use a fixed 60 fps tick when
        // the host has no clock yet. Callers wanting precise CPU/GPU
        // graphs can extend Context to accept dt as an argument.
        if (io.DeltaTime <= 0.0F)
            io.DeltaTime = 1.0F / 60.0F;
    }
    ImGui_ImplVulkan_NewFrame();
#if defined(_WIN32)
    ImGui_ImplWin32_NewFrame();
#endif
    ImGui::NewFrame();
}

void Context::render(cd::rhi::ICommandBuffer& cmd)
{
    if (impl_ == nullptr || ImGui::GetCurrentContext() == nullptr)
        return;
    ImGui::Render();
    auto* draw = ImGui::GetDrawData();
    if (draw == nullptr || draw->TotalVtxCount == 0)
        return;
    auto vk_cmd = cd::rhi_vulkan::get_native(cmd);
    if (vk_cmd == VK_NULL_HANDLE)
        return;
    ImGui_ImplVulkan_RenderDrawData(draw, vk_cmd);
}

}  // namespace cd::imgui
