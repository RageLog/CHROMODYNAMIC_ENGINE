// =============================================================================
// CHROMODYNAMIC — engine/render/rhi_d3d12/src/D3D12Device.cpp
//
// Phase 12.B / v0.27.0 — D3D12 boot-only backend. Creates an
// ID3D12Device + DXGIFactory + adapter selection + direct command
// queue + a fence so wait_idle() works. The rest of the IDevice
// surface (50 virtuals total) is stubbed to return kNotImplemented;
// subsequent v0.27.x patches will fill in PSO, draw, buffer/texture
// upload, swapchain, present.
//
// This commit ships "the D3D12 path is reachable, the device + queue
// exist on this machine, adapter introspection works". That's the
// smallest defensible milestone for a backend the marathon author
// cannot rely on a human to pixel-check.
// =============================================================================
#include <cd/rhi_d3d12/D3D12Device.hpp>

#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/ICommandBuffer.hpp>

#if defined(_WIN32)
    // Win32 platform headers first so D3D12 + DXGI macros resolve cleanly.
    // The samples already define WIN32_LEAN_AND_MEAN + NOMINMAX globally;
    // we mirror that here so this TU is independent of include order.
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    // IID_PPV_ARGS + __uuidof are MS-language extensions; clang-cl under
    // -Werror -Wlanguage-extension-token rejects them. This is the only
    // legitimate D3D12 entry point in the SDK, so we silence the warning
    // for the duration of these system includes.
    #if defined(__clang__)
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wlanguage-extension-token"
    #endif
    #include <windows.h>
    #include <wrl/client.h>
    #include <d3d12.h>
    #include <dxgi1_6.h>
    #include <dxgidebug.h>
#endif

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cd::rhi_d3d12
{

#if defined(_WIN32)

namespace
{

using Microsoft::WRL::ComPtr;

// ---- Format mapping -----------------------------------------------------
//
// Phase 13.C v0.32.0 — only the subset that `hello_d3d12_clear` and a
// future MVP renderer actually need is wired. Adding the rest of the
// table is one obvious-shape line per entry; deferred until a sample
// actually requires the format.
[[nodiscard]] DXGI_FORMAT to_dxgi_format(cd::rhi::Format f) noexcept
{
    using F = cd::rhi::Format;
    switch (f)
    {
        case F::kRGBA8Unorm:  return DXGI_FORMAT_R8G8B8A8_UNORM;
        case F::kRGBA8Srgb:   return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case F::kBGRA8Unorm:  return DXGI_FORMAT_B8G8R8A8_UNORM;
        case F::kBGRA8Srgb:   return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        case F::kRGBA16Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case F::kRGBA32Float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case F::kD32Float:    return DXGI_FORMAT_D32_FLOAT;
        case F::kD24UnormS8Uint: return DXGI_FORMAT_D24_UNORM_S8_UINT;
        default:              return DXGI_FORMAT_UNKNOWN;
    }
}

// Pick the D3D12 heap type from the engine's MemoryUsage hint.
// DEFAULT  ← kGpuOnly (device-local, GPU-visible only)
// UPLOAD   ← kCpuToGpu (write-combine, host-visible)
// READBACK ← kGpuToCpu (cached, host-visible)
// For kAuto on a buffer we pick UPLOAD so the common "small constant
// buffer / staging" case works without a separate transfer; a future
// optimization can route bulk vertex/index buffers through a staging
// upload + DEFAULT residency.
[[nodiscard]] D3D12_HEAP_TYPE heap_type_for(cd::rhi::MemoryUsage m) noexcept
{
    using M = cd::rhi::MemoryUsage;
    switch (m)
    {
        case M::kGpuOnly:         return D3D12_HEAP_TYPE_DEFAULT;
        case M::kCpuToGpu:        return D3D12_HEAP_TYPE_UPLOAD;
        case M::kGpuToCpu:        return D3D12_HEAP_TYPE_READBACK;
        case M::kCpuRandomAccess: return D3D12_HEAP_TYPE_UPLOAD;
        case M::kAuto:            return D3D12_HEAP_TYPE_UPLOAD;
    }
    return D3D12_HEAP_TYPE_UPLOAD;
}

/// Phase 13.C v0.32.0 D3D12 backend: buffer + texture + swapchain wired,
/// plus a minimal command-list path sufficient for `hello_d3d12_clear`.
/// Other resource types (PSO, descriptor sets, shaders) remain stubbed
/// and surface kNotImplemented.
class D3D12Device final : public cd::rhi::IDevice
{
public:
    D3D12Device() noexcept = default;

    [[nodiscard]] HRESULT initialize(const D3D12CreateInfo& info)
    {
        // ---- 1. Debug layer (Debug builds only) ----------------------------
        if (info.enable_validation)
        {
            ComPtr<ID3D12Debug> debug;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
            {
                debug->EnableDebugLayer();
            }
        }

        // ---- 2. DXGI factory + adapter selection ---------------------------
        UINT factory_flags = 0;
        if (info.enable_validation)
            factory_flags |= DXGI_CREATE_FACTORY_DEBUG;

        HRESULT hr = CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&factory_));
        if (FAILED(hr))
            return hr;

        // Walk adapters in GPU-preference order; pick the first that
        // satisfies our min_feature_level. The high-performance preference
        // skips integrated when a discrete is present, matching the
        // prefer_discrete_gpu flag's intent.
        const DXGI_GPU_PREFERENCE pref =
            info.prefer_discrete_gpu ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE
                                     : DXGI_GPU_PREFERENCE_UNSPECIFIED;
        ComPtr<IDXGIAdapter4> adapter;
        for (UINT i = 0; ; ++i)
        {
            ComPtr<IDXGIAdapter1> next;
            hr = factory_->EnumAdapterByGpuPreference(i, pref, IID_PPV_ARGS(&next));
            if (hr == DXGI_ERROR_NOT_FOUND)
                break;
            if (FAILED(hr))
                return hr;

            DXGI_ADAPTER_DESC1 desc {};
            next->GetDesc1(&desc);
            if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
                continue;  // skip WARP unless explicitly requested

            // Probe whether this adapter can create a D3D12 device at the
            // requested feature level. We pass nullptr to skip actual
            // device creation; we'll create the real one below once chosen.
            const D3D_FEATURE_LEVEL fl =
                static_cast<D3D_FEATURE_LEVEL>(info.min_feature_level);
            hr = D3D12CreateDevice(next.Get(), fl, _uuidof(ID3D12Device), nullptr);
            if (SUCCEEDED(hr))
            {
                if (FAILED(next.As(&adapter)))
                    continue;
                adapter_desc_ = desc;
                break;
            }
        }
        if (!adapter)
            return DXGI_ERROR_NOT_FOUND;

        // ---- 3. The actual device ------------------------------------------
        hr = D3D12CreateDevice(
            adapter.Get(),
            static_cast<D3D_FEATURE_LEVEL>(info.min_feature_level),
            IID_PPV_ARGS(&device_)
        );
        if (FAILED(hr))
            return hr;

        // ---- 4. Direct (graphics) queue ------------------------------------
        D3D12_COMMAND_QUEUE_DESC qd {};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        qd.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        qd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        qd.NodeMask = 0;
        hr = device_->CreateCommandQueue(&qd, IID_PPV_ARGS(&graphics_queue_));
        if (FAILED(hr))
            return hr;

        // ---- 5. Fence for wait_idle() --------------------------------------
        hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&idle_fence_));
        if (FAILED(hr))
            return hr;
        idle_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (idle_event_ == nullptr)
            return HRESULT_FROM_WIN32(GetLastError());

        // Cache the WCHAR adapter name as UTF-8 for adapter_name().
        char utf8[256] {};
        const int n = WideCharToMultiByte(
            CP_UTF8, 0,
            adapter_desc_.Description, -1,
            utf8, static_cast<int>(sizeof(utf8) - 1),
            nullptr, nullptr
        );
        if (n > 0)
            adapter_name_.assign(utf8, static_cast<std::size_t>(n - 1));
        else
            adapter_name_ = "D3D12 adapter (unknown)";

        return S_OK;
    }

    ~D3D12Device() override
    {
        // wait_idle so the queue drains before we tear down the queue.
        if (device_ && graphics_queue_ && idle_fence_)
            (void)wait_idle_internal();
        if (idle_event_ != nullptr)
            CloseHandle(idle_event_);
    }

    // ---- Introspection (REAL) ---------------------------------------------

    [[nodiscard]] cd::rhi::Backend backend() const noexcept override
    {
        return cd::rhi::Backend::kD3D12;
    }

    [[nodiscard]] std::string_view adapter_name() const noexcept override
    {
        return adapter_name_;
    }

    [[nodiscard]] const cd::rhi::DeviceLimits& limits() const noexcept override
    {
        return limits_;
    }

    [[nodiscard]] const cd::rhi::DeviceFeatures& features() const noexcept override
    {
        return features_;
    }

    // ---- wait_idle (REAL) -------------------------------------------------

    void wait_idle() override
    {
        (void)wait_idle_internal();
    }

    // ---- Buffer (REAL — Phase 13.C v0.32.0) -------------------------------

    [[nodiscard]] cd::core::Result<cd::rhi::BufferHandle>
    create_buffer(const cd::rhi::BufferDesc& desc) override
    {
        if (desc.size == 0)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument, "buffer size == 0"));
        }
        D3D12_HEAP_PROPERTIES hp {};
        hp.Type = heap_type_for(desc.memory);
        hp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        hp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        hp.CreationNodeMask = 1;
        hp.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC rd {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Alignment = 0;
        rd.Width = desc.size;
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_UNKNOWN;
        rd.SampleDesc.Count = 1;
        rd.SampleDesc.Quality = 0;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        rd.Flags = D3D12_RESOURCE_FLAG_NONE;
        if ((static_cast<std::uint32_t>(desc.usage) &
             static_cast<std::uint32_t>(cd::rhi::BufferUsage::kStorage)) != 0)
            rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        // UPLOAD/READBACK heaps require GENERIC_READ / COPY_DEST initial
        // state respectively per the D3D12 spec; DEFAULT heap allows any
        // initial state but COMMON is the safe portable choice.
        D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON;
        if (hp.Type == D3D12_HEAP_TYPE_UPLOAD)
            initial_state = D3D12_RESOURCE_STATE_GENERIC_READ;
        else if (hp.Type == D3D12_HEAP_TYPE_READBACK)
            initial_state = D3D12_RESOURCE_STATE_COPY_DEST;

        ComPtr<ID3D12Resource> res;
        HRESULT hr = device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, initial_state, nullptr,
            IID_PPV_ARGS(&res));
        if (FAILED(hr))
        {
            char msg[160] {};
            std::snprintf(msg, sizeof(msg),
                          "CreateCommittedResource(buffer) failed: HRESULT 0x%08lx",
                          static_cast<unsigned long>(hr));
            return std::unexpected(cd::rhi::rhi_errors::make_owning(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                std::string { msg }));
        }
        const auto id = next_id_++;
        BufferRecord rec;
        rec.resource = res;
        rec.size = desc.size;
        rec.heap_type = hp.Type;
        buffers_.emplace(id, std::move(rec));
        return cd::rhi::BufferHandle { id, 1u };
    }

    void destroy_buffer(cd::rhi::BufferHandle h) override
    {
        buffers_.erase(h.index());
    }

    // ---- Texture (REAL — Phase 13.C v0.32.0) ------------------------------

    [[nodiscard]] cd::core::Result<cd::rhi::TextureHandle>
    create_texture(const cd::rhi::TextureDesc& desc) override
    {
        if (desc.extent.width == 0 || desc.extent.height == 0)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument, "texture extent has zero dimension"));
        }
        if (desc.type != cd::rhi::TextureType::k2D)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kNotImplemented,
                "D3D12 backend currently supports k2D textures only (v0.32.0)"));
        }
        const DXGI_FORMAT fmt = to_dxgi_format(desc.format);
        if (fmt == DXGI_FORMAT_UNKNOWN)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "unsupported texture format for D3D12 backend"));
        }

        D3D12_HEAP_PROPERTIES hp {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        hp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        hp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        hp.CreationNodeMask = 1;
        hp.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC rd {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Alignment = 0;
        rd.Width = desc.extent.width;
        rd.Height = desc.extent.height;
        rd.DepthOrArraySize = static_cast<UINT16>(desc.array_layers);
        rd.MipLevels = static_cast<UINT16>(desc.mip_levels);
        rd.Format = fmt;
        rd.SampleDesc.Count = 1;
        rd.SampleDesc.Quality = 0;
        rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        rd.Flags = D3D12_RESOURCE_FLAG_NONE;
        const auto u = static_cast<std::uint32_t>(desc.usage);
        if ((u & static_cast<std::uint32_t>(cd::rhi::TextureUsage::kColorAttachment)) != 0)
            rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if ((u & static_cast<std::uint32_t>(cd::rhi::TextureUsage::kDepthStencilAttachment)) != 0)
            rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        if ((u & static_cast<std::uint32_t>(cd::rhi::TextureUsage::kStorage)) != 0)
            rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        ComPtr<ID3D12Resource> res;
        HRESULT hr = device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&res));
        if (FAILED(hr))
        {
            char msg[160] {};
            std::snprintf(msg, sizeof(msg),
                          "CreateCommittedResource(texture) failed: HRESULT 0x%08lx",
                          static_cast<unsigned long>(hr));
            return std::unexpected(cd::rhi::rhi_errors::make_owning(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                std::string { msg }));
        }
        const auto id = next_id_++;
        TextureRecord rec;
        rec.resource = res;
        rec.format = fmt;
        rec.extent = desc.extent;
        rec.usage = desc.usage;
        textures_.emplace(id, std::move(rec));
        return cd::rhi::TextureHandle { id, 1u };
    }

    void destroy_texture(cd::rhi::TextureHandle h) override
    {
        textures_.erase(h.index());
    }

    // ---- Everything else: stubbed (Phase 13.C — out of scope) -------------

#define CD_D3D12_NOT_IMPL_RESULT(rt)                                              \
    return std::unexpected(cd::rhi::rhi_errors::make(                             \
        cd::rhi::rhi_errors::Code::kNotImplemented,                               \
        "D3D12 backend at v0.32.0 ships buffer/texture/swapchain only; "          \
        "this entry point lands in a later phase"))

    [[nodiscard]] cd::core::Result<cd::rhi::TextureViewHandle>
    create_texture_view(const cd::rhi::TextureViewDesc&) override { CD_D3D12_NOT_IMPL_RESULT(TextureViewHandle); }
    void destroy_texture_view(cd::rhi::TextureViewHandle) override {}

    [[nodiscard]] cd::core::Result<cd::rhi::SamplerHandle>
    create_sampler(const cd::rhi::SamplerDesc&) override { CD_D3D12_NOT_IMPL_RESULT(SamplerHandle); }
    void destroy_sampler(cd::rhi::SamplerHandle) override {}

    [[nodiscard]] cd::core::Result<cd::rhi::ShaderModuleHandle>
    create_shader_module(const cd::rhi::ShaderModuleDesc&) override { CD_D3D12_NOT_IMPL_RESULT(ShaderModuleHandle); }
    void destroy_shader_module(cd::rhi::ShaderModuleHandle) override {}

    [[nodiscard]] cd::core::Result<cd::rhi::DescriptorSetLayoutHandle>
    create_descriptor_set_layout(const cd::rhi::DescriptorSetLayoutDesc&) override { CD_D3D12_NOT_IMPL_RESULT(DescriptorSetLayoutHandle); }
    void destroy_descriptor_set_layout(cd::rhi::DescriptorSetLayoutHandle) override {}

    [[nodiscard]] cd::core::Result<cd::rhi::PipelineLayoutHandle>
    create_pipeline_layout(const cd::rhi::PipelineLayoutDesc&) override { CD_D3D12_NOT_IMPL_RESULT(PipelineLayoutHandle); }
    void destroy_pipeline_layout(cd::rhi::PipelineLayoutHandle) override {}

    [[nodiscard]] cd::core::Result<cd::rhi::GraphicsPipelineHandle>
    create_graphics_pipeline(const cd::rhi::GraphicsPipelineDesc&) override { CD_D3D12_NOT_IMPL_RESULT(GraphicsPipelineHandle); }
    void destroy_graphics_pipeline(cd::rhi::GraphicsPipelineHandle) override {}

    [[nodiscard]] cd::core::Result<cd::rhi::ComputePipelineHandle>
    create_compute_pipeline(const cd::rhi::ComputePipelineDesc&) override { CD_D3D12_NOT_IMPL_RESULT(ComputePipelineHandle); }
    void destroy_compute_pipeline(cd::rhi::ComputePipelineHandle) override {}

    [[nodiscard]] cd::core::Result<cd::rhi::DescriptorSetHandle>
    allocate_descriptor_set(cd::rhi::DescriptorSetLayoutHandle) override { CD_D3D12_NOT_IMPL_RESULT(DescriptorSetHandle); }
    void destroy_descriptor_set(cd::rhi::DescriptorSetHandle) override {}

    [[nodiscard]] cd::core::Result<void>
    update_descriptor_set(cd::rhi::DescriptorSetHandle, std::span<const cd::rhi::DescriptorWrite>) override
    {
        return std::unexpected(cd::rhi::rhi_errors::make(
            cd::rhi::rhi_errors::Code::kNotImplemented,
            "D3D12 backend is boot-only at v0.27.0; this entry point lands in v0.27.x"));
    }

    [[nodiscard]] cd::core::Result<cd::rhi::SemaphoreHandle>
    create_semaphore() override { CD_D3D12_NOT_IMPL_RESULT(SemaphoreHandle); }
    void destroy_semaphore(cd::rhi::SemaphoreHandle) override {}

    [[nodiscard]] cd::core::Result<cd::rhi::FenceHandle>
    create_fence(bool) override { CD_D3D12_NOT_IMPL_RESULT(FenceHandle); }
    void destroy_fence(cd::rhi::FenceHandle) override {}

    [[nodiscard]] cd::core::Result<void>
    wait_for_fence(cd::rhi::FenceHandle, std::uint64_t) override
    {
        return std::unexpected(cd::rhi::rhi_errors::make(
            cd::rhi::rhi_errors::Code::kNotImplemented,
            "D3D12 backend is boot-only at v0.27.0"));
    }

    void reset_fence(cd::rhi::FenceHandle) override {}
    [[nodiscard]] bool is_fence_signaled(cd::rhi::FenceHandle) override { return false; }

    [[nodiscard]] cd::core::Result<cd::rhi::TimelineSemaphoreHandle>
    create_timeline_semaphore(std::uint64_t) override { CD_D3D12_NOT_IMPL_RESULT(TimelineSemaphoreHandle); }
    void destroy_timeline_semaphore(cd::rhi::TimelineSemaphoreHandle) override {}

    [[nodiscard]] cd::core::Result<void>
    wait_timeline_semaphore(cd::rhi::TimelineSemaphoreHandle, std::uint64_t, std::uint64_t) override
    {
        return std::unexpected(cd::rhi::rhi_errors::make(
            cd::rhi::rhi_errors::Code::kNotImplemented,
            "D3D12 backend is boot-only at v0.27.0"));
    }

    [[nodiscard]] cd::core::Result<void>
    signal_timeline_semaphore(cd::rhi::TimelineSemaphoreHandle, std::uint64_t) override
    {
        return std::unexpected(cd::rhi::rhi_errors::make(
            cd::rhi::rhi_errors::Code::kNotImplemented,
            "D3D12 backend is boot-only at v0.27.0"));
    }

    [[nodiscard]] std::uint64_t timeline_semaphore_value(cd::rhi::TimelineSemaphoreHandle) const override
    {
        return 0;
    }

    // ---- acquire / present (REAL — Phase 13.C v0.32.0) --------------------

    [[nodiscard]] cd::core::Result<std::uint32_t>
    acquire_next_image(cd::rhi::SwapchainHandle h, cd::rhi::SemaphoreHandle, cd::rhi::FenceHandle, std::uint64_t) override
    {
        auto it = swapchains_.find(h.index());
        if (it == swapchains_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument, "acquire_next_image: unknown swapchain"));
        }
        // DXGI flip-model: GetCurrentBackBufferIndex returns the index
        // the *next* Present will publish. No semaphore signaling — D3D12
        // synchronization is driven by command-queue fences, not by
        // Vulkan-style binary semaphores. The semaphore/fence parameters
        // are accepted for interface parity and ignored.
        return it->second.swap->GetCurrentBackBufferIndex();
    }

    [[nodiscard]] cd::core::Result<void>
    present(cd::rhi::SwapchainHandle h, std::uint32_t, std::span<const cd::rhi::SemaphoreHandle>) override
    {
        auto it = swapchains_.find(h.index());
        if (it == swapchains_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument, "present: unknown swapchain"));
        }
        const UINT sync_interval = it->second.vsync ? 1u : 0u;
        HRESULT hr = it->second.swap->Present(sync_interval, 0);
        if (FAILED(hr))
        {
            char msg[160] {};
            std::snprintf(msg, sizeof(msg),
                          "IDXGISwapChain3::Present failed: HRESULT 0x%08lx",
                          static_cast<unsigned long>(hr));
            return std::unexpected(cd::rhi::rhi_errors::make_owning(
                cd::rhi::rhi_errors::Code::kDeviceLost,
                std::string { msg }));
        }
        return {};
    }

    // ---- Swapchain (REAL — Phase 13.C v0.32.0) ----------------------------

    [[nodiscard]] cd::core::Result<cd::rhi::SwapchainHandle>
    create_swapchain(const cd::rhi::SwapchainDesc& desc) override
    {
        if (desc.window_handle == nullptr)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "SwapchainDesc.window_handle is null"));
        }
        const DXGI_FORMAT fmt = to_dxgi_format(desc.format);
        if (fmt == DXGI_FORMAT_UNKNOWN)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "swapchain format unsupported"));
        }

        // DXGI flip-model can't be used with SRGB back-buffer formats —
        // create the swapchain with the linear sibling and let the
        // rendering layer pick an SRGB RTV. The caller's intent is
        // preserved by tracking `desc.format` in the SwapchainRecord
        // so RTV creation can honor it.
        DXGI_FORMAT swap_fmt = fmt;
        if (swap_fmt == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
            swap_fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
        else if (swap_fmt == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
            swap_fmt = DXGI_FORMAT_B8G8R8A8_UNORM;

        DXGI_SWAP_CHAIN_DESC1 sd {};
        sd.Width = desc.extent.width;
        sd.Height = desc.extent.height;
        sd.Format = swap_fmt;
        sd.Stereo = FALSE;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = desc.image_count;
        sd.Scaling = DXGI_SCALING_STRETCH;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        sd.Flags = 0;

        ComPtr<IDXGISwapChain1> chain1;
        HRESULT hr = factory_->CreateSwapChainForHwnd(
            graphics_queue_.Get(),
            static_cast<HWND>(desc.window_handle),
            &sd, nullptr, nullptr, &chain1);
        if (FAILED(hr))
        {
            char msg[160] {};
            std::snprintf(msg, sizeof(msg),
                          "CreateSwapChainForHwnd failed: HRESULT 0x%08lx",
                          static_cast<unsigned long>(hr));
            return std::unexpected(cd::rhi::rhi_errors::make_owning(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                std::string { msg }));
        }
        ComPtr<IDXGISwapChain3> chain3;
        hr = chain1.As(&chain3);
        if (FAILED(hr))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "IDXGISwapChain3 QueryInterface failed"));
        }

        SwapchainRecord rec;
        rec.swap = chain3;
        rec.format = fmt;
        rec.swap_format = swap_fmt;
        rec.extent = desc.extent;
        rec.vsync = desc.vsync;
        rec.images.resize(desc.image_count);
        rec.image_handles.resize(desc.image_count);
        rec.image_view_handles.resize(desc.image_count);

        // Build a single CPU-visible RTV heap to expose RTV CPU descriptors
        // for the back-buffers. The dxgi sample backbuffer images become
        // engine-side TextureHandles + TextureViewHandles so consumer code
        // can hand them to the trivial clear command-list path.
        D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc {};
        rtv_heap_desc.NumDescriptors = desc.image_count;
        rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        hr = device_->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rec.rtv_heap));
        if (FAILED(hr))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "RTV descriptor heap creation failed"));
        }
        rec.rtv_descriptor_size =
            device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv_cpu = rec.rtv_heap->GetCPUDescriptorHandleForHeapStart();
        for (std::uint32_t i = 0; i < desc.image_count; ++i)
        {
            ComPtr<ID3D12Resource> back;
            hr = chain3->GetBuffer(i, IID_PPV_ARGS(&back));
            if (FAILED(hr))
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                    "IDXGISwapChain3::GetBuffer failed"));
            }
            rec.images[i] = back;

            // The RTV format is the user-requested format (potentially
            // SRGB); the underlying back-buffer is the linear sibling.
            // D3D12 supports this mismatch via DXGI_FORMAT_*_SRGB RTVs
            // over UNORM resources for flip-model swapchains.
            D3D12_RENDER_TARGET_VIEW_DESC rtv_desc {};
            rtv_desc.Format = fmt;
            rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            device_->CreateRenderTargetView(back.Get(), &rtv_desc, rtv_cpu);

            // Register the back-buffer as an engine TextureHandle so
            // sample code can hand it to the command-buffer clear path.
            const auto tex_id = next_id_++;
            TextureRecord trec;
            trec.resource = back;
            trec.format = fmt;
            trec.extent = { desc.extent.width, desc.extent.height, 1 };
            trec.usage = cd::rhi::TextureUsage::kColorAttachment;
            trec.is_swapchain_image = true;
            trec.rtv_cpu = rtv_cpu;
            textures_.emplace(tex_id, std::move(trec));
            rec.image_handles[i] = cd::rhi::TextureHandle { tex_id, 1u };

            const auto view_id = next_id_++;
            TextureViewRecord vrec;
            vrec.parent = rec.image_handles[i];
            vrec.format = fmt;
            vrec.rtv_cpu = rtv_cpu;
            texture_views_.emplace(view_id, std::move(vrec));
            rec.image_view_handles[i] = cd::rhi::TextureViewHandle { view_id, 1u };

            rtv_cpu.ptr += rec.rtv_descriptor_size;
        }

        const auto id = next_id_++;
        swapchains_.emplace(id, std::move(rec));
        return cd::rhi::SwapchainHandle { id, 1u };
    }

    void destroy_swapchain(cd::rhi::SwapchainHandle h) override
    {
        auto it = swapchains_.find(h.index());
        if (it == swapchains_.end())
            return;
        for (auto th : it->second.image_handles)
            textures_.erase(th.index());
        for (auto vh : it->second.image_view_handles)
            texture_views_.erase(vh.index());
        swapchains_.erase(it);
    }

    [[nodiscard]] cd::rhi::TextureViewHandle swapchain_image_view(cd::rhi::SwapchainHandle h, std::uint32_t i) const override
    {
        auto it = swapchains_.find(h.index());
        if (it == swapchains_.end() || i >= it->second.image_view_handles.size())
            return {};
        return it->second.image_view_handles[i];
    }
    [[nodiscard]] std::uint32_t swapchain_image_count(cd::rhi::SwapchainHandle h) const override
    {
        auto it = swapchains_.find(h.index());
        return it == swapchains_.end() ? 0u : static_cast<std::uint32_t>(it->second.images.size());
    }
    [[nodiscard]] cd::rhi::TextureHandle swapchain_image(cd::rhi::SwapchainHandle h, std::uint32_t i) const override
    {
        auto it = swapchains_.find(h.index());
        if (it == swapchains_.end() || i >= it->second.image_handles.size())
            return {};
        return it->second.image_handles[i];
    }

    // ---- Buffer upload / download (REAL — Phase 13.C v0.32.0) -------------

    [[nodiscard]] cd::core::Result<void>
    upload_buffer(cd::rhi::BufferHandle h, std::uint64_t offset, std::span<const std::byte> data) override
    {
        auto it = buffers_.find(h.index());
        if (it == buffers_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument, "upload_buffer: unknown buffer handle"));
        }
        const auto& rec = it->second;
        if (offset + data.size() > rec.size)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument, "upload_buffer: range out of bounds"));
        }
        if (rec.heap_type != D3D12_HEAP_TYPE_UPLOAD)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "upload_buffer: target heap is not UPLOAD (staging path lands in a later phase)"));
        }
        void* mapped = nullptr;
        const D3D12_RANGE no_read { 0, 0 };
        HRESULT hr = rec.resource->Map(0, &no_read, &mapped);
        if (FAILED(hr) || mapped == nullptr)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed, "ID3D12Resource::Map failed"));
        }
        std::memcpy(static_cast<std::byte*>(mapped) + offset, data.data(), data.size());
        rec.resource->Unmap(0, nullptr);
        return {};
    }

    [[nodiscard]] cd::core::Result<void>
    download_buffer(cd::rhi::BufferHandle h, std::uint64_t offset, std::span<std::byte> data) override
    {
        auto it = buffers_.find(h.index());
        if (it == buffers_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument, "download_buffer: unknown buffer handle"));
        }
        const auto& rec = it->second;
        if (offset + data.size() > rec.size)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument, "download_buffer: range out of bounds"));
        }
        if (rec.heap_type != D3D12_HEAP_TYPE_READBACK)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "download_buffer: target heap is not READBACK (staging path lands in a later phase)"));
        }
        void* mapped = nullptr;
        const D3D12_RANGE read_range { 0, rec.size };
        HRESULT hr = rec.resource->Map(0, &read_range, &mapped);
        if (FAILED(hr) || mapped == nullptr)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed, "ID3D12Resource::Map failed"));
        }
        std::memcpy(data.data(), static_cast<const std::byte*>(mapped) + offset, data.size());
        const D3D12_RANGE no_write { 0, 0 };
        rec.resource->Unmap(0, &no_write);
        return {};
    }

    [[nodiscard]] std::unique_ptr<cd::rhi::ICommandBuffer>
    create_command_buffer(cd::rhi::QueueType) override;  // defined out-of-line below

    void submit(cd::rhi::ICommandBuffer& cb) override;   // defined out-of-line below

    [[nodiscard]] cd::core::Result<void>
    submit(const cd::rhi::SubmitDesc&) override
    {
        return std::unexpected(cd::rhi::rhi_errors::make(
            cd::rhi::rhi_errors::Code::kNotImplemented,
            "D3D12 SubmitDesc path lands with the PSO surface (post-13.C)"));
    }

#undef CD_D3D12_NOT_IMPL_RESULT

    // ---- Resource records (Phase 13.C) ------------------------------------

    struct BufferRecord
    {
        ComPtr<ID3D12Resource> resource;
        std::uint64_t size { 0 };
        D3D12_HEAP_TYPE heap_type { D3D12_HEAP_TYPE_UPLOAD };
    };

    struct TextureRecord
    {
        ComPtr<ID3D12Resource> resource;
        DXGI_FORMAT format { DXGI_FORMAT_UNKNOWN };
        cd::rhi::Extent3D extent {};
        cd::rhi::TextureUsage usage { cd::rhi::TextureUsage::kNone };
        bool is_swapchain_image { false };
        D3D12_CPU_DESCRIPTOR_HANDLE rtv_cpu {};
        D3D12_RESOURCE_STATES state { D3D12_RESOURCE_STATE_COMMON };
    };

    struct TextureViewRecord
    {
        cd::rhi::TextureHandle parent {};
        DXGI_FORMAT format { DXGI_FORMAT_UNKNOWN };
        D3D12_CPU_DESCRIPTOR_HANDLE rtv_cpu {};
    };

    struct SwapchainRecord
    {
        ComPtr<IDXGISwapChain3> swap;
        DXGI_FORMAT format { DXGI_FORMAT_UNKNOWN };
        DXGI_FORMAT swap_format { DXGI_FORMAT_UNKNOWN };
        cd::rhi::Extent2D extent {};
        bool vsync { true };
        std::vector<ComPtr<ID3D12Resource>> images;
        std::vector<cd::rhi::TextureHandle> image_handles;
        std::vector<cd::rhi::TextureViewHandle> image_view_handles;
        ComPtr<ID3D12DescriptorHeap> rtv_heap;
        UINT rtv_descriptor_size { 0 };
    };

    [[nodiscard]] ID3D12Device* native_device() noexcept { return device_.Get(); }
    [[nodiscard]] ID3D12CommandQueue* native_queue() noexcept { return graphics_queue_.Get(); }
    [[nodiscard]] TextureRecord* find_texture(cd::rhi::TextureHandle h) noexcept
    {
        auto it = textures_.find(h.index());
        return it == textures_.end() ? nullptr : &it->second;
    }
    [[nodiscard]] TextureViewRecord* find_texture_view(cd::rhi::TextureViewHandle h) noexcept
    {
        auto it = texture_views_.find(h.index());
        return it == texture_views_.end() ? nullptr : &it->second;
    }

private:
    [[nodiscard]] HRESULT wait_idle_internal()
    {
        const UINT64 sig = ++idle_value_;
        HRESULT hr = graphics_queue_->Signal(idle_fence_.Get(), sig);
        if (FAILED(hr))
            return hr;
        if (idle_fence_->GetCompletedValue() < sig)
        {
            hr = idle_fence_->SetEventOnCompletion(sig, idle_event_);
            if (FAILED(hr))
                return hr;
            WaitForSingleObject(idle_event_, INFINITE);
        }
        return S_OK;
    }

    ComPtr<IDXGIFactory6> factory_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> graphics_queue_;
    ComPtr<ID3D12Fence> idle_fence_;
    UINT64 idle_value_ { 0 };
    HANDLE idle_event_ { nullptr };
    DXGI_ADAPTER_DESC1 adapter_desc_ {};
    std::string adapter_name_;

    cd::rhi::DeviceLimits limits_ {};
    cd::rhi::DeviceFeatures features_ {};

    std::uint32_t next_id_ { 1u };
    std::unordered_map<std::uint32_t, BufferRecord> buffers_;
    std::unordered_map<std::uint32_t, TextureRecord> textures_;
    std::unordered_map<std::uint32_t, TextureViewRecord> texture_views_;
    std::unordered_map<std::uint32_t, SwapchainRecord> swapchains_;
};

// ---- Trivial command buffer (Phase 13.C — clear-only) ----------------------
//
// A pared-down ICommandBuffer specialization sufficient for
// `hello_d3d12_clear`. It supports begin/end + begin_render_pass
// (records LoadOp::kClear → ClearRenderTargetView and the surrounding
// COMMON↔RENDER_TARGET↔PRESENT transitions) + end_render_pass.
// Everything else surfaces a soft no-op or a NotImplemented Result;
// the PSO/draw surface lands in a later phase.

class D3D12CommandBuffer final : public cd::rhi::ICommandBuffer
{
public:
    D3D12CommandBuffer(D3D12Device* owner, ComPtr<ID3D12CommandAllocator> alloc, ComPtr<ID3D12GraphicsCommandList> list) noexcept
        : owner_ { owner }, alloc_ { std::move(alloc) }, list_ { std::move(list) }
    {}
    ~D3D12CommandBuffer() override = default;
    D3D12CommandBuffer(const D3D12CommandBuffer&) = delete;
    D3D12CommandBuffer& operator=(const D3D12CommandBuffer&) = delete;
    D3D12CommandBuffer(D3D12CommandBuffer&&) = delete;
    D3D12CommandBuffer& operator=(D3D12CommandBuffer&&) = delete;

    [[nodiscard]] ID3D12GraphicsCommandList* native() noexcept { return list_.Get(); }

    void begin() override
    {
        alloc_->Reset();
        list_->Reset(alloc_.Get(), nullptr);
        target_view_ = {};
        target_texture_ = {};
    }
    void end() override
    {
        // Final transition: RENDER_TARGET → PRESENT if we wrote to a
        // swapchain image. After end() the consumer queues a Present().
        if (target_texture_.value() != 0u)
        {
            if (auto* trec = owner_->find_texture(target_texture_))
            {
                D3D12_RESOURCE_BARRIER bar {};
                bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                bar.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                bar.Transition.pResource = trec->resource.Get();
                bar.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                bar.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
                bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                list_->ResourceBarrier(1, &bar);
                trec->state = D3D12_RESOURCE_STATE_PRESENT;
            }
        }
        list_->Close();
    }

    void begin_render_pass(const cd::rhi::RenderPassBeginInfo& info) override
    {
        if (info.color_attachments.empty())
            return;
        const auto& a = info.color_attachments.front();
        target_view_ = a.view;
        if (auto* vrec = owner_->find_texture_view(a.view))
        {
            target_texture_ = vrec->parent;
            if (auto* trec = owner_->find_texture(vrec->parent))
            {
                // Transition into RENDER_TARGET if not already there.
                if (trec->state != D3D12_RESOURCE_STATE_RENDER_TARGET)
                {
                    D3D12_RESOURCE_BARRIER bar {};
                    bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    bar.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                    bar.Transition.pResource = trec->resource.Get();
                    bar.Transition.StateBefore = trec->state;
                    bar.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
                    bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    list_->ResourceBarrier(1, &bar);
                    trec->state = D3D12_RESOURCE_STATE_RENDER_TARGET;
                }
                const D3D12_CPU_DESCRIPTOR_HANDLE rtv = vrec->rtv_cpu;
                list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
                if (a.load_op == cd::rhi::LoadOp::kClear)
                {
                    list_->ClearRenderTargetView(rtv, a.clear_color.f32, 0, nullptr);
                }
            }
        }
    }
    void end_render_pass() override {}

    // The rest of the surface — PSO, draws, copies — surfaces as a
    // no-op or unsupported at v0.32.0. hello_d3d12_clear needs only
    // begin / begin_render_pass / end_render_pass / end.
    void bind_graphics_pipeline(cd::rhi::GraphicsPipelineHandle) override {}
    void bind_compute_pipeline(cd::rhi::ComputePipelineHandle) override {}
    void bind_descriptor_set(std::uint32_t, cd::rhi::DescriptorSetHandle) override {}
    void bind_vertex_buffer(std::uint32_t, cd::rhi::BufferHandle, std::uint64_t) override {}
    void bind_index_buffer(cd::rhi::BufferHandle, std::uint64_t, cd::rhi::IndexType) override {}
    void push_constants(cd::rhi::PipelineLayoutHandle, cd::rhi::ShaderStage, std::uint32_t, std::uint32_t, const void*) override {}
    void set_viewport(const cd::rhi::Viewport&) override {}
    void set_scissor(const cd::rhi::Rect2D&) override {}
    void draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void draw_indexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override {}
    void dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void copy_buffer(cd::rhi::BufferHandle, cd::rhi::BufferHandle, std::span<const cd::rhi::BufferCopyRegion>) override {}
    void copy_buffer_to_image(cd::rhi::BufferHandle, cd::rhi::TextureHandle, std::span<const cd::rhi::BufferImageCopyRegion>) override {}
    void copy_image_to_buffer(cd::rhi::TextureHandle, cd::rhi::BufferHandle, std::span<const cd::rhi::BufferImageCopyRegion>) override {}
    void barrier(std::span<const cd::rhi::BufferBarrier>, std::span<const cd::rhi::TextureBarrier>) override {}
    void push_debug_group(std::string_view) override {}
    void pop_debug_group() override {}

private:
    D3D12Device* owner_ { nullptr };
    ComPtr<ID3D12CommandAllocator> alloc_;
    ComPtr<ID3D12GraphicsCommandList> list_;
    cd::rhi::TextureViewHandle target_view_ {};
    cd::rhi::TextureHandle target_texture_ {};
};

std::unique_ptr<cd::rhi::ICommandBuffer>
D3D12Device::create_command_buffer(cd::rhi::QueueType)
{
    ComPtr<ID3D12CommandAllocator> alloc;
    if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))))
        return nullptr;
    ComPtr<ID3D12GraphicsCommandList> list;
    if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&list))))
        return nullptr;
    list->Close();  // start closed; begin() resets it
    return std::make_unique<D3D12CommandBuffer>(this, alloc, list);
}

void D3D12Device::submit(cd::rhi::ICommandBuffer& cb)
{
    auto* d3d_cb = static_cast<D3D12CommandBuffer*>(&cb);
    ID3D12CommandList* lists[] = { d3d_cb->native() };
    graphics_queue_->ExecuteCommandLists(1, lists);
}

}  // namespace

cd::core::Result<std::unique_ptr<cd::rhi::IDevice>>
create_d3d12_device(D3D12CreateInfo info)
{
    auto dev = std::make_unique<D3D12Device>();
    const HRESULT hr = dev->initialize(info);
    if (FAILED(hr))
    {
        char msg[160] {};
        std::snprintf(msg, sizeof(msg),
                      "D3D12CreateDevice / DXGI init failed: HRESULT 0x%08lx",
                      static_cast<unsigned long>(hr));
        return std::unexpected(cd::rhi::rhi_errors::make_owning(
            cd::rhi::rhi_errors::Code::kBackendInitFailed,
            std::string { msg }));
    }
    return std::unique_ptr<cd::rhi::IDevice>(std::move(dev));
}

// End of the Windows-only block — close the diagnostic pragma we
// pushed alongside the Win32 headers above.
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

#else  // _WIN32 — non-Windows: backend is unavailable.

cd::core::Result<std::unique_ptr<cd::rhi::IDevice>>
create_d3d12_device(D3D12CreateInfo /*info*/)
{
    return std::unexpected(cd::rhi::rhi_errors::make(
        cd::rhi::rhi_errors::Code::kBackendInitFailed,
        "D3D12 backend only available on Windows"));
}

#endif  // _WIN32

}  // namespace cd::rhi_d3d12
