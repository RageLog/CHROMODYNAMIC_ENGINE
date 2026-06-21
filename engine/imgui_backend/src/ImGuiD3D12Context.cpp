// =============================================================================
// CHROMODYNAMIC — cd/imgui/ImGuiD3D12Context.cpp
//
// D3D12 mirror of Context.cpp (Vulkan). RAII over ImGui_ImplDX12_* + the
// ImGui_ImplWin32 input bridge. Fetches the raw D3D12 handles from a cd::rhi
// D3D12 IDevice via cd::rhi::d3d12::get_native; returns kBackendMismatch on a
// non-D3D12 device. Owns the shader-visible SRV descriptor heap the DX12
// backend needs for its font texture + 1.92 dynamic atlas.
// =============================================================================
#include <cd/imgui/D3D12Context.hpp>
#include <cd/imgui/InputMapping.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/d3d12/NativeHandles.hpp>

#include <imgui.h>

// IID_PPV_ARGS / __uuidof are MS-language extensions that clang-cl rejects
// under -Werror -Wlanguage-extension-token; silence for the D3D12 includes +
// the CreateDescriptorHeap call below (mirrors D3D12Device.cpp).
#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wlanguage-extension-token"
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgiformat.h>

#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_win32.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace cd::imgui
{

namespace
{

// Map cd::rhi::Format → DXGI_FORMAT for ImGui's D3D12 RTV format. Only the
// subset we actually use as a render target is required (mirrors the Vulkan
// Context's local to_vk_format).
[[nodiscard]] DXGI_FORMAT to_dxgi_format(cd::rhi::Format f) noexcept
{
    switch (f)
    {
        case cd::rhi::Format::kBGRA8Unorm:
            return DXGI_FORMAT_B8G8R8A8_UNORM;
        case cd::rhi::Format::kBGRA8Srgb:
            return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        case cd::rhi::Format::kRGBA8Unorm:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case cd::rhi::Format::kRGBA8Srgb:
            return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        default:
            return DXGI_FORMAT_B8G8R8A8_UNORM;
    }
}

// ImGui 1.92's DX12 backend allocates more than one SRV descriptor (font atlas
// + dynamic textures), so the heap must hold a small pool. 64 is the canonical
// "big enough for the editor" size from the imgui_impl_dx12 example.
constexpr std::uint32_t kSrvHeapSize = 64;

}  // namespace

struct D3D12Context::Impl
{
    cd::platform::IWindow* window { nullptr };
    cd::rhi::IDevice* device { nullptr };
    ID3D12Device* d3d_device { nullptr };
    ID3D12CommandQueue* d3d_queue { nullptr };
    ID3D12DescriptorHeap* srv_heap { nullptr };
    std::uint32_t srv_descriptor_size { 0 };
    // Free-list bitmap over the SRV heap slots, driven by the alloc/free
    // callbacks ImGui 1.92 requires. A bit set means the slot is in use.
    std::array<bool, kSrvHeapSize> srv_used {};

    [[nodiscard]] bool alloc_srv(D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu,
                                 D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu) noexcept
    {
        for (std::uint32_t i = 0; i < kSrvHeapSize; ++i)
        {
            if (srv_used.at(i))
                continue;
            srv_used.at(i) = true;
            const auto offset =
                static_cast<UINT64>(i) * static_cast<UINT64>(srv_descriptor_size);
            D3D12_CPU_DESCRIPTOR_HANDLE cpu =
                srv_heap->GetCPUDescriptorHandleForHeapStart();
            D3D12_GPU_DESCRIPTOR_HANDLE gpu =
                srv_heap->GetGPUDescriptorHandleForHeapStart();
            cpu.ptr += static_cast<SIZE_T>(offset);
            gpu.ptr += offset;
            *out_cpu = cpu;
            *out_gpu = gpu;
            return true;
        }
        return false;
    }

    void free_srv(D3D12_CPU_DESCRIPTOR_HANDLE cpu) noexcept
    {
        const SIZE_T base = srv_heap->GetCPUDescriptorHandleForHeapStart().ptr;
        if (cpu.ptr < base || srv_descriptor_size == 0)
            return;
        const auto idx =
            static_cast<std::uint32_t>((cpu.ptr - base) / srv_descriptor_size);
        if (idx < kSrvHeapSize)
            srv_used.at(idx) = false;
    }

    // ImGui 1.92 alloc/free callbacks. Static members so they can name the
    // private Impl type and form C-compatible function pointers.
    static void srv_alloc_cb(ImGui_ImplDX12_InitInfo* info,
                             D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu,
                             D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu)
    {
        auto* self = static_cast<Impl*>(info->UserData);
        // The heap is sized for the editor's worst case; an exhausted pool is a
        // misconfiguration, not a per-frame condition — leave the handles zero.
        (void) self->alloc_srv(out_cpu, out_gpu);
    }

    static void srv_free_cb(ImGui_ImplDX12_InitInfo* info,
                            D3D12_CPU_DESCRIPTOR_HANDLE cpu,
                            D3D12_GPU_DESCRIPTOR_HANDLE /*gpu*/)
    {
        static_cast<Impl*>(info->UserData)->free_srv(cpu);
    }

    ~Impl()
    {
        if (d3d_device != nullptr)
        {
            // Drain GPU work so RenderDrawData's per-frame resources are idle
            // before ImGui releases them — event-based wait via the device.
            if (device != nullptr)
                device->wait_idle();
            ImGui_ImplDX12_Shutdown();
            ImGui_ImplWin32_Shutdown();
            if (srv_heap != nullptr)
                srv_heap->Release();
            if (ImGui::GetCurrentContext() != nullptr)
                ImGui::DestroyContext();
        }
    }
};

cd::core::Result<std::unique_ptr<D3D12Context>>
D3D12Context::create(const D3D12InitDesc& desc)
{
    if (desc.window == nullptr || desc.device == nullptr)
    {
        return std::unexpected(
            imgui_errors::make(imgui_errors::Code::kInvalidArgument, "window or device is null"));
    }

    auto nh = cd::rhi::d3d12::get_native(*desc.device);
    if (!nh.has_value() || nh->device == nullptr || nh->graphics_queue == nullptr)
    {
        return std::unexpected(
            imgui_errors::make(imgui_errors::Code::kBackendMismatch, "IDevice is not D3D12-backed")
        );
    }

    // Shader-visible SRV descriptor heap ImGui's DX12 backend draws from.
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc {};
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.NumDescriptors = kSrvHeapSize;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ID3D12DescriptorHeap* srv_heap = nullptr;
    if (FAILED(nh->device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&srv_heap))) ||
        srv_heap == nullptr)
    {
        return std::unexpected(
            imgui_errors::make(imgui_errors::Code::kInitFailed, "CreateDescriptorHeap (SRV) failed"));
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Multi-viewport requires per-window swapchains we don't wire in v1; mask
    // it off so a future ImGui default change can't enable it behind our back.
    io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;

    auto impl = std::make_unique<D3D12Context::Impl>();
    impl->window = desc.window;
    impl->device = desc.device;
    impl->d3d_device = nh->device;
    impl->d3d_queue = nh->graphics_queue;
    impl->srv_heap = srv_heap;
    impl->srv_descriptor_size =
        nh->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    auto* hwnd = static_cast<HWND>(desc.window->native_window_handle());
    if (!ImGui_ImplWin32_Init(hwnd))
    {
        srv_heap->Release();
        ImGui::DestroyContext();
        return std::unexpected(
            imgui_errors::make(imgui_errors::Code::kInitFailed, "ImGui_ImplWin32_Init failed"));
    }
    // ImGui_ImplWin32_Init sets PlatformHasViewports unconditionally; we don't
    // wire viewport surfaces in v1 — clear it (pairs with the mask above).
    io.BackendFlags &= ~ImGuiBackendFlags_PlatformHasViewports;

    ImGui_ImplDX12_InitInfo init {};
    init.Device = impl->d3d_device;
    init.CommandQueue = impl->d3d_queue;
    init.NumFramesInFlight = static_cast<int>(desc.frames_in_flight);
    init.RTVFormat = to_dxgi_format(desc.color_format);
    init.DSVFormat = DXGI_FORMAT_UNKNOWN;
    init.UserData = impl.get();
    init.SrvDescriptorHeap = srv_heap;
    init.SrvDescriptorAllocFn = &D3D12Context::Impl::srv_alloc_cb;
    init.SrvDescriptorFreeFn = &D3D12Context::Impl::srv_free_cb;

    if (!ImGui_ImplDX12_Init(&init))
    {
        ImGui_ImplWin32_Shutdown();
        srv_heap->Release();
        ImGui::DestroyContext();
        return std::unexpected(
            imgui_errors::make(imgui_errors::Code::kInitFailed, "ImGui_ImplDX12_Init failed"));
    }

    // Font texture builds lazily on first NewFrame (ImGui 1.92 dynamic atlas +
    // RendererHasTextures). No manual Fonts->Build() / upload call is needed.

    auto ctx = std::unique_ptr<D3D12Context> { new D3D12Context };
    ctx->impl_ = std::move(impl);
    return ctx;
}

D3D12Context::~D3D12Context() = default;

void D3D12Context::handle_event(const cd::platform::OSEvent& event)
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
        case K::kTextChar:
            if (event.code_point != 0)
                io.AddInputCharacter(event.code_point);
            break;
        case K::kResize:
            io.DisplaySize = ImVec2 { static_cast<float>(event.width), static_cast<float>(event.height) };
            break;
        default:
            break;
    }
}

void D3D12Context::new_frame()
{
    if (impl_ == nullptr || ImGui::GetCurrentContext() == nullptr)
        return;
    if (impl_->window != nullptr)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize =
            ImVec2 { static_cast<float>(impl_->window->width()), static_cast<float>(impl_->window->height()) };
        if (io.DeltaTime <= 0.0F)
            io.DeltaTime = 1.0F / 60.0F;
    }
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void D3D12Context::render(cd::rhi::ICommandBuffer& cmd)
{
    if (impl_ == nullptr || ImGui::GetCurrentContext() == nullptr)
        return;
    ImGui::Render();
    auto* draw = ImGui::GetDrawData();
    if (draw == nullptr || draw->TotalVtxCount == 0)
        return;
    auto* list = cd::rhi::d3d12::get_native(cmd);
    if (list == nullptr)
        return;
    // ImGui_ImplDX12_RenderDrawData binds the font/atlas SRV via
    // SetGraphicsRootDescriptorTable into our heap, so the heap MUST be bound
    // on this command list first (the backend does not bind it itself).
    list->SetDescriptorHeaps(1, &impl_->srv_heap);
    ImGui_ImplDX12_RenderDrawData(draw, list);
}

}  // namespace cd::imgui

#if defined(__clang__)
    #pragma clang diagnostic pop
#endif
