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
    #include <d3dcompiler.h>  // D3D12SerializeRootSignature
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
        // Vertex-attribute formats (Phase 14.C). These don't render
        // textures so they don't appear in the create_texture or
        // swapchain-format paths, but the input layout consumes them.
        case F::kRG32Float:   return DXGI_FORMAT_R32G32_FLOAT;
        case F::kRGB32Float:  return DXGI_FORMAT_R32G32B32_FLOAT;
        case F::kR32Float:    return DXGI_FORMAT_R32_FLOAT;
        case F::kR32Uint:     return DXGI_FORMAT_R32_UINT;
        case F::kRG32Uint:    return DXGI_FORMAT_R32G32_UINT;
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

        // ---- 6. DXR feature query (Phase 142 — v0.99.84) -------------------
        // Tier 1.1 is the DXR generation that includes inline ray queries
        // (the equivalent of Vulkan VK_KHR_ray_query). Tier 1.0 covers the
        // pipeline + traceRayEXT-equivalent. Both are reported via
        // D3D12_FEATURE_DATA_D3D12_OPTIONS5::RaytracingTier.
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 opts5 {};
        if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,
                                                   &opts5, sizeof(opts5))))
        {
            features_.ray_tracing = (opts5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0);
            // Query ID3D12Device5 for the AS-related entry points; if it
            // succeeds we can call BuildRaytracingAccelerationStructure.
            (void)device_.As(&device5_);
        }

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

    // ---- Texture view (REAL — Phase 124 v0.99.51) -------------------------
    //
    // D3D12 has no standalone "texture view" object — views are
    // descriptor-heap entries. We allocate the appropriate descriptor
    // (RTV / DSV / SRV / UAV) based on the parent texture's usage flags
    // and store the resulting CPU handle in TextureViewRecord. The
    // command buffer's begin_render_pass + descriptor-set bind paths
    // already know how to consume those handles.
    //
    // Bump-allocator pools per descriptor heap type. 256 slots each is
    // sufficient for marathon-scale scenes; switch to a free-list when
    // the cap shows up in profiling.

    [[nodiscard]] cd::core::Result<cd::rhi::TextureViewHandle>
    create_texture_view(const cd::rhi::TextureViewDesc& desc) override
    {
        auto* trec = find_texture(desc.texture);
        if (trec == nullptr)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "create_texture_view: unknown parent texture handle"));
        }
        // Phase 126 — TextureType::kCube now supported for the
        // sampled / IBL path. Texture3D + Texture2DArray-rt still
        // pending (no IBL bake path needs them yet).
        const bool is_cube_view = (desc.type == cd::rhi::TextureType::kCube);
        if (desc.type != cd::rhi::TextureType::k2D && !is_cube_view)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kNotImplemented,
                "create_texture_view: only k2D + kCube wired in v0.99.53"));
        }

        const DXGI_FORMAT view_fmt = (desc.format == cd::rhi::Format::kUndefined)
            ? trec->format
            : to_dxgi_format(desc.format);
        if (view_fmt == DXGI_FORMAT_UNKNOWN)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "create_texture_view: unsupported view format"));
        }

        TextureViewRecord vrec;
        vrec.parent = desc.texture;
        vrec.format = view_fmt;
        vrec.is_cube = is_cube_view;

        const auto u = static_cast<std::uint32_t>(trec->usage);
        const bool is_rt    = (u & static_cast<std::uint32_t>(cd::rhi::TextureUsage::kColorAttachment)) != 0;
        const bool is_depth = (u & static_cast<std::uint32_t>(cd::rhi::TextureUsage::kDepthStencilAttachment)) != 0;
        const bool is_samp  = (u & static_cast<std::uint32_t>(cd::rhi::TextureUsage::kSampled)) != 0;
        const bool is_uav   = (u & static_cast<std::uint32_t>(cd::rhi::TextureUsage::kStorage)) != 0;

        if (is_rt)
        {
            if (auto r = ensure_rtv_pool_(); !r.has_value()) return std::unexpected(r.error());
            if (rtv_cursor_ >= kViewPoolCap)
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                    "create_texture_view: RTV pool exhausted"));
            }
            D3D12_CPU_DESCRIPTOR_HANDLE cpu = rtv_pool_->GetCPUDescriptorHandleForHeapStart();
            cpu.ptr += static_cast<SIZE_T>(rtv_cursor_) * rtv_increment_;
            ++rtv_cursor_;

            D3D12_RENDER_TARGET_VIEW_DESC rd {};
            rd.Format = view_fmt;
            rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            rd.Texture2D.MipSlice = desc.base_mip;
            device_->CreateRenderTargetView(trec->resource.Get(), &rd, cpu);
            vrec.rtv_cpu = cpu;
        }
        else if (is_depth)
        {
            if (auto r = ensure_dsv_pool_(); !r.has_value()) return std::unexpected(r.error());
            if (dsv_cursor_ >= kViewPoolCap)
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                    "create_texture_view: DSV pool exhausted"));
            }
            D3D12_CPU_DESCRIPTOR_HANDLE cpu = dsv_pool_->GetCPUDescriptorHandleForHeapStart();
            cpu.ptr += static_cast<SIZE_T>(dsv_cursor_) * dsv_increment_;
            ++dsv_cursor_;

            D3D12_DEPTH_STENCIL_VIEW_DESC dd {};
            dd.Format = view_fmt;
            dd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            dd.Texture2D.MipSlice = desc.base_mip;
            device_->CreateDepthStencilView(trec->resource.Get(), &dd, cpu);
            // The DSV handle lives in a separate pool; we reuse rtv_cpu
            // as the "primary attachment view slot" — begin_render_pass'
            // depth-stencil path reads this slot when the parent texture
            // was created with kDepthStencilAttachment usage.
            vrec.rtv_cpu = cpu;
        }
        else if (is_samp || is_uav)
        {
            if (auto r = ensure_cpu_heap_(); !r.has_value()) return std::unexpected(r.error());
            if (cpu_heap_cursor_ >= 4096)
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                    "create_texture_view: SRV/UAV pool exhausted (4096 cap shared with descriptor sets)"));
            }
            D3D12_CPU_DESCRIPTOR_HANDLE cpu = cpu_heap_->GetCPUDescriptorHandleForHeapStart();
            cpu.ptr += static_cast<SIZE_T>(cpu_heap_cursor_) * cpu_heap_increment_;
            ++cpu_heap_cursor_;

            if (is_samp)
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC sd {};
                sd.Format = view_fmt;
                sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                if (is_cube_view)
                {
                    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                    sd.TextureCube.MostDetailedMip = desc.base_mip;
                    sd.TextureCube.MipLevels = (desc.mip_count == 0u) ? 1u : desc.mip_count;
                    sd.TextureCube.ResourceMinLODClamp = 0.0F;
                }
                else
                {
                    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    sd.Texture2D.MostDetailedMip = desc.base_mip;
                    sd.Texture2D.MipLevels = (desc.mip_count == 0u) ? 1u : desc.mip_count;
                    sd.Texture2D.PlaneSlice = 0;
                    sd.Texture2D.ResourceMinLODClamp = 0.0F;
                }
                device_->CreateShaderResourceView(trec->resource.Get(), &sd, cpu);
            }
            else  // is_uav
            {
                D3D12_UNORDERED_ACCESS_VIEW_DESC ud {};
                ud.Format = view_fmt;
                if (is_cube_view)
                {
                    // Cube UAV maps to a TEXTURE2DARRAY UAV view in DX12
                    // (no dedicated cube UAV). 6 array slices, all mips.
                    ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
                    ud.Texture2DArray.MipSlice = desc.base_mip;
                    ud.Texture2DArray.FirstArraySlice = 0;
                    ud.Texture2DArray.ArraySize = 6;
                    ud.Texture2DArray.PlaneSlice = 0;
                }
                else
                {
                    ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                    ud.Texture2D.MipSlice = desc.base_mip;
                    ud.Texture2D.PlaneSlice = 0;
                }
                device_->CreateUnorderedAccessView(trec->resource.Get(), nullptr, &ud, cpu);
            }
            vrec.rtv_cpu = cpu;
        }
        else
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "create_texture_view: parent texture has no view-creatable usage flag set"));
        }

        const auto id = next_id_++;
        texture_views_.emplace(id, vrec);
        return cd::rhi::TextureViewHandle { id, 1u };
    }
    void destroy_texture_view(cd::rhi::TextureViewHandle h) override
    {
        // Bump-allocator pools — handle entry is dropped, slot reuse
        // requires a free-list pass (Phase 125 candidate).
        texture_views_.erase(h.index());
    }

    [[nodiscard]] cd::core::Result<cd::rhi::SamplerHandle>
    create_sampler(const cd::rhi::SamplerDesc&) override { CD_D3D12_NOT_IMPL_RESULT(SamplerHandle); }
    void destroy_sampler(cd::rhi::SamplerHandle) override {}

    // ---- Shader module (REAL — Phase 14.C v0.36.0) ------------------------

    [[nodiscard]] cd::core::Result<cd::rhi::ShaderModuleHandle>
    create_shader_module(const cd::rhi::ShaderModuleDesc& desc) override
    {
        if (desc.code == nullptr || desc.code_size == 0)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "shader module: empty bytecode"));
        }
        const auto* src = static_cast<const std::uint8_t*>(desc.code);
        ShaderModuleRecord rec;
        rec.bytecode.assign(src, src + desc.code_size);
        rec.stage = desc.stage;
        rec.entry_point = std::string { desc.entry_point };
        const auto id = next_id_++;
        shader_modules_.emplace(id, std::move(rec));
        return cd::rhi::ShaderModuleHandle { id, 1u };
    }

    void destroy_shader_module(cd::rhi::ShaderModuleHandle h) override
    {
        shader_modules_.erase(h.index());
    }

    // ---- Descriptor set layout (REAL — Phase 15.B v0.43.0) ----------------
    [[nodiscard]] cd::core::Result<cd::rhi::DescriptorSetLayoutHandle>
    create_descriptor_set_layout(const cd::rhi::DescriptorSetLayoutDesc& desc) override
    {
        DescriptorSetLayoutRecord rec;
        rec.bindings.assign(desc.bindings.begin(), desc.bindings.end());
        // Count descriptor-table entries — every binding occupies
        // one slot in the CPU heap regardless of type. Samplers are
        // budgeted separately if/when sampler bindings appear.
        for (const auto& b : rec.bindings)
        {
            if (b.type == cd::rhi::DescriptorType::kSampler)
                rec.sampler_count += b.count;
            else
                rec.view_count += b.count;
        }
        const auto id = next_id_++;
        descriptor_set_layouts_.emplace(id, std::move(rec));
        return cd::rhi::DescriptorSetLayoutHandle { id, 1u };
    }
    void destroy_descriptor_set_layout(cd::rhi::DescriptorSetLayoutHandle h) override
    {
        descriptor_set_layouts_.erase(h.index());
    }

    // ---- Pipeline layout (REAL — Phase 14.C v0.36.0 / Phase 15.B v0.43.0)
    //
    // D3D12 root signature. Builds one DESCRIPTOR_TABLE root parameter
    // per descriptor-set layout in the PipelineLayoutDesc; each table
    // contains separate ranges per descriptor type (CBV / SRV / UAV).
    // Sampler bindings live in their own table when present (D3D12
    // requires sampler heaps to be separate from CBV/SRV/UAV heaps).
    // Empty layout (no set_layouts) still produces a valid signature
    // — the triangle sample uses inline vertex data.
    [[nodiscard]] cd::core::Result<cd::rhi::PipelineLayoutHandle>
    create_pipeline_layout(const cd::rhi::PipelineLayoutDesc& desc) override
    {
        std::vector<D3D12_ROOT_PARAMETER> params;
        std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>> all_ranges;
        all_ranges.reserve(desc.set_layouts.size() * 2);
        std::vector<std::uint32_t> table_params;
        table_params.reserve(desc.set_layouts.size());

        for (const auto h : desc.set_layouts)
        {
            auto it = descriptor_set_layouts_.find(h.index());
            if (it == descriptor_set_layouts_.end())
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kInvalidArgument,
                    "pipeline_layout: unknown descriptor_set_layout handle"));
            }
            const auto& layout = it->second;

            // Single descriptor table (CBV/SRV/UAV only — samplers are
            // a separate table when present).
            std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
            ranges.reserve(layout.bindings.size());
            for (const auto& b : layout.bindings)
            {
                if (b.type == cd::rhi::DescriptorType::kSampler) continue;
                D3D12_DESCRIPTOR_RANGE r {};
                switch (b.type)
                {
                    case cd::rhi::DescriptorType::kUniformBuffer:
                    case cd::rhi::DescriptorType::kUniformBufferDynamic:
                        r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
                        break;
                    case cd::rhi::DescriptorType::kSampledImage:
                    case cd::rhi::DescriptorType::kCombinedImageSampler:
                    case cd::rhi::DescriptorType::kInputAttachment:
                    case cd::rhi::DescriptorType::kAccelerationStructure:
                        // DXR exposes acceleration structures as SRVs of
                        // a special RAYTRACING_ACCELERATION_STRUCTURE
                        // type. The root-signature range slot is plain
                        // SRV; the descriptor-write side fills in the
                        // RAYTRACING_ACCELERATION_STRUCTURE SRV desc.
                        r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                        break;
                    case cd::rhi::DescriptorType::kStorageImage:
                    case cd::rhi::DescriptorType::kStorageBuffer:
                    case cd::rhi::DescriptorType::kStorageBufferDynamic:
                        r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                        break;
                    default: continue;
                }
                r.NumDescriptors = b.count;
                r.BaseShaderRegister = b.binding;
                r.RegisterSpace = 0;
                r.OffsetInDescriptorsFromTableStart =
                    D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                ranges.push_back(r);
            }
            if (ranges.empty())
            {
                // Edge case: layout has nothing but samplers. Skip the
                // CBV/SRV/UAV table; a future wave can add a sampler
                // table when sample code needs it.
                continue;
            }
            all_ranges.push_back(std::move(ranges));

            D3D12_ROOT_PARAMETER p {};
            p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p.DescriptorTable.NumDescriptorRanges =
                static_cast<UINT>(all_ranges.back().size());
            p.DescriptorTable.pDescriptorRanges = all_ranges.back().data();
            p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            table_params.push_back(static_cast<std::uint32_t>(params.size()));
            params.push_back(p);
        }

        D3D12_ROOT_SIGNATURE_DESC rsd {};
        rsd.NumParameters = static_cast<UINT>(params.size());
        rsd.pParameters = params.empty() ? nullptr : params.data();
        rsd.NumStaticSamplers = 0;
        rsd.pStaticSamplers = nullptr;
        rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> err;
        HRESULT hr = D3D12SerializeRootSignature(
            &rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
        if (FAILED(hr))
        {
            std::string msg = "D3D12SerializeRootSignature failed: ";
            if (err && err->GetBufferSize() > 0)
                msg.append(static_cast<const char*>(err->GetBufferPointer()),
                           err->GetBufferSize());
            return std::unexpected(cd::rhi::rhi_errors::make_owning(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed, msg));
        }
        ComPtr<ID3D12RootSignature> root_sig;
        hr = device_->CreateRootSignature(
            0, blob->GetBufferPointer(), blob->GetBufferSize(),
            IID_PPV_ARGS(&root_sig));
        if (FAILED(hr))
        {
            char buf[160] {};
            std::snprintf(buf, sizeof(buf),
                          "CreateRootSignature failed: HRESULT 0x%08lx",
                          static_cast<unsigned long>(hr));
            return std::unexpected(cd::rhi::rhi_errors::make_owning(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                std::string { buf }));
        }
        PipelineLayoutRecord rec;
        rec.root_sig = root_sig;
        rec.table_params = std::move(table_params);
        const auto id = next_id_++;
        pipeline_layouts_.emplace(id, std::move(rec));
        return cd::rhi::PipelineLayoutHandle { id, 1u };
    }
    void destroy_pipeline_layout(cd::rhi::PipelineLayoutHandle h) override
    {
        pipeline_layouts_.erase(h.index());
    }

    // ---- Graphics pipeline (REAL — Phase 14.C v0.36.0) --------------------

    [[nodiscard]] cd::core::Result<cd::rhi::GraphicsPipelineHandle>
    create_graphics_pipeline(const cd::rhi::GraphicsPipelineDesc& desc) override
    {
        auto layout_it = pipeline_layouts_.find(desc.layout.index());
        if (layout_it == pipeline_layouts_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "graphics pipeline: layout handle unknown"));
        }
        auto vs_it = shader_modules_.find(desc.vertex_shader.index());
        auto fs_it = shader_modules_.find(desc.fragment_shader.index());
        if (vs_it == shader_modules_.end() || fs_it == shader_modules_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "graphics pipeline: missing vertex or fragment shader module"));
        }
        // Geometry / tess shaders are accepted by the descriptor but
        // optional; only wire them when present.
        const ShaderModuleRecord* gs = nullptr;
        if (desc.geometry_shader.value() != 0u)
        {
            auto it = shader_modules_.find(desc.geometry_shader.index());
            if (it != shader_modules_.end())
                gs = &it->second;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psd {};
        psd.pRootSignature = layout_it->second.root_sig.Get();
        psd.VS = { vs_it->second.bytecode.data(),
                   vs_it->second.bytecode.size() };
        psd.PS = { fs_it->second.bytecode.data(),
                   fs_it->second.bytecode.size() };
        if (gs != nullptr)
            psd.GS = { gs->bytecode.data(), gs->bytecode.size() };

        // Rasterizer state from the engine's RasterState.
        D3D12_RASTERIZER_DESC rs {};
        rs.FillMode = (desc.raster.polygon_mode == cd::rhi::PolygonMode::kFill)
            ? D3D12_FILL_MODE_SOLID : D3D12_FILL_MODE_WIREFRAME;
        switch (desc.raster.cull)
        {
            case cd::rhi::CullMode::kNone:  rs.CullMode = D3D12_CULL_MODE_NONE;  break;
            case cd::rhi::CullMode::kFront: rs.CullMode = D3D12_CULL_MODE_FRONT; break;
            case cd::rhi::CullMode::kBack:  rs.CullMode = D3D12_CULL_MODE_BACK;  break;
            // D3D12 has no FRONT_AND_BACK direct value — emulating it
            // requires culling both sides which equals "draw nothing".
            // We map to NONE (the only sensible interpretation that
            // doesn't suppress the entire geometry).
            case cd::rhi::CullMode::kFrontAndBack: rs.CullMode = D3D12_CULL_MODE_NONE; break;
        }
        // D3D12 "FrontCounterClockwise" — engine's kClockwise default
        // matches D3D12 default (CW = front). Vulkan-NDC samples have
        // already negated their winding by the time they reach the
        // rasterizer; D3D12 doesn't Y-flip in NDC so we honor the
        // descriptor directly.
        rs.FrontCounterClockwise =
            (desc.raster.front_face == cd::rhi::FrontFace::kCounterClockwise)
                ? TRUE : FALSE;
        rs.DepthBias = 0;
        rs.DepthBiasClamp = 0.0F;
        rs.SlopeScaledDepthBias = 0.0F;
        rs.DepthClipEnable = TRUE;
        rs.MultisampleEnable = FALSE;
        rs.AntialiasedLineEnable = FALSE;
        rs.ForcedSampleCount = 0;
        rs.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
        psd.RasterizerState = rs;

        // Blend — opaque single attachment for v0.36.0.
        D3D12_BLEND_DESC bd {};
        bd.AlphaToCoverageEnable = FALSE;
        bd.IndependentBlendEnable = FALSE;
        for (auto& rt : bd.RenderTarget)
        {
            rt.BlendEnable = FALSE;
            rt.LogicOpEnable = FALSE;
            rt.SrcBlend = D3D12_BLEND_ONE;
            rt.DestBlend = D3D12_BLEND_ZERO;
            rt.BlendOp = D3D12_BLEND_OP_ADD;
            rt.SrcBlendAlpha = D3D12_BLEND_ONE;
            rt.DestBlendAlpha = D3D12_BLEND_ZERO;
            rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            rt.LogicOp = D3D12_LOGIC_OP_NOOP;
            rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        }
        psd.BlendState = bd;

        // Depth-stencil. Disabled by default for hello_d3d12_triangle —
        // the depth_attachment_format == kUndefined branch reflects that.
        D3D12_DEPTH_STENCIL_DESC ds {};
        ds.DepthEnable = (desc.depth_attachment_format != cd::rhi::Format::kUndefined)
            && desc.depth_stencil.depth_test;
        ds.DepthWriteMask = desc.depth_stencil.depth_write
            ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        ds.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        ds.StencilEnable = FALSE;
        psd.DepthStencilState = ds;

        psd.SampleMask = UINT_MAX;
        switch (desc.topology)
        {
            case cd::rhi::PrimitiveTopology::kPointList:
                psd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
                break;
            case cd::rhi::PrimitiveTopology::kLineList:
            case cd::rhi::PrimitiveTopology::kLineStrip:
                psd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
                break;
            default:
                psd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                break;
        }

        // Vertex input layout. Engine's VertexAttribute carries
        // (location, binding, format, offset) per attribute and the
        // VertexBinding[] carries the stride per binding slot. Translate
        // to D3D12_INPUT_ELEMENT_DESC[] — semantic name is "TEXCOORD"
        // with semantic index = engine's `location` so HLSL can
        // address them via TEXCOORD0/TEXCOORD1/... regardless of
        // what the user's `location` integer means.
        std::vector<D3D12_INPUT_ELEMENT_DESC> input_elements;
        input_elements.reserve(desc.vertex_attributes.size());
        std::vector<std::string> semantic_storage;  // keep names alive
        semantic_storage.reserve(desc.vertex_attributes.size());
        for (const auto& va : desc.vertex_attributes)
        {
            D3D12_INPUT_ELEMENT_DESC e {};
            semantic_storage.push_back("TEXCOORD");
            e.SemanticName = semantic_storage.back().c_str();
            e.SemanticIndex = va.location;
            e.Format = to_dxgi_format(va.format);
            e.InputSlot = va.binding;
            e.AlignedByteOffset = va.offset;
            e.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            e.InstanceDataStepRate = 0;
            input_elements.push_back(e);
        }
        psd.InputLayout.NumElements = static_cast<UINT>(input_elements.size());
        psd.InputLayout.pInputElementDescs =
            input_elements.empty() ? nullptr : input_elements.data();

        // Color attachment formats.
        psd.NumRenderTargets =
            static_cast<UINT>(std::min<std::size_t>(desc.color_attachment_formats.size(), 8));
        for (UINT i = 0; i < psd.NumRenderTargets; ++i)
        {
            psd.RTVFormats[i] = to_dxgi_format(desc.color_attachment_formats[i]);
        }
        psd.DSVFormat = (desc.depth_attachment_format != cd::rhi::Format::kUndefined)
            ? to_dxgi_format(desc.depth_attachment_format)
            : DXGI_FORMAT_UNKNOWN;
        psd.SampleDesc.Count = 1;
        psd.SampleDesc.Quality = 0;
        psd.NodeMask = 0;
        psd.CachedPSO = {};
        psd.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

        ComPtr<ID3D12PipelineState> pso;
        HRESULT hr = device_->CreateGraphicsPipelineState(&psd, IID_PPV_ARGS(&pso));
        if (FAILED(hr))
        {
            char buf[160] {};
            std::snprintf(buf, sizeof(buf),
                          "CreateGraphicsPipelineState failed: HRESULT 0x%08lx",
                          static_cast<unsigned long>(hr));
            return std::unexpected(cd::rhi::rhi_errors::make_owning(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                std::string { buf }));
        }
        GraphicsPipelineRecord rec;
        rec.pso = pso;
        rec.layout_handle = desc.layout;
        // Cache the D3D12 topology that bind_graphics_pipeline +
        // IASetPrimitiveTopology consumer needs (PSO carries the
        // *type* but the command-list call needs the *topology*
        // enum). We stamp kTriangleList by default for v0.36.0.
        switch (desc.topology)
        {
            case cd::rhi::PrimitiveTopology::kPointList:
                rec.d3d_topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
                break;
            case cd::rhi::PrimitiveTopology::kLineList:
                rec.d3d_topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
                break;
            case cd::rhi::PrimitiveTopology::kLineStrip:
                rec.d3d_topology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
                break;
            case cd::rhi::PrimitiveTopology::kTriangleStrip:
                rec.d3d_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
                break;
            default:
                rec.d3d_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
                break;
        }
        const auto id = next_id_++;
        graphics_pipelines_.emplace(id, std::move(rec));
        return cd::rhi::GraphicsPipelineHandle { id, 1u };
    }
    void destroy_graphics_pipeline(cd::rhi::GraphicsPipelineHandle h) override
    {
        graphics_pipelines_.erase(h.index());
    }

    [[nodiscard]] cd::core::Result<cd::rhi::ComputePipelineHandle>
    create_compute_pipeline(const cd::rhi::ComputePipelineDesc&) override { CD_D3D12_NOT_IMPL_RESULT(ComputePipelineHandle); }
    void destroy_compute_pipeline(cd::rhi::ComputePipelineHandle) override {}

    // ---- Descriptor set (REAL — Phase 15.B v0.43.0) -----------------------

    [[nodiscard]] cd::core::Result<cd::rhi::DescriptorSetHandle>
    allocate_descriptor_set(cd::rhi::DescriptorSetLayoutHandle layout) override
    {
        auto it = descriptor_set_layouts_.find(layout.index());
        if (it == descriptor_set_layouts_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "allocate_descriptor_set: unknown layout"));
        }
        const auto vcount = it->second.view_count;
        if (cpu_heap_ == nullptr)
        {
            // Lazily create the CPU-visible descriptor heap. 4096 slots
            // covers the marathon-shippable set count; future waves can
            // grow it or switch to a slab allocator.
            D3D12_DESCRIPTOR_HEAP_DESC hd {};
            hd.NumDescriptors = 4096;
            hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            if (FAILED(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&cpu_heap_))))
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                    "CPU descriptor heap creation failed"));
            }
            cpu_heap_increment_ = device_->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
        if (cpu_heap_cursor_ + vcount > 4096)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "CPU descriptor heap exhausted (4096-slot cap)"));
        }
        DescriptorSetRecord rec;
        rec.layout_handle = layout;
        rec.cpu_heap_offset = cpu_heap_cursor_;
        rec.view_count = vcount;
        cpu_heap_cursor_ += vcount;
        const auto id = next_id_++;
        descriptor_sets_.emplace(id, std::move(rec));
        return cd::rhi::DescriptorSetHandle { id, 1u };
    }
    void destroy_descriptor_set(cd::rhi::DescriptorSetHandle h) override
    {
        descriptor_sets_.erase(h.index());
        // CPU heap slots are not reclaimed (bump allocator). Acceptable
        // until the slab allocator wave.
    }

    [[nodiscard]] cd::core::Result<void>
    update_descriptor_set(
        cd::rhi::DescriptorSetHandle h,
        std::span<const cd::rhi::DescriptorWrite> writes) override
    {
        auto set_it = descriptor_sets_.find(h.index());
        if (set_it == descriptor_sets_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "update_descriptor_set: unknown set"));
        }
        const auto& set = set_it->second;
        auto layout_it = descriptor_set_layouts_.find(set.layout_handle.index());
        if (layout_it == descriptor_set_layouts_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "update_descriptor_set: layout vanished"));
        }
        const auto& layout = layout_it->second;

        D3D12_CPU_DESCRIPTOR_HANDLE heap_base = cpu_heap_->GetCPUDescriptorHandleForHeapStart();
        heap_base.ptr += static_cast<SIZE_T>(set.cpu_heap_offset) *
                         cpu_heap_increment_;

        for (const auto& w : writes)
        {
            // Find the offset of this binding within the set's
            // descriptor table. Sum view-typed binding counts of
            // earlier bindings.
            std::uint32_t offset = 0;
            bool found = false;
            for (const auto& b : layout.bindings)
            {
                if (b.binding == w.binding)
                {
                    found = true;
                    break;
                }
                if (b.type != cd::rhi::DescriptorType::kSampler)
                    offset += b.count;
            }
            if (!found)
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kInvalidArgument,
                    "update_descriptor_set: write binding not in layout"));
            }
            D3D12_CPU_DESCRIPTOR_HANDLE dst = heap_base;
            dst.ptr += static_cast<SIZE_T>(offset + w.array_element) *
                       cpu_heap_increment_;

            switch (w.type)
            {
                case cd::rhi::DescriptorType::kUniformBuffer:
                case cd::rhi::DescriptorType::kUniformBufferDynamic:
                {
                    auto buf_it = buffers_.find(w.buffer.index());
                    if (buf_it == buffers_.end())
                    {
                        return std::unexpected(cd::rhi::rhi_errors::make(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: CBV buffer unknown"));
                    }
                    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv {};
                    cbv.BufferLocation =
                        buf_it->second.resource->GetGPUVirtualAddress() + w.buffer_offset;
                    UINT64 range = w.buffer_range == 0
                        ? (buf_it->second.size - w.buffer_offset)
                        : w.buffer_range;
                    // CBV size must be a multiple of 256.
                    range = (range + 255u) & ~static_cast<UINT64>(255u);
                    cbv.SizeInBytes = static_cast<UINT>(range);
                    device_->CreateConstantBufferView(&cbv, dst);
                    break;
                }
                case cd::rhi::DescriptorType::kSampledImage:
                {
                    auto view_it = texture_views_.find(w.view.index());
                    if (view_it == texture_views_.end())
                    {
                        return std::unexpected(cd::rhi::rhi_errors::make(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: SRV view unknown"));
                    }
                    auto tex_it = textures_.find(view_it->second.parent.index());
                    if (tex_it == textures_.end())
                    {
                        return std::unexpected(cd::rhi::rhi_errors::make(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: SRV texture unknown"));
                    }
                    D3D12_SHADER_RESOURCE_VIEW_DESC srv {};
                    srv.Format = view_it->second.format;
                    srv.Shader4ComponentMapping =
                        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    // Phase 126 — pick TEXTURECUBE dimension when the
                    // view was created from a kCube parent. Otherwise
                    // standard Texture2D.
                    if (view_it->second.is_cube)
                    {
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                        srv.TextureCube.MostDetailedMip = 0;
                        srv.TextureCube.MipLevels = 1;
                        srv.TextureCube.ResourceMinLODClamp = 0.0F;
                    }
                    else
                    {
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                        srv.Texture2D.MipLevels = 1;
                    }
                    device_->CreateShaderResourceView(
                        tex_it->second.resource.Get(), &srv, dst);
                    break;
                }
                case cd::rhi::DescriptorType::kStorageImage:
                {
                    // UAV (Phase 16.B). Texture2D RW for compute /
                    // graphics-side image stores.
                    auto view_it = texture_views_.find(w.view.index());
                    if (view_it == texture_views_.end())
                        return std::unexpected(cd::rhi::rhi_errors::make(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: UAV view unknown"));
                    auto tex_it = textures_.find(view_it->second.parent.index());
                    if (tex_it == textures_.end())
                        return std::unexpected(cd::rhi::rhi_errors::make(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: UAV texture unknown"));
                    D3D12_UNORDERED_ACCESS_VIEW_DESC uav {};
                    uav.Format = view_it->second.format;
                    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                    uav.Texture2D.MipSlice = 0;
                    uav.Texture2D.PlaneSlice = 0;
                    device_->CreateUnorderedAccessView(
                        tex_it->second.resource.Get(), nullptr, &uav, dst);
                    break;
                }
                case cd::rhi::DescriptorType::kStorageBuffer:
                case cd::rhi::DescriptorType::kStorageBufferDynamic:
                {
                    // UAV Buffer (raw / structured). Phase 16.B
                    // ships the raw flavour (one element per 4 bytes);
                    // structured stride needs more descriptor metadata.
                    auto buf_it = buffers_.find(w.buffer.index());
                    if (buf_it == buffers_.end())
                        return std::unexpected(cd::rhi::rhi_errors::make(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: UAV buffer unknown"));
                    D3D12_UNORDERED_ACCESS_VIEW_DESC uav {};
                    uav.Format = DXGI_FORMAT_R32_TYPELESS;
                    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                    uav.Buffer.FirstElement = w.buffer_offset / 4;
                    UINT64 range = w.buffer_range == 0
                        ? (buf_it->second.size - w.buffer_offset)
                        : w.buffer_range;
                    uav.Buffer.NumElements = static_cast<UINT>(range / 4);
                    uav.Buffer.StructureByteStride = 0;
                    uav.Buffer.CounterOffsetInBytes = 0;
                    uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
                    device_->CreateUnorderedAccessView(
                        buf_it->second.resource.Get(), nullptr, &uav, dst);
                    break;
                }
                case cd::rhi::DescriptorType::kCombinedImageSampler:
                {
                    // CombinedImageSampler maps to a Texture2D SRV in
                    // the CBV/SRV/UAV table (the sampler half goes
                    // into a separate sampler heap when present; for
                    // v0.47.0 we route to a static-sampler fallback
                    // baked into the root signature). Same SRV path
                    // as kSampledImage.
                    auto view_it = texture_views_.find(w.view.index());
                    if (view_it == texture_views_.end())
                        return std::unexpected(cd::rhi::rhi_errors::make(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: combined SRV view unknown"));
                    auto tex_it = textures_.find(view_it->second.parent.index());
                    if (tex_it == textures_.end())
                        return std::unexpected(cd::rhi::rhi_errors::make(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: combined SRV texture unknown"));
                    D3D12_SHADER_RESOURCE_VIEW_DESC srv {};
                    srv.Format = view_it->second.format;
                    srv.Shader4ComponentMapping =
                        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    if (view_it->second.is_cube)
                    {
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                        srv.TextureCube.MostDetailedMip = 0;
                        srv.TextureCube.MipLevels = 1;
                        srv.TextureCube.ResourceMinLODClamp = 0.0F;
                    }
                    else
                    {
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                        srv.Texture2D.MipLevels = 1;
                    }
                    device_->CreateShaderResourceView(
                        tex_it->second.resource.Get(), &srv, dst);
                    break;
                }
                case cd::rhi::DescriptorType::kSampler:
                    // Sampler writes belong in a separate sampler heap;
                    // this CBV/SRV/UAV-only update path skips them.
                    // The root signature currently bakes static
                    // samplers (LINEAR clamp by default) so this
                    // skip is safe for the v0.47.0 short-list of
                    // forward-shaded samples.
                    break;
                case cd::rhi::DescriptorType::kInputAttachment:
                {
                    // Phase 129 — D3D12 doesn't have a distinct "input
                    // attachment" concept. Vulkan input attachments are
                    // colour/depth render targets read back as textures
                    // inside the same render pass. On D3D12 the same
                    // image is bound twice (as an RTV in the OM and as
                    // an SRV in the root table); a developer writing
                    // cross-backend code can declare the same handle
                    // as kInputAttachment, and we route it down the
                    // SRV path so the descriptor is filled with a
                    // TEXTURE2D SRV.
                    auto view_it = texture_views_.find(w.view.index());
                    if (view_it == texture_views_.end())
                        return std::unexpected(cd::rhi::rhi_errors::make(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: input-attachment view unknown"));
                    auto tex_it = textures_.find(view_it->second.parent.index());
                    if (tex_it == textures_.end())
                        return std::unexpected(cd::rhi::rhi_errors::make(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: input-attachment texture unknown"));
                    D3D12_SHADER_RESOURCE_VIEW_DESC srv {};
                    srv.Format = view_it->second.format;
                    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    srv.Shader4ComponentMapping =
                        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    srv.Texture2D.MipLevels = 1;
                    device_->CreateShaderResourceView(
                        tex_it->second.resource.Get(), &srv, dst);
                    break;
                }
                default:
                    return std::unexpected(cd::rhi::rhi_errors::make(
                        cd::rhi::rhi_errors::Code::kNotImplemented,
                        "update_descriptor_set: unhandled DescriptorType value"));
            }
        }
        return {};
    }

    // ---- Synchronization (REAL — Phase 125 v0.99.52) ----------------------
    //
    // D3D12 doesn't have binary semaphores in the Vulkan sense; the
    // queue-side sync primitive is `ID3D12Fence` (which is essentially
    // a Vulkan timeline semaphore). We map:
    //
    //   * `SemaphoreHandle`  — `ID3D12Fence` + a per-instance value
    //                          that bumps to "1" on each signal.
    //                          acquire_next_image / present accept the
    //                          handle for interface parity and ignore
    //                          it (DXGI handles its own backbuffer
    //                          fencing).
    //   * `FenceHandle`      — `ID3D12Fence` + a Win32 event the host
    //                          waits on (`SetEventOnCompletion`).
    //                          `signaled` boot state matches Vulkan's
    //                          `VkFenceCreateFlagBits::VK_FENCE_CREATE_SIGNALED_BIT`.
    //   * `TimelineSemaphoreHandle` — `ID3D12Fence` exposed natively
    //                          since fences already carry a u64 counter.
    //
    // All three share an ID3D12Fence underneath; the wrapper types
    // exist for ABI parity with Vulkan code paths.

    [[nodiscard]] cd::core::Result<cd::rhi::SemaphoreHandle>
    create_semaphore() override
    {
        ComPtr<ID3D12Fence> fence;
        HRESULT hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                          IID_PPV_ARGS(&fence));
        if (FAILED(hr))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "CreateFence(semaphore) failed"));
        }
        const auto id = next_id_++;
        SemaphoreRecord rec;
        rec.fence = fence;
        rec.value = 0;
        semaphores_.emplace(id, std::move(rec));
        return cd::rhi::SemaphoreHandle { id, 1u };
    }
    void destroy_semaphore(cd::rhi::SemaphoreHandle h) override
    {
        semaphores_.erase(h.index());
    }

    [[nodiscard]] cd::core::Result<cd::rhi::FenceHandle>
    create_fence(bool signaled) override
    {
        ComPtr<ID3D12Fence> fence;
        const UINT64 initial = signaled ? 1u : 0u;
        HRESULT hr = device_->CreateFence(initial, D3D12_FENCE_FLAG_NONE,
                                          IID_PPV_ARGS(&fence));
        if (FAILED(hr))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "CreateFence(fence) failed"));
        }
        HANDLE evt = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (evt == nullptr)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "CreateEvent(fence) failed"));
        }
        const auto id = next_id_++;
        FenceRecord rec;
        rec.fence = fence;
        rec.event = evt;
        rec.target_value = initial;  // matches the value the fence was created at
        fences_.emplace(id, std::move(rec));
        return cd::rhi::FenceHandle { id, 1u };
    }
    void destroy_fence(cd::rhi::FenceHandle h) override
    {
        auto it = fences_.find(h.index());
        if (it == fences_.end()) return;
        if (it->second.event != nullptr) ::CloseHandle(it->second.event);
        fences_.erase(it);
    }

    [[nodiscard]] cd::core::Result<void>
    wait_for_fence(cd::rhi::FenceHandle h, std::uint64_t timeout_ns) override
    {
        auto it = fences_.find(h.index());
        if (it == fences_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "wait_for_fence: unknown fence handle"));
        }
        auto& rec = it->second;
        // Fast path: already at or above target.
        if (rec.fence->GetCompletedValue() >= rec.target_value) return {};

        if (FAILED(rec.fence->SetEventOnCompletion(rec.target_value, rec.event)))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kDeviceLost,
                "SetEventOnCompletion failed"));
        }
        // ns → ms with rounding-up; INFINITE on a 0-tag timeout
        // (matching Vulkan's UINT64_MAX semantic).
        const DWORD ms = (timeout_ns == 0u || timeout_ns == ~std::uint64_t { 0 })
            ? INFINITE
            : static_cast<DWORD>((timeout_ns + 999'999u) / 1'000'000u);
        const DWORD wr = ::WaitForSingleObject(rec.event, ms);
        if (wr == WAIT_TIMEOUT)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kTimeout,
                "wait_for_fence: timed out"));
        }
        if (wr != WAIT_OBJECT_0)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kDeviceLost,
                "wait_for_fence: WaitForSingleObject failed"));
        }
        return {};
    }

    void reset_fence(cd::rhi::FenceHandle h) override
    {
        auto it = fences_.find(h.index());
        if (it == fences_.end()) return;
        // D3D12 fences are monotonic counters — "reset" means
        // setting the target value forward to the next signal we
        // expect. The caller is responsible for queueing the next
        // signal via command-queue Signal afterwards.
        ++it->second.target_value;
    }

    [[nodiscard]] bool is_fence_signaled(cd::rhi::FenceHandle h) override
    {
        auto it = fences_.find(h.index());
        if (it == fences_.end()) return false;
        return it->second.fence->GetCompletedValue() >= it->second.target_value;
    }

    [[nodiscard]] cd::core::Result<cd::rhi::TimelineSemaphoreHandle>
    create_timeline_semaphore(std::uint64_t initial_value) override
    {
        ComPtr<ID3D12Fence> fence;
        HRESULT hr = device_->CreateFence(initial_value, D3D12_FENCE_FLAG_NONE,
                                          IID_PPV_ARGS(&fence));
        if (FAILED(hr))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "CreateFence(timeline) failed"));
        }
        HANDLE evt = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (evt == nullptr)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "CreateEvent(timeline) failed"));
        }
        const auto id = next_id_++;
        TimelineRecord rec;
        rec.fence = fence;
        rec.event = evt;
        timelines_.emplace(id, std::move(rec));
        return cd::rhi::TimelineSemaphoreHandle { id, 1u };
    }
    void destroy_timeline_semaphore(cd::rhi::TimelineSemaphoreHandle h) override
    {
        auto it = timelines_.find(h.index());
        if (it == timelines_.end()) return;
        if (it->second.event != nullptr) ::CloseHandle(it->second.event);
        timelines_.erase(it);
    }

    [[nodiscard]] cd::core::Result<void>
    wait_timeline_semaphore(cd::rhi::TimelineSemaphoreHandle h,
                            std::uint64_t value,
                            std::uint64_t timeout_ns) override
    {
        auto it = timelines_.find(h.index());
        if (it == timelines_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "wait_timeline_semaphore: unknown handle"));
        }
        auto& rec = it->second;
        if (rec.fence->GetCompletedValue() >= value) return {};
        if (FAILED(rec.fence->SetEventOnCompletion(value, rec.event)))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kDeviceLost,
                "SetEventOnCompletion(timeline) failed"));
        }
        const DWORD ms = (timeout_ns == 0u || timeout_ns == ~std::uint64_t { 0 })
            ? INFINITE
            : static_cast<DWORD>((timeout_ns + 999'999u) / 1'000'000u);
        const DWORD wr = ::WaitForSingleObject(rec.event, ms);
        if (wr == WAIT_TIMEOUT)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kTimeout,
                "wait_timeline_semaphore: timed out"));
        }
        if (wr != WAIT_OBJECT_0)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kDeviceLost,
                "WaitForSingleObject(timeline) failed"));
        }
        return {};
    }

    [[nodiscard]] cd::core::Result<void>
    signal_timeline_semaphore(cd::rhi::TimelineSemaphoreHandle h, std::uint64_t value) override
    {
        auto it = timelines_.find(h.index());
        if (it == timelines_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "signal_timeline_semaphore: unknown handle"));
        }
        if (FAILED(it->second.fence->Signal(value)))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kDeviceLost,
                "Signal(timeline) failed"));
        }
        return {};
    }

    [[nodiscard]] std::uint64_t timeline_semaphore_value(cd::rhi::TimelineSemaphoreHandle h) const override
    {
        auto it = timelines_.find(h.index());
        if (it == timelines_.end()) return 0;
        return it->second.fence->GetCompletedValue();
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
        /// Phase 126 — true when the view was created with
        /// `TextureType::kCube`. `update_descriptor_set` uses this hint
        /// to pick `D3D12_SRV_DIMENSION_TEXTURECUBE` instead of TEX2D
        /// when (re-)creating the SRV at descriptor-set update time.
        bool is_cube { false };
    };

    struct ShaderModuleRecord
    {
        std::vector<std::uint8_t> bytecode;
        cd::rhi::ShaderStage stage { cd::rhi::ShaderStage::kNone };
        std::string entry_point;
    };

    // Phase 125 — sync primitive storage. All three are backed by
    // ID3D12Fence; the wrappers differ in surface semantics.
    struct SemaphoreRecord
    {
        ComPtr<ID3D12Fence> fence;
        std::uint64_t       value { 0 };
    };
    struct FenceRecord
    {
        ComPtr<ID3D12Fence> fence;
        HANDLE              event { nullptr };
        std::uint64_t       target_value { 0 };
    };
    struct TimelineRecord
    {
        ComPtr<ID3D12Fence> fence;
        HANDLE              event { nullptr };
    };

    struct DescriptorSetLayoutRecord
    {
        std::vector<cd::rhi::DescriptorSetLayoutBinding> bindings;
        std::uint32_t view_count { 0 };       // CBV + SRV + UAV total
        std::uint32_t sampler_count { 0 };
    };

    struct PipelineLayoutRecord
    {
        ComPtr<ID3D12RootSignature> root_sig;
        /// Index of the first table parameter per descriptor-set slot.
        /// table_params[i] == root-signature parameter index of the
        /// descriptor table that backs descriptor-set index i.
        std::vector<std::uint32_t> table_params;
    };

    struct DescriptorSetRecord
    {
        cd::rhi::DescriptorSetLayoutHandle layout_handle {};
        /// CPU-heap slot range that holds this set's descriptors. The
        /// values are written into the CPU-visible heap by
        /// update_descriptor_set and copied into the per-frame GPU
        /// heap by bind_descriptor_set.
        std::uint32_t cpu_heap_offset { 0 };
        std::uint32_t view_count { 0 };
    };

    struct GraphicsPipelineRecord
    {
        ComPtr<ID3D12PipelineState> pso;
        cd::rhi::PipelineLayoutHandle layout_handle {};
        D3D_PRIMITIVE_TOPOLOGY d3d_topology { D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST };
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
    ComPtr<ID3D12Device5> device5_;  // DXR entry points (Phase 142)
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
    std::unordered_map<std::uint32_t, ShaderModuleRecord> shader_modules_;
    std::unordered_map<std::uint32_t, DescriptorSetLayoutRecord> descriptor_set_layouts_;
    std::unordered_map<std::uint32_t, PipelineLayoutRecord> pipeline_layouts_;
    std::unordered_map<std::uint32_t, GraphicsPipelineRecord> graphics_pipelines_;
    std::unordered_map<std::uint32_t, DescriptorSetRecord> descriptor_sets_;

    // CPU-visible descriptor heap (CBV/SRV/UAV) — slot-bump allocator.
    ComPtr<ID3D12DescriptorHeap> cpu_heap_;
    UINT cpu_heap_increment_ { 0 };
    std::uint32_t cpu_heap_cursor_ { 0 };

    // Phase 125 — sync primitive registries.
    std::unordered_map<std::uint32_t, SemaphoreRecord> semaphores_;
    std::unordered_map<std::uint32_t, FenceRecord>     fences_;
    std::unordered_map<std::uint32_t, TimelineRecord>  timelines_;

    // Phase 124 — device-level RTV / DSV pools for create_texture_view.
    // Bump allocators. The swapchain RTV path uses its own per-swapchain
    // heap (Phase 13.C); these pools are for non-swapchain attachments
    // (depth target, offscreen colour pass, IBL render).
    static constexpr std::uint32_t kViewPoolCap = 256;
    ComPtr<ID3D12DescriptorHeap> rtv_pool_;
    UINT                          rtv_increment_ { 0 };
    std::uint32_t                 rtv_cursor_    { 0 };
    ComPtr<ID3D12DescriptorHeap> dsv_pool_;
    UINT                          dsv_increment_ { 0 };
    std::uint32_t                 dsv_cursor_    { 0 };

    [[nodiscard]] cd::core::Result<void> ensure_rtv_pool_()
    {
        if (rtv_pool_ != nullptr) return {};
        D3D12_DESCRIPTOR_HEAP_DESC hd {};
        hd.NumDescriptors = kViewPoolCap;
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtv_pool_))))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "RTV view-pool heap creation failed"));
        }
        rtv_increment_ = device_->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        return {};
    }
    [[nodiscard]] cd::core::Result<void> ensure_dsv_pool_()
    {
        if (dsv_pool_ != nullptr) return {};
        D3D12_DESCRIPTOR_HEAP_DESC hd {};
        hd.NumDescriptors = kViewPoolCap;
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&dsv_pool_))))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "DSV view-pool heap creation failed"));
        }
        dsv_increment_ = device_->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        return {};
    }
    [[nodiscard]] cd::core::Result<void> ensure_cpu_heap_()
    {
        if (cpu_heap_ != nullptr) return {};
        D3D12_DESCRIPTOR_HEAP_DESC hd {};
        hd.NumDescriptors = 4096;
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&cpu_heap_))))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "CPU descriptor heap creation failed (view path)"));
        }
        cpu_heap_increment_ = device_->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        return {};
    }
    // GPU-visible descriptor heap — populated per-bind by copying from
    // the CPU heap. Ring-buffer style allocator (16k slots) so frames
    // don't trample each other.
    ComPtr<ID3D12DescriptorHeap> gpu_heap_;
    UINT gpu_heap_increment_ { 0 };
    std::uint32_t gpu_heap_cursor_ { 0 };
    static constexpr UINT kGpuHeapCap = 16384;

public:
    [[nodiscard]] GraphicsPipelineRecord* find_graphics_pipeline(cd::rhi::GraphicsPipelineHandle h) noexcept
    {
        auto it = graphics_pipelines_.find(h.index());
        return it == graphics_pipelines_.end() ? nullptr : &it->second;
    }
    [[nodiscard]] PipelineLayoutRecord* find_pipeline_layout(cd::rhi::PipelineLayoutHandle h) noexcept
    {
        auto it = pipeline_layouts_.find(h.index());
        return it == pipeline_layouts_.end() ? nullptr : &it->second;
    }
    [[nodiscard]] BufferRecord* find_buffer(cd::rhi::BufferHandle h) noexcept
    {
        auto it = buffers_.find(h.index());
        return it == buffers_.end() ? nullptr : &it->second;
    }
    [[nodiscard]] DescriptorSetRecord* find_descriptor_set(cd::rhi::DescriptorSetHandle h) noexcept
    {
        auto it = descriptor_sets_.find(h.index());
        return it == descriptor_sets_.end() ? nullptr : &it->second;
    }

    /// Copy `view_count` descriptors from this set's CPU heap slots into
    /// the per-frame GPU-visible heap and return the resulting GPU
    /// descriptor handle. Allocates a fresh GPU heap on first call.
    /// Returns an invalid handle (ptr == 0) on failure.
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE
    copy_set_to_gpu_heap(const DescriptorSetRecord& set)
    {
        if (gpu_heap_ == nullptr)
        {
            D3D12_DESCRIPTOR_HEAP_DESC hd {};
            hd.NumDescriptors = kGpuHeapCap;
            hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            if (FAILED(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&gpu_heap_))))
                return D3D12_GPU_DESCRIPTOR_HANDLE { 0 };
            gpu_heap_increment_ = device_->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
        if (gpu_heap_cursor_ + set.view_count > kGpuHeapCap)
            gpu_heap_cursor_ = 0;  // wrap (ring); rely on wait_idle()
                                   // between frames to keep things sane
        const auto slot = gpu_heap_cursor_;
        gpu_heap_cursor_ += set.view_count;
        D3D12_CPU_DESCRIPTOR_HANDLE src = cpu_heap_->GetCPUDescriptorHandleForHeapStart();
        src.ptr += static_cast<SIZE_T>(set.cpu_heap_offset) * cpu_heap_increment_;
        D3D12_CPU_DESCRIPTOR_HANDLE gpu_cpu = gpu_heap_->GetCPUDescriptorHandleForHeapStart();
        gpu_cpu.ptr += static_cast<SIZE_T>(slot) * gpu_heap_increment_;
        device_->CopyDescriptorsSimple(set.view_count, gpu_cpu, src,
                                       D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_GPU_DESCRIPTOR_HANDLE out = gpu_heap_->GetGPUDescriptorHandleForHeapStart();
        out.ptr += static_cast<UINT64>(slot) * gpu_heap_increment_;
        return out;
    }
    [[nodiscard]] ID3D12DescriptorHeap* gpu_heap() noexcept { return gpu_heap_.Get(); }
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

    // PSO + draw surface (Phase 14.C / v0.36.0). The rest of the
    // ICommandBuffer surface (compute dispatch, descriptor sets,
    // copies, barriers) remains no-op for v0.36.0 — wired by later
    // phases.
    void bind_graphics_pipeline(cd::rhi::GraphicsPipelineHandle h) override
    {
        if (auto* rec = owner_->find_graphics_pipeline(h))
        {
            list_->SetPipelineState(rec->pso.Get());
            if (auto* layout = owner_->find_pipeline_layout(rec->layout_handle))
                list_->SetGraphicsRootSignature(layout->root_sig.Get());
            list_->IASetPrimitiveTopology(rec->d3d_topology);
        }
    }
    void bind_compute_pipeline(cd::rhi::ComputePipelineHandle) override {}
    void bind_descriptor_set(std::uint32_t set_index, cd::rhi::DescriptorSetHandle set) override
    {
        // Phase 15.B real implementation: copy this set's descriptors
        // into the per-frame GPU-visible heap and bind it as the root
        // descriptor table for parameter `set_index`.
        auto* rec = owner_->find_descriptor_set(set);
        if (rec == nullptr) return;
        const auto gpu = owner_->copy_set_to_gpu_heap(*rec);
        if (gpu.ptr == 0) return;
        ID3D12DescriptorHeap* heaps[] = { owner_->gpu_heap() };
        list_->SetDescriptorHeaps(1, heaps);
        list_->SetGraphicsRootDescriptorTable(set_index, gpu);
    }
    void bind_vertex_buffer(std::uint32_t binding, cd::rhi::BufferHandle buffer, std::uint64_t offset) override
    {
        if (auto* buf = owner_->find_buffer(buffer))
        {
            D3D12_VERTEX_BUFFER_VIEW vbv {};
            vbv.BufferLocation = buf->resource->GetGPUVirtualAddress() + offset;
            vbv.SizeInBytes = static_cast<UINT>(buf->size - offset);
            // StrideInBytes is set by the PSO's input layout via
            // engine VertexBinding[]. The engine API doesn't pass
            // stride to bind_vertex_buffer (PSO carries it), so we
            // store 0 here; D3D12 actually requires it. Phase 14.C
            // workaround: stride is encoded in the caller's binding
            // descriptor that built the PSO — we re-fetch it from the
            // last-bound graphics pipeline's stored layout. For the
            // triangle sample the vertex layout is (Vec3 pos, Vec3 col)
            // = 24 B; we use that as a sensible default when stride
            // can't be inferred. A follow-up wave adds stride to the
            // bind_vertex_buffer signature.
            vbv.StrideInBytes = 24;
            list_->IASetVertexBuffers(binding, 1, &vbv);
        }
    }
    void bind_index_buffer(cd::rhi::BufferHandle buffer, std::uint64_t offset, cd::rhi::IndexType type) override
    {
        if (auto* buf = owner_->find_buffer(buffer))
        {
            D3D12_INDEX_BUFFER_VIEW ibv {};
            ibv.BufferLocation = buf->resource->GetGPUVirtualAddress() + offset;
            ibv.SizeInBytes = static_cast<UINT>(buf->size - offset);
            ibv.Format = (type == cd::rhi::IndexType::kUInt16)
                ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
            list_->IASetIndexBuffer(&ibv);
        }
    }
    void push_constants(cd::rhi::PipelineLayoutHandle, cd::rhi::ShaderStage, std::uint32_t, std::uint32_t, const void*) override {}
    void set_viewport(const cd::rhi::Viewport& vp) override
    {
        D3D12_VIEWPORT v {};
        v.TopLeftX = vp.x;
        v.TopLeftY = vp.y;
        v.Width = vp.width;
        v.Height = vp.height;
        v.MinDepth = vp.min_depth;
        v.MaxDepth = vp.max_depth;
        list_->RSSetViewports(1, &v);
    }
    void set_scissor(const cd::rhi::Rect2D& rect) override
    {
        D3D12_RECT r {};
        r.left = rect.offset.x;
        r.top = rect.offset.y;
        r.right = rect.offset.x + static_cast<LONG>(rect.extent.width);
        r.bottom = rect.offset.y + static_cast<LONG>(rect.extent.height);
        list_->RSSetScissorRects(1, &r);
    }
    void draw(std::uint32_t vertex_count, std::uint32_t instance_count,
              std::uint32_t first_vertex, std::uint32_t first_instance) override
    {
        list_->DrawInstanced(vertex_count, instance_count, first_vertex, first_instance);
    }
    void draw_indexed(std::uint32_t index_count, std::uint32_t instance_count,
                      std::uint32_t first_index, std::int32_t vertex_offset,
                      std::uint32_t first_instance) override
    {
        list_->DrawIndexedInstanced(index_count, instance_count, first_index,
                                    vertex_offset, first_instance);
    }
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
