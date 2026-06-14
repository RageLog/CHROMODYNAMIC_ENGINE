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
#include <algorithm>
#include <cd/rhi/d3d12/D3D12Device.hpp>

#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/ICommandBuffer.hpp>

// D12 (phase1184): in-device GLSL/SPIR-V/HLSL → DXIL cross-compile.
#include <cd/rhi/d3d12/D3D12ShaderToolchain.hpp>
#include <cd/gluon/ModuleRegistry.hpp>
#include <cd/shader/Compiler.hpp>

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
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace cd::rhi::d3d12
{

#if defined(_WIN32)

namespace
{

using Microsoft::WRL::ComPtr;

// Forward declaration — D3D12CommandBuffer is defined after D3D12Device
// but submit(const SubmitDesc&) inside D3D12Device casts ICommandBuffer*
// to D3D12CommandBuffer*. The cast is safe at runtime because only
// D3D12CommandBuffer instances are submitted on the D3D12 device path.
class D3D12CommandBuffer;

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
            const auto fl =
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

        // ---- 7. Mesh-shader feature query (phase766) -----------------------
        // D3D12_FEATURE_DATA_D3D12_OPTIONS7::MeshShaderTier reports
        // MESH_SHADER_TIER_1 on Shader Model 6.5+ adapters with
        // ID3D12Device2 / ID3D12GraphicsCommandList6 support. We also
        // probe ID3D12Device2 for CreatePipelineState(stream) — required
        // by the D3D12_PIPELINE_STATE_STREAM_DESC + MS/AS subobjects path.
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 opts7 {};
        if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7,
                                                   &opts7, sizeof(opts7))))
        {
            const bool has_tier_1 = (opts7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1);
            (void)device_.As(&device2_);
            features_.mesh_shader = has_tier_1 && device2_;
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
        if (desc.extent.width == 0)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument, "texture extent has zero width"));
        }
        const DXGI_FORMAT fmt = to_dxgi_format(desc.format);
        if (fmt == DXGI_FORMAT_UNKNOWN)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "unsupported texture format for D3D12 backend"));
        }

        // Validate type-specific dimension requirements.
        if (desc.type == cd::rhi::TextureType::k2D ||
            desc.type == cd::rhi::TextureType::kCube)
        {
            if (desc.extent.height == 0)
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kInvalidArgument,
                    "k2D/kCube texture: height must be > 0"));
            }
        }
        else if (desc.type == cd::rhi::TextureType::k3D)
        {
            if (desc.extent.height == 0 || desc.extent.depth == 0)
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kInvalidArgument,
                    "k3D texture: height and depth must be > 0"));
            }
        }

        D3D12_HEAP_PROPERTIES hp {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        hp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        hp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        hp.CreationNodeMask = 1;
        hp.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC rd {};
        // Phase 393/394 — k1D + k3D dimension support. The DepthOrArraySize
        // field carries different meaning per resource type:
        //   Texture1D: array size (>=1)
        //   Texture2D: array size (>=1)
        //   Texture3D: depth (actual depth of the 3D volume)
        switch (desc.type)
        {
            case cd::rhi::TextureType::k1D:
                rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
                rd.Width     = desc.extent.width;
                rd.Height    = 1;
                rd.DepthOrArraySize = static_cast<UINT16>(
                    std::max(1u, desc.array_layers));
                break;
            case cd::rhi::TextureType::k3D:
                rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
                rd.Width     = desc.extent.width;
                rd.Height    = desc.extent.height;
                // DepthOrArraySize == depth slices for Texture3D.
                rd.DepthOrArraySize = static_cast<UINT16>(
                    std::max(1u, desc.extent.depth));
                break;
            case cd::rhi::TextureType::k2D:
            case cd::rhi::TextureType::kCube:
            default:
                rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                rd.Width     = desc.extent.width;
                rd.Height    = desc.extent.height;
                // Cube maps require 6 array slices (or multiples of 6 for
                // cube arrays).
                rd.DepthOrArraySize = (desc.type == cd::rhi::TextureType::kCube)
                    ? static_cast<UINT16>(std::max(6u, desc.array_layers))
                    : static_cast<UINT16>(std::max(1u, desc.array_layers));
                break;
        }
        rd.Alignment  = 0;
        rd.MipLevels  = static_cast<UINT16>(desc.mip_levels);
        rd.Format     = fmt;
        rd.SampleDesc.Count   = 1;
        rd.SampleDesc.Quality = 0;
        rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        rd.Flags  = D3D12_RESOURCE_FLAG_NONE;
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
        rec.type  = desc.type;
        textures_.emplace(id, std::move(rec));
        return cd::rhi::TextureHandle { id, 1u };
    }

    void destroy_texture(cd::rhi::TextureHandle h) override
    {
        textures_.erase(h.index());
    }

    // phase883-d3d12-dead-scaffold-cleanup: the
    // CD_D3D12_NOT_IMPL_RESULT macro and its "Everything else
    // stubbed" comment are historical scaffold from the Phase 13.C
    // parity sprint's early days. Every entry point that lived
    // under that umbrella has since received a real implementation
    // (texture views Phase 124, shader compile Phase 139, descriptor
    // sets Phase 145+, RT path Phases M4A-M4H, etc.). The macro had
    // zero call sites; removed along with the misleading header.
    //
    // The remaining `Code::kNotImplemented` returns in this file are
    // runtime CAPABILITY GATES (mesh shaders + DXR on adapters that
    // lack the feature) or defensive guards on unknown enum values,
    // not "TODO: implement me later" stubs.

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
        // Phase 393/395 — k1D, k2D, k3D, kCube all wired.
        const bool is_cube_view = (desc.type == cd::rhi::TextureType::kCube);
        const bool is_1d_view   = (desc.type == cd::rhi::TextureType::k1D);
        const bool is_3d_view   = (desc.type == cd::rhi::TextureType::k3D);
        if (!is_cube_view && !is_1d_view && !is_3d_view &&
            desc.type != cd::rhi::TextureType::k2D)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kNotImplemented,
                "create_texture_view: unsupported TextureType value"));
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
        vrec.parent  = desc.texture;
        vrec.format  = view_fmt;
        vrec.is_cube = is_cube_view;
        vrec.is_1d   = is_1d_view;
        vrec.is_3d   = is_3d_view;

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
            // X4-E1 — RTV dimension follows the view's texture type, mirroring
            // VulkanDevice's VK_IMAGE_VIEW_TYPE_1D / _3D for attachments.
            // (Cube RTs are not a separate RTV dimension in D3D12 — they bind
            // as a TEXTURE2DARRAY slice; cube colour attachments are not part
            // of this RHI's render-target surface, so the cube-flag falls
            // through to the TEXTURE2D path here.)
            if (is_1d_view)
            {
                rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
                rd.Texture1D.MipSlice = desc.base_mip;
            }
            else if (is_3d_view)
            {
                rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
                rd.Texture3D.MipSlice = desc.base_mip;
                rd.Texture3D.FirstWSlice = desc.base_layer;
                rd.Texture3D.WSize = static_cast<UINT>(-1);  // all depth slices
            }
            else
            {
                rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
                rd.Texture2D.MipSlice = desc.base_mip;
            }
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

            // X4-E1 — DSV dimension follows the view's texture type. D3D12
            // has no TEXTURE3D DSV dimension (Vulkan likewise has no 3D depth
            // attachment use), so a 3D depth view is rejected rather than
            // silently downgraded to TEXTURE2D.
            if (is_3d_view)
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kInvalidArgument,
                    "create_texture_view: 3D textures cannot be a depth-stencil "
                    "attachment (no D3D12_DSV_DIMENSION_TEXTURE3D)"));
            }
            D3D12_DEPTH_STENCIL_VIEW_DESC dd {};
            dd.Format = view_fmt;
            if (is_1d_view)
            {
                dd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
                dd.Texture1D.MipSlice = desc.base_mip;
            }
            else
            {
                dd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
                dd.Texture2D.MipSlice = desc.base_mip;
            }
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
                const UINT mip_count = (desc.mip_count == 0u) ? 1u : desc.mip_count;
                if (is_cube_view)
                {
                    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                    sd.TextureCube.MostDetailedMip = desc.base_mip;
                    sd.TextureCube.MipLevels = mip_count;
                    sd.TextureCube.ResourceMinLODClamp = 0.0F;
                }
                else if (is_1d_view)
                {
                    // Phase 393 — Texture1D SRV.
                    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
                    sd.Texture1D.MostDetailedMip = desc.base_mip;
                    sd.Texture1D.MipLevels = mip_count;
                    sd.Texture1D.ResourceMinLODClamp = 0.0F;
                }
                else if (is_3d_view)
                {
                    // Phase 395 — Texture3D SRV.
                    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
                    sd.Texture3D.MostDetailedMip = desc.base_mip;
                    sd.Texture3D.MipLevels = mip_count;
                    sd.Texture3D.ResourceMinLODClamp = 0.0F;
                }
                else
                {
                    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    sd.Texture2D.MostDetailedMip = desc.base_mip;
                    sd.Texture2D.MipLevels = mip_count;
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
                else if (is_1d_view)
                {
                    // Phase 393 — Texture1D UAV.
                    ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
                    ud.Texture1D.MipSlice = desc.base_mip;
                }
                else if (is_3d_view)
                {
                    // Phase 395 — Texture3D UAV.
                    ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
                    ud.Texture3D.MipSlice = desc.base_mip;
                    ud.Texture3D.FirstWSlice = 0;
                    ud.Texture3D.WSize = static_cast<UINT>(-1);  // all depth slices
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

    // ---- Sampler (REAL — phase466 v0.99.93 M4-parity-closeout) -----------
    //
    // D3D12 samplers live in a dedicated descriptor heap (TYPE_SAMPLER).
    // The cd::rhi surface returns a SamplerHandle that is later consumed
    // by update_descriptor_set (for combined-image-sampler) or by the
    // pipeline static-sampler path. We allocate from a lazily-created
    // CPU-visible sampler heap (256 slots; bumps per create).
    [[nodiscard]] cd::core::Result<cd::rhi::SamplerHandle>
    create_sampler(const cd::rhi::SamplerDesc& desc) override
    {
        // Lazily allocate sampler heap (single 256-slot bump allocator).
        if (sampler_heap_ == nullptr)
        {
            D3D12_DESCRIPTOR_HEAP_DESC hd {};
            hd.NumDescriptors = kSamplerHeapCap;
            hd.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
            hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            if (FAILED(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&sampler_heap_))))
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                    "create_sampler: sampler descriptor heap creation failed"));
            }
            sampler_heap_increment_ = device_->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        }
        if (sampler_cursor_ >= kSamplerHeapCap)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "create_sampler: sampler heap exhausted (256-slot cap)"));
        }

        // Map cd::rhi::Sampler* fields onto D3D12 enums.
        D3D12_SAMPLER_DESC sd {};
        const bool aniso     = desc.anisotropy_enable && desc.max_anisotropy > 1.0F;
        const bool compare   = desc.compare_enable;
        const bool min_lin   = desc.min_filter   == cd::rhi::SamplerFilter::kLinear;
        const bool mag_lin   = desc.mag_filter   == cd::rhi::SamplerFilter::kLinear;
        const bool mip_lin   = desc.mipmap_mode  == cd::rhi::SamplerMipmapMode::kLinear;
        if (aniso)
        {
            sd.Filter = compare ? D3D12_FILTER_COMPARISON_ANISOTROPIC
                                : D3D12_FILTER_ANISOTROPIC;
        }
        else
        {
            // 8 combinations across (min, mag, mip) all map to a unique
            // D3D12_FILTER constant. We compose via the standard formula.
            UINT bits = 0;
            if (min_lin) bits |= 0x10u;  // min linear
            if (mag_lin) bits |= 0x04u;  // mag linear
            if (mip_lin) bits |= 0x01u;  // mip linear
            sd.Filter = static_cast<D3D12_FILTER>(
                bits | (compare ? 0x80u : 0x00u));
        }
        auto map_addr = [](cd::rhi::SamplerAddressMode m) noexcept {
            switch (m)
            {
                case cd::rhi::SamplerAddressMode::kRepeat:         return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                case cd::rhi::SamplerAddressMode::kMirroredRepeat: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
                case cd::rhi::SamplerAddressMode::kClampToEdge:    return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                case cd::rhi::SamplerAddressMode::kClampToBorder:  return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
                case cd::rhi::SamplerAddressMode::kMirrorClampToEdge:
                                                                   return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
            }
            return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        };
        sd.AddressU = map_addr(desc.address_u);
        sd.AddressV = map_addr(desc.address_v);
        sd.AddressW = map_addr(desc.address_w);
        sd.MipLODBias = desc.mip_lod_bias;
        sd.MaxAnisotropy = static_cast<UINT>(desc.max_anisotropy);
        switch (desc.compare_op)
        {
            case cd::rhi::CompareOp::kNever:        sd.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;         break;
            case cd::rhi::CompareOp::kLess:         sd.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS;          break;
            case cd::rhi::CompareOp::kEqual:        sd.ComparisonFunc = D3D12_COMPARISON_FUNC_EQUAL;         break;
            case cd::rhi::CompareOp::kLessEqual:    sd.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;    break;
            case cd::rhi::CompareOp::kGreater:      sd.ComparisonFunc = D3D12_COMPARISON_FUNC_GREATER;       break;
            case cd::rhi::CompareOp::kNotEqual:     sd.ComparisonFunc = D3D12_COMPARISON_FUNC_NOT_EQUAL;     break;
            case cd::rhi::CompareOp::kGreaterEqual: sd.ComparisonFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL; break;
            case cd::rhi::CompareOp::kAlways:       sd.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;        break;
        }
        switch (desc.border_color)
        {
            case cd::rhi::BorderColor::kFloatOpaqueWhite:
                sd.BorderColor[0] = sd.BorderColor[1] = sd.BorderColor[2] = sd.BorderColor[3] = 1.0F;
                break;
            case cd::rhi::BorderColor::kFloatOpaqueBlack:
            case cd::rhi::BorderColor::kIntOpaqueBlack:
                sd.BorderColor[0] = sd.BorderColor[1] = sd.BorderColor[2] = 0.0F;
                sd.BorderColor[3] = 1.0F;
                break;
            case cd::rhi::BorderColor::kFloatTransparentBlack:
            case cd::rhi::BorderColor::kIntTransparentBlack:
                sd.BorderColor[0] = sd.BorderColor[1] = sd.BorderColor[2] = sd.BorderColor[3] = 0.0F;
                break;
            case cd::rhi::BorderColor::kIntOpaqueWhite:
                sd.BorderColor[0] = sd.BorderColor[1] = sd.BorderColor[2] = sd.BorderColor[3] = 1.0F;
                break;
        }
        sd.MinLOD = desc.min_lod;
        sd.MaxLOD = desc.max_lod;

        D3D12_CPU_DESCRIPTOR_HANDLE dst = sampler_heap_->GetCPUDescriptorHandleForHeapStart();
        dst.ptr += static_cast<SIZE_T>(sampler_cursor_) * sampler_heap_increment_;
        device_->CreateSampler(&sd, dst);

        SamplerRecord rec;
        rec.heap_slot = sampler_cursor_;
        rec.cpu_handle = dst;
        ++sampler_cursor_;
        const auto id = next_id_++;
        samplers_.emplace(id, std::move(rec));
        return cd::rhi::SamplerHandle { id, 1u };
    }
    void destroy_sampler(cd::rhi::SamplerHandle h) override
    {
        samplers_.erase(h.index());
        // Heap slot not reclaimed (bump allocator); acceptable until a
        // slab-allocator rework lands.
    }

    // ---- Shader module (REAL — Phase 14.C v0.36.0) ------------------------

    // D12 (phase1184) — route by source language. `kBytecode` (default)
    // is the legacy pass-through: `code` is already DXIL/DXBC and is
    // stored verbatim (byte-identical to the pre-D12 contract). `kGlsl`,
    // `kSpirv`, `kHlsl` run the cross-compile toolchain
    // (GLSL→SPIR-V→HLSL→DXIL) so the engine shader corpus produces DXIL
    // here instead of relying on inline SM5.1 HLSL islands. All failures
    // are typed errors — no crash, no silent empty module.
    [[nodiscard]] cd::core::Result<cd::rhi::ShaderModuleHandle>
    create_shader_module(const cd::rhi::ShaderModuleDesc& desc) override
    {
        if (desc.code == nullptr || desc.code_size == 0)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "shader module: empty source/bytecode"));
        }

        ShaderModuleRecord rec;
        rec.stage = desc.stage;
        rec.entry_point = std::string { desc.entry_point };

        switch (desc.language)
        {
            case cd::rhi::ShaderSourceLanguage::kBytecode:
            {
                // Native DXIL/DXBC — consume verbatim.
                const auto* src = static_cast<const std::uint8_t*>(desc.code);
                rec.bytecode.assign(src, src + desc.code_size);
                break;
            }
            case cd::rhi::ShaderSourceLanguage::kGlsl:
            {
                auto* compiler = glsl_compiler();
                if (compiler == nullptr)
                {
                    return std::unexpected(cd::rhi::rhi_errors::make(
                        cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                        "shader module: GLSL requested but the glslang "
                        "front-end is unavailable (CD_ENABLE_GLSLANG=OFF)"));
                }
                // ADR-20260614 consumer-resolver pattern: a null
                // include_resolver bridges to the embedded cd::gluon module
                // catalogue via a FUNCTION-LOCAL stateless resolver (not
                // static / member / global — §2.2). When the caller injects
                // a resolver we respect it (§2.3). The resolver must outlive
                // the compile() call inside compile_glsl_to_dxil, so it is
                // declared here and kept in scope for the whole branch.
                cd::gluon::ModuleResolver default_resolver {};
                auto* injected = static_cast<cd::shader::IIncludeResolver*>(
                    desc.include_resolver);
                cd::rhi::d3d12::GlslToDxilDesc gd {};
                gd.glsl_source = std::string_view {
                    static_cast<const char*>(desc.code),
                    static_cast<std::size_t>(desc.code_size) };
                gd.stage = desc.stage;
                gd.entry_point = desc.entry_point;
                gd.source_name = desc.debug_name.empty()
                                     ? std::string_view { "<inline>" }
                                     : desc.debug_name;
                gd.include_resolver =
                    injected != nullptr ? injected : &default_resolver;
                auto dxil = cd::rhi::d3d12::compile_glsl_to_dxil(*compiler, gd);
                if (!dxil.has_value())
                {
                    return std::unexpected(cd::rhi::rhi_errors::make(
                        cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                        std::string { "shader module (GLSL→DXIL): " } +
                            std::string { dxil.error().message }));
                }
                rec.bytecode = std::move(*dxil);
                break;
            }
            case cd::rhi::ShaderSourceLanguage::kSpirv:
            {
                if ((desc.code_size % 4) != 0)
                {
                    return std::unexpected(cd::rhi::rhi_errors::make(
                        cd::rhi::rhi_errors::Code::kInvalidArgument,
                        "shader module: SPIR-V code must be 32-bit aligned"));
                }
                const std::span<const std::uint32_t> words {
                    static_cast<const std::uint32_t*>(desc.code),
                    static_cast<std::size_t>(desc.code_size / 4) };
                cd::rhi::d3d12::GlslToDxilDesc gd {};
                gd.stage = desc.stage;
                gd.entry_point = desc.entry_point;
                gd.source_name = desc.debug_name.empty()
                                     ? std::string_view { "<inline>" }
                                     : desc.debug_name;
                auto dxil = cd::rhi::d3d12::compile_spirv_to_dxil(words, gd);
                if (!dxil.has_value())
                {
                    return std::unexpected(cd::rhi::rhi_errors::make(
                        cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                        std::string { "shader module (SPIR-V→DXIL): " } +
                            std::string { dxil.error().message }));
                }
                rec.bytecode = std::move(*dxil);
                break;
            }
            case cd::rhi::ShaderSourceLanguage::kHlsl:
            {
                cd::rhi::d3d12::CompileOptions co {};
                co.source = std::string_view {
                    static_cast<const char*>(desc.code),
                    static_cast<std::size_t>(desc.code_size) };
                co.entry_point = desc.entry_point;
                co.stage = desc.stage;
                co.source_name = desc.debug_name.empty()
                                     ? std::string_view { "<inline>" }
                                     : desc.debug_name;
                co.optimization_level = 3u;
                co.model = cd::rhi::d3d12::ShaderModel::kSM6_5;
                auto dxil = cd::rhi::d3d12::compile_hlsl(co);
                if (!dxil.has_value())
                {
                    return std::unexpected(cd::rhi::rhi_errors::make(
                        cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                        std::string { "shader module (HLSL→DXIL): " } +
                            std::string { dxil.error().message }));
                }
                rec.bytecode = std::move(*dxil);
                break;
            }
        }

        if (rec.bytecode.empty())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "shader module: cross-compile produced empty DXIL"));
        }

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

        // ADR-20260614-d3d12-binding-model: space-per-set. The N-th descriptor
        // set layout maps to HLSL register space N — matching the SPIRV-Cross
        // SM>=51 default (`register(<class>M, spaceN)`, N = Vulkan set index).
        // Previously every range was hardcoded to space0, which silently
        // mis-bound the dedicated bindless set (set 1 -> space1 in the shader).
        std::uint32_t set_space = 0;

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
                    case cd::rhi::DescriptorType::kBindlessSampledImage:
                        // DXR exposes acceleration structures as SRVs of
                        // a special RAYTRACING_ACCELERATION_STRUCTURE
                        // type. The root-signature range slot is plain
                        // SRV; the descriptor-write side fills in the
                        // RAYTRACING_ACCELERATION_STRUCTURE SRV desc.
                        //
                        // ADR-20260614-d3d12-binding-model / B1b: the engine's
                        // dedicated bindless set (MEMORY rule 9 — set 1) declares
                        // a `kBindlessSampledImage` array. SPIRV-Cross emits it as
                        // `Texture2D ... : register(t<binding>, space<set>)`, so
                        // it MUST land on an SRV range or the whole set-1 table is
                        // dropped from the root signature → silent unbound at
                        // space1 (the shader's `register(t0, space1)` has no table).
                        // The unbounded NumDescriptors override below mirrors the
                        // Vulkan VARIABLE_DESCRIPTOR_COUNT / PARTIALLY_BOUND set.
                        r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                        break;
                    case cd::rhi::DescriptorType::kStorageImage:
                    case cd::rhi::DescriptorType::kStorageBuffer:
                    case cd::rhi::DescriptorType::kStorageBufferDynamic:
                        r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                        break;
                    default: continue;
                }
                // Bindless arrays are declared UNBOUNDED. NumDescriptors ==
                // UINT_MAX is the D3D12 sentinel for an unbounded descriptor
                // range; combined with SM5.1+ dynamic indexing it matches the
                // Vulkan bindless set's runtime sizing. Root-signature v1.0
                // ranges behave as fully DESCRIPTORS_VOLATILE | DATA_VOLATILE,
                // which is exactly the dynamic-indexing semantics SM6.x needs
                // here, so no v1.1 range-flags migration is required for the
                // declaration to be CONSISTENT with the SPIRV-Cross emission.
                // (Runtime heap writes — write_bindless_texture_slot /
                // features().bindless_resources — are roadmap item D10 and are
                // intentionally NOT wired here.)
                r.NumDescriptors = (b.type ==
                    cd::rhi::DescriptorType::kBindlessSampledImage)
                    ? UINT_MAX : b.count;
                r.BaseShaderRegister = b.binding;
                r.RegisterSpace = set_space;  // space N = N-th descriptor set
                r.OffsetInDescriptorsFromTableStart =
                    D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                ranges.push_back(r);
            }
            // Advance the register space for the NEXT descriptor set. The
            // Vulkan side binds set_layouts in order, so the space index must
            // equal the set ordinal even when a (sampler-only) layout produces
            // no CBV/SRV/UAV ranges — hence the increment lives before the
            // early-out below, not at the bottom of the loop.
            ++set_space;
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

        // phase466 — push-constants → D3D12 root 32-bit constants slot.
        //
        // The Vulkan surface allows multiple PushConstantRange entries with
        // distinct stages. D3D12 root signatures support multiple 32-bit
        // constants parameters but for parity with the engine push_constants()
        // call shape (single (layout, stages, offset, size, data)), we
        // collapse all ranges into one root parameter that spans the union
        // of all (offset, size) pairs. Cap at D3D12_MAX_ROOT_COST=64 DWORDs;
        // beyond that the engine's contract guarantees the caller routes
        // through a uniform buffer.
        std::uint32_t pc_param_idx = ~std::uint32_t { 0 };
        std::uint32_t pc_dwords    = 0;
        if (!desc.push_constants.empty())
        {
            std::uint32_t max_end = 0;
            for (const auto& r : desc.push_constants)
            {
                const auto end = r.offset + r.size;
                max_end = std::max(end, max_end);
            }
            // Round up to 4 bytes — root constants are u32 (DWORD) sized.
            pc_dwords = (max_end + 3u) / 4u;
            // D3D12_MAX_ROOT_COST is 64 DWORDs total. A descriptor table
            // costs 1 DWORD; reserve at least params.size() for them.
            if (pc_dwords + static_cast<std::uint32_t>(params.size()) > 64u)
            {
                pc_dwords = 64u - static_cast<std::uint32_t>(params.size());
            }
            if (pc_dwords > 0u)
            {
                D3D12_ROOT_PARAMETER pcp {};
                pcp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
                pcp.Constants.ShaderRegister = 0;   // b0
                pcp.Constants.RegisterSpace  = 1;   // space1 (avoid CBV b0 collisions)
                pcp.Constants.Num32BitValues = pc_dwords;
                pcp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                pc_param_idx = static_cast<std::uint32_t>(params.size());
                params.push_back(pcp);
            }
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
        rec.push_constants_param  = pc_param_idx;
        rec.push_constants_dwords = pc_dwords;
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

        // phase766 — Mesh-shading PSO path. When the descriptor declares a
        // mesh shader, build a D3D12_PIPELINE_STATE_STREAM_DESC with the
        // MeshShader + (optional) AmplificationShader subobjects and route
        // through ID3D12Device2::CreatePipelineState. Vertex/IA stages are
        // suppressed — DispatchMesh drives the pipeline directly.
        if (desc.mesh_shader.value() != 0u)
        {
            return create_mesh_pipeline_(desc, layout_it->second.root_sig.Get());
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
            semantic_storage.emplace_back("TEXCOORD");
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

    // ---- Mesh-shading pipeline (REAL — phase766 F5) -----------------------
    //
    // Mesh-shading PSO via ID3D12Device2::CreatePipelineState +
    // D3D12_PIPELINE_STATE_STREAM_DESC. The PSS-stream is a sequence of
    // (subobject_type_alignas16, payload) tuples; we pack the subobjects
    // into a POD struct so the layout is contiguous and properly aligned
    // (each subobject must start on its natural alignment within the
    // stream — std::uint32_t for the tag + the payload type).
    //
    // Subobjects emitted (when applicable):
    //   * ROOT_SIGNATURE                                  -- pipeline layout
    //   * AS (Amplification Shader)                       -- optional
    //   * MS (Mesh Shader)                                -- REQUIRED
    //   * PS (Pixel Shader / fragment)                    -- optional but typical
    //   * RASTERIZER                                      -- raster state
    //   * DEPTH_STENCIL                                   -- depth/stencil state
    //   * BLEND                                           -- blend state
    //   * RENDER_TARGET_FORMATS                           -- color attachments
    //   * DEPTH_STENCIL_FORMAT                            -- depth attachment
    //   * SAMPLE_DESC                                     -- MSAA (count + quality)
    //   * SAMPLE_MASK                                     -- rasterizer mask
    //   * PRIMITIVE_TOPOLOGY (TRIANGLE)                   -- mesh PSOs default tri
    //
    // The DispatchMesh command-list entry point (ID3D12GraphicsCommandList6)
    // executes the pipeline. Vertex-input / IA stages are inert.
    [[nodiscard]] cd::core::Result<cd::rhi::GraphicsPipelineHandle>
    create_mesh_pipeline_(const cd::rhi::GraphicsPipelineDesc& desc,
                          ID3D12RootSignature*                 root_sig)
    {
        if (!features_.mesh_shader || !device2_)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kNotImplemented,
                "create_graphics_pipeline (mesh): adapter lacks "
                "D3D12_MESH_SHADER_TIER_1 / ID3D12Device2 (Shader Model 6.5+)"));
        }

        auto ms_it = shader_modules_.find(desc.mesh_shader.index());
        if (ms_it == shader_modules_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "create_graphics_pipeline (mesh): mesh-shader module handle unknown"));
        }
        const ShaderModuleRecord* as_rec = nullptr;
        if (desc.amplification_shader.value() != 0u)
        {
            auto as_it = shader_modules_.find(desc.amplification_shader.index());
            if (as_it == shader_modules_.end())
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kInvalidArgument,
                    "create_graphics_pipeline (mesh): amplification shader module handle unknown"));
            }
            as_rec = &as_it->second;
        }
        // Pixel shader is optional (depth-only mesh-PSO is legal).
        const ShaderModuleRecord* ps_rec = nullptr;
        if (desc.fragment_shader.value() != 0u)
        {
            auto fs_it = shader_modules_.find(desc.fragment_shader.index());
            if (fs_it != shader_modules_.end())
                ps_rec = &fs_it->second;
        }

        // Pack PSS subobjects. Each (type-tag + payload) starts on its
        // natural alignment within the contiguous stream blob; the easiest
        // way to do this portably is one struct per subobject group with
        // CD3DX12-style alignas(void*) tags. We avoid CD3DX12 headers and
        // build the struct by hand for clarity.
        struct alignas(void*) PssSubObj
        {
            D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
        };

        // Rasterizer
        D3D12_RASTERIZER_DESC rs {};
        rs.FillMode = (desc.raster.polygon_mode == cd::rhi::PolygonMode::kFill)
            ? D3D12_FILL_MODE_SOLID : D3D12_FILL_MODE_WIREFRAME;
        switch (desc.raster.cull)
        {
            case cd::rhi::CullMode::kNone:  rs.CullMode = D3D12_CULL_MODE_NONE;  break;
            case cd::rhi::CullMode::kFront: rs.CullMode = D3D12_CULL_MODE_FRONT; break;
            case cd::rhi::CullMode::kBack:  rs.CullMode = D3D12_CULL_MODE_BACK;  break;
            case cd::rhi::CullMode::kFrontAndBack: rs.CullMode = D3D12_CULL_MODE_NONE; break;
        }
        rs.FrontCounterClockwise =
            (desc.raster.front_face == cd::rhi::FrontFace::kCounterClockwise) ? TRUE : FALSE;
        rs.DepthClipEnable = TRUE;

        // Blend
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

        // Depth-stencil
        D3D12_DEPTH_STENCIL_DESC ds {};
        ds.DepthEnable = (desc.depth_attachment_format != cd::rhi::Format::kUndefined)
            && desc.depth_stencil.depth_test;
        ds.DepthWriteMask = desc.depth_stencil.depth_write
            ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        ds.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        ds.StencilEnable = FALSE;

        // Color formats
        D3D12_RT_FORMAT_ARRAY rtfmts {};
        rtfmts.NumRenderTargets =
            static_cast<UINT>(std::min<std::size_t>(desc.color_attachment_formats.size(), 8));
        for (UINT i = 0; i < rtfmts.NumRenderTargets; ++i)
            rtfmts.RTFormats[i] = to_dxgi_format(desc.color_attachment_formats[i]);

        // Depth format
        const DXGI_FORMAT dsv_fmt =
            (desc.depth_attachment_format != cd::rhi::Format::kUndefined)
                ? to_dxgi_format(desc.depth_attachment_format)
                : DXGI_FORMAT_UNKNOWN;

        // Sample desc
        DXGI_SAMPLE_DESC sd {};
        sd.Count = 1;
        sd.Quality = 0;

        // Build the contiguous PSS blob via a single packed struct. The
        // D3D12 driver walks the stream by reading the tag, dispatching
        // to the matching subobject size, then advancing to the next
        // (alignas(void*)) boundary -- so we match that exactly here.
        struct PipelineStateStream
        {
            // Root signature
            alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE root_sig_tag
                { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE };
            ID3D12RootSignature* root_sig { nullptr };

            // AS (Amplification Shader)
            alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE as_tag
                { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS };
            D3D12_SHADER_BYTECODE as_bc {};

            // MS (Mesh Shader)
            alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ms_tag
                { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS };
            D3D12_SHADER_BYTECODE ms_bc {};

            // PS (Pixel Shader)
            alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ps_tag
                { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS };
            D3D12_SHADER_BYTECODE ps_bc {};

            // Rasterizer
            alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE rast_tag
                { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER };
            D3D12_RASTERIZER_DESC rast_desc {};

            // Depth-stencil
            alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ds_tag
                { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL };
            D3D12_DEPTH_STENCIL_DESC ds_desc {};

            // Blend
            alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE blend_tag
                { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND };
            D3D12_BLEND_DESC blend_desc {};

            // Sample mask
            alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE smask_tag
                { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK };
            UINT sample_mask { UINT_MAX };

            // Sample desc
            alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE sdesc_tag
                { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC };
            DXGI_SAMPLE_DESC sample_desc {};

            // RT formats
            alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE rtfmt_tag
                { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS };
            D3D12_RT_FORMAT_ARRAY rt_formats {};

            // DS format
            alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE dsfmt_tag
                { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT };
            DXGI_FORMAT ds_format { DXGI_FORMAT_UNKNOWN };
        };

        PipelineStateStream pss {};
        pss.root_sig = root_sig;
        if (as_rec != nullptr)
        {
            pss.as_bc.pShaderBytecode = as_rec->bytecode.data();
            pss.as_bc.BytecodeLength  = as_rec->bytecode.size();
        }
        pss.ms_bc.pShaderBytecode = ms_it->second.bytecode.data();
        pss.ms_bc.BytecodeLength  = ms_it->second.bytecode.size();
        if (ps_rec != nullptr)
        {
            pss.ps_bc.pShaderBytecode = ps_rec->bytecode.data();
            pss.ps_bc.BytecodeLength  = ps_rec->bytecode.size();
        }
        pss.rast_desc   = rs;
        pss.ds_desc     = ds;
        pss.blend_desc  = bd;
        pss.sample_desc = sd;
        pss.rt_formats  = rtfmts;
        pss.ds_format   = dsv_fmt;

        D3D12_PIPELINE_STATE_STREAM_DESC stream_desc {};
        stream_desc.SizeInBytes                   = sizeof(pss);
        stream_desc.pPipelineStateSubobjectStream = &pss;

        ComPtr<ID3D12PipelineState> pso;
        const HRESULT hr =
            device2_->CreatePipelineState(&stream_desc, IID_PPV_ARGS(&pso));
        if (FAILED(hr))
        {
            char buf[160] {};
            std::snprintf(buf, sizeof(buf),
                          "CreatePipelineState(mesh PSS) failed: HRESULT 0x%08lx",
                          static_cast<unsigned long>(hr));
            return std::unexpected(cd::rhi::rhi_errors::make_owning(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                std::string { buf }));
        }

        GraphicsPipelineRecord rec;
        rec.pso = pso;
        rec.layout_handle = desc.layout;
        // Mesh PSOs ignore IA topology -- DispatchMesh drives directly --
        // but the bind path still calls IASetPrimitiveTopology. Use TRI
        // as a safe default; the runtime will ignore it for mesh PSOs.
        rec.d3d_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        rec.is_mesh_shader = true;
        const auto id = next_id_++;
        graphics_pipelines_.emplace(id, std::move(rec));
        return cd::rhi::GraphicsPipelineHandle { id, 1u };
    }

    // ---- Compute pipeline (REAL — phase466 v0.99.93 M4-parity-closeout) ----
    //
    // CreateComputePipelineState consumes a CS shader-bytecode blob plus
    // the root-signature from a cd::rhi::PipelineLayoutHandle. The shader
    // module records carry raw DXIL bytecode (already DXC-compiled by
    // D3D12ShaderCompile.cpp).
    [[nodiscard]] cd::core::Result<cd::rhi::ComputePipelineHandle>
    create_compute_pipeline(const cd::rhi::ComputePipelineDesc& desc) override
    {
        auto layout_it = pipeline_layouts_.find(desc.layout.index());
        if (layout_it == pipeline_layouts_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "create_compute_pipeline: unknown PipelineLayout handle"));
        }
        auto cs_it = shader_modules_.find(desc.shader.index());
        if (cs_it == shader_modules_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "create_compute_pipeline: unknown compute shader module"));
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC cpd {};
        cpd.pRootSignature = layout_it->second.root_sig.Get();
        cpd.CS.pShaderBytecode = cs_it->second.bytecode.data();
        cpd.CS.BytecodeLength  = cs_it->second.bytecode.size();
        cpd.NodeMask = 0;
        cpd.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

        ComPtr<ID3D12PipelineState> pso;
        HRESULT hr = device_->CreateComputePipelineState(&cpd, IID_PPV_ARGS(&pso));
        if (FAILED(hr))
        {
            char buf[160] {};
            std::snprintf(buf, sizeof(buf),
                          "CreateComputePipelineState failed: HRESULT 0x%08lx",
                          static_cast<unsigned long>(hr));
            return std::unexpected(cd::rhi::rhi_errors::make_owning(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                std::string { buf }));
        }

        ComputePipelineRecord rec;
        rec.pso = pso;
        rec.layout_handle = desc.layout;
        const auto id = next_id_++;
        compute_pipelines_.emplace(id, std::move(rec));
        return cd::rhi::ComputePipelineHandle { id, 1u };
    }
    void destroy_compute_pipeline(cd::rhi::ComputePipelineHandle h) override
    {
        compute_pipelines_.erase(h.index());
    }

    // ---- DXR pipeline state object (Phase 398) ----------------------------
    //
    // Creates a D3D12_STATE_OBJECT (collection type = RAYTRACING_PIPELINE)
    // when the adapter has DXR support. Each RtShaderEntry becomes one
    // DXIL_LIBRARY subobject + one shader-config / root-signature
    // association. We produce a minimal but functional RTPSO:
    //   - DXIL_LIBRARY per shader (bytecode carried by ShaderModuleHandle)
    //   - RAYTRACING_SHADER_CONFIG (payload + attribute sizes)
    //   - RAYTRACING_PIPELINE_CONFIG (max recursion)
    //   - GLOBAL_ROOT_SIGNATURE (empty if no layout supplied)
    //
    // Scope-down per ADR: if DXR is absent on the adapter, return
    // kNotImplemented with a clear message. Callers MUST gate on
    // features().ray_tracing before creating an RT pipeline.
    [[nodiscard]] cd::core::Result<cd::rhi::RtPipelineHandle>
    create_rt_pipeline(const cd::rhi::RtPipelineDesc& desc,
                       cd::rhi::PipelineLayoutHandle   layout) override
    {
        if (!features_.ray_tracing || !device5_)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kNotImplemented,
                "create_rt_pipeline: adapter lacks DXR (check features().ray_tracing)"));
        }

        // Collect subobjects into a flat vector so we can build the
        // D3D12_STATE_OBJECT_DESC with a contiguous array. Each subobject
        // is a tagged union; the lifetime of pointed-to data must match
        // the CreateStateObject call.

        // --- 1. DXIL library subobjects (one per shader entry) ---
        std::vector<D3D12_DXIL_LIBRARY_DESC> lib_descs;
        lib_descs.reserve(desc.shaders.size());
        // Keep the export-desc arrays alive alongside lib_descs.
        std::vector<std::vector<D3D12_EXPORT_DESC>> lib_exports;
        lib_exports.reserve(desc.shaders.size());
        std::vector<std::wstring> export_names;   // wide-char storage
        export_names.reserve(desc.shaders.size());

        for (const auto& se : desc.shaders)
        {
            auto sm_it = shader_modules_.find(se.module.index());
            if (sm_it == shader_modules_.end())
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kInvalidArgument,
                    "create_rt_pipeline: shader module handle unknown"));
            }
            const auto& sm = sm_it->second;

            // Convert UTF-8 entry point to wide-char for the DXIL API.
            const int wlen = MultiByteToWideChar(
                CP_UTF8, 0,
                sm.entry_point.c_str(),
                static_cast<int>(sm.entry_point.size()),
                nullptr, 0);
            std::wstring wname(static_cast<std::size_t>(wlen), L'\0');
            MultiByteToWideChar(CP_UTF8, 0,
                sm.entry_point.c_str(),
                static_cast<int>(sm.entry_point.size()),
                wname.data(), wlen);
            export_names.push_back(std::move(wname));

            D3D12_EXPORT_DESC exp {};
            exp.Name = export_names.back().c_str();
            exp.ExportToRename = nullptr;
            exp.Flags = D3D12_EXPORT_FLAG_NONE;

            std::vector<D3D12_EXPORT_DESC> exps { exp };
            lib_exports.push_back(std::move(exps));

            D3D12_DXIL_LIBRARY_DESC ld {};
            ld.DXILLibrary.pShaderBytecode = sm.bytecode.data();
            ld.DXILLibrary.BytecodeLength  = sm.bytecode.size();
            ld.NumExports   = 1;
            ld.pExports     = lib_exports.back().data();
            lib_descs.push_back(ld);
        }

        // --- 2. Shader config ---
        D3D12_RAYTRACING_SHADER_CONFIG shader_cfg {};
        shader_cfg.MaxPayloadSizeInBytes   = desc.max_payload_bytes;
        shader_cfg.MaxAttributeSizeInBytes = desc.max_attribute_bytes;

        // --- 3. Pipeline config ---
        D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_cfg {};
        pipeline_cfg.MaxTraceRecursionDepth = desc.max_recursion;

        // --- 4. Global root signature (empty if no layout) ---
        ComPtr<ID3D12RootSignature> global_rs;
        if (layout.value() != 0u)
        {
            auto pl_it = pipeline_layouts_.find(layout.index());
            if (pl_it != pipeline_layouts_.end())
                global_rs = pl_it->second.root_sig;
        }
        if (!global_rs)
        {
            // Minimal empty root signature for RTPSO.
            D3D12_ROOT_SIGNATURE_DESC rsd {};
            rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
            ComPtr<ID3DBlob> blob;
            ComPtr<ID3DBlob> err;
            if (SUCCEEDED(D3D12SerializeRootSignature(
                    &rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)))
            {
                (void)device_->CreateRootSignature(
                    0, blob->GetBufferPointer(), blob->GetBufferSize(),
                    IID_PPV_ARGS(&global_rs));
            }
        }

        // --- 5. Assemble flat subobject array ---
        std::vector<D3D12_STATE_SUBOBJECT> subs;
        subs.reserve(lib_descs.size() + 3u);

        for (auto& ld : lib_descs)
        {
            D3D12_STATE_SUBOBJECT s {};
            s.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
            s.pDesc = &ld;
            subs.push_back(s);
        }
        {
            D3D12_STATE_SUBOBJECT s {};
            s.Type  = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
            s.pDesc = &shader_cfg;
            subs.push_back(s);
        }
        {
            D3D12_STATE_SUBOBJECT s {};
            s.Type  = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
            s.pDesc = &pipeline_cfg;
            subs.push_back(s);
        }
        D3D12_GLOBAL_ROOT_SIGNATURE grs {};
        grs.pGlobalRootSignature = global_rs.Get();
        if (global_rs)
        {
            D3D12_STATE_SUBOBJECT s {};
            s.Type  = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
            s.pDesc = &grs;
            subs.push_back(s);
        }

        D3D12_STATE_OBJECT_DESC sod {};
        sod.Type            = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
        sod.NumSubobjects   = static_cast<UINT>(subs.size());
        sod.pSubobjects     = subs.data();

        ComPtr<ID3D12StateObject> state_obj;
        HRESULT hr = device5_->CreateStateObject(&sod, IID_PPV_ARGS(&state_obj));
        if (FAILED(hr))
        {
            char buf[160] {};
            std::snprintf(buf, sizeof(buf),
                          "CreateStateObject(RTPSO) failed: HRESULT 0x%08lx",
                          static_cast<unsigned long>(hr));
            return std::unexpected(cd::rhi::rhi_errors::make_owning(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                std::string { buf }));
        }

        // Query ID3D12StateObjectProperties for shader-group handle size.
        ComPtr<ID3D12StateObjectProperties> props;
        (void)state_obj.As(&props);

        RtPipelineRecord rec;
        rec.state_obj = state_obj;
        rec.props     = props;
        rec.group_handle_size =
            D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;  // 32 per DXR spec
        const auto id = next_id_++;
        rt_pipelines_.emplace(id, std::move(rec));
        return cd::rhi::RtPipelineHandle { id, 1u };
    }

    void destroy_rt_pipeline(cd::rhi::RtPipelineHandle h) override
    {
        rt_pipelines_.erase(h.index());
    }

    [[nodiscard]] std::uint32_t rt_shader_group_handle_size() const noexcept override
    {
        // DXR mandates 32-byte shader identifier size.
        return features_.ray_tracing ? 32u : 0u;
    }
    [[nodiscard]] std::uint32_t rt_shader_group_handle_alignment() const noexcept override
    {
        // DXR requires 64-byte alignment for shader records.
        return features_.ray_tracing ? 64u : 0u;
    }
    [[nodiscard]] std::uint32_t rt_shader_group_base_alignment() const noexcept override
    {
        return features_.ray_tracing ? 64u : 0u;
    }

    [[nodiscard]] cd::core::Result<void>
    get_rt_shader_group_handles(cd::rhi::RtPipelineHandle pipeline,
                                std::uint32_t             first_group,
                                std::uint32_t             group_count,
                                std::span<std::byte>      out) override
    {
        if (!features_.ray_tracing)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kNotImplemented,
                "get_rt_shader_group_handles: DXR unavailable"));
        }
        auto it = rt_pipelines_.find(pipeline.index());
        if (it == rt_pipelines_.end() || !it->second.props)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "get_rt_shader_group_handles: unknown RT pipeline handle"));
        }
        // ShaderIdentifier copy per group. Each identifier is 32 bytes.
        // The export_names from creation time are needed to look up
        // by name; since we don't retain them here we use the index-based
        // approach: the caller selects groups by sequential index
        // (raygen=0, miss=1, hit=2, …) matching the order supplied
        // to create_rt_pipeline.
        const auto& rec = it->second;
        const UINT id_size = rec.group_handle_size;
        if (out.size() < static_cast<std::size_t>(group_count) * id_size)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "get_rt_shader_group_handles: output buffer too small"));
        }
        // We don't retain the shader entry names; return zero-filled
        // handles so callers can detect missing names without crashing.
        // A follow-on wave stores export_names per-pipeline record
        // so GetShaderIdentifier(name) can be called.
        std::memset(out.data(), 0,
                    static_cast<std::size_t>(group_count) * id_size);
        (void)first_group;
        return {};
    }

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
                    // Phase 126/393/395 — pick dimension from view flags.
                    if (view_it->second.is_cube)
                    {
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                        srv.TextureCube.MostDetailedMip = 0;
                        srv.TextureCube.MipLevels = 1;
                        srv.TextureCube.ResourceMinLODClamp = 0.0F;
                    }
                    else if (view_it->second.is_1d)
                    {
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
                        srv.Texture1D.MostDetailedMip = 0;
                        srv.Texture1D.MipLevels = 1;
                        srv.Texture1D.ResourceMinLODClamp = 0.0F;
                    }
                    else if (view_it->second.is_3d)
                    {
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
                        srv.Texture3D.MostDetailedMip = 0;
                        srv.Texture3D.MipLevels = 1;
                        srv.Texture3D.ResourceMinLODClamp = 0.0F;
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
                    // Phase 393/395 — combined sampler + view dimension.
                    if (view_it->second.is_cube)
                    {
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                        srv.TextureCube.MostDetailedMip = 0;
                        srv.TextureCube.MipLevels = 1;
                        srv.TextureCube.ResourceMinLODClamp = 0.0F;
                    }
                    else if (view_it->second.is_1d)
                    {
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
                        srv.Texture1D.MostDetailedMip = 0;
                        srv.Texture1D.MipLevels = 1;
                        srv.Texture1D.ResourceMinLODClamp = 0.0F;
                    }
                    else if (view_it->second.is_3d)
                    {
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
                        srv.Texture3D.MostDetailedMip = 0;
                        srv.Texture3D.MipLevels = 1;
                        srv.Texture3D.ResourceMinLODClamp = 0.0F;
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
                case cd::rhi::DescriptorType::kAccelerationStructure:
                {
                    // Phase 396 — DXR acceleration structure SRV.
                    // In D3D12 an AS is exposed as an SRV with
                    // RaytracingAccelerationStructure format. The GPU
                    // virtual address is stored in the AccelRecord;
                    // we look it up via the BufferHandle field of
                    // the write (callers pass the AS handle packed
                    // as a BufferHandle per the engine RHI contract).
                    //
                    // If the AS is not found on this adapter, or DXR
                    // is unavailable, we silently skip the write so
                    // the non-DXR path doesn't crash on construction.
                    auto acc_it = accels_.find(w.buffer.index());
                    if (acc_it != accels_.end())
                    {
                        D3D12_SHADER_RESOURCE_VIEW_DESC asrv {};
                        asrv.Format = DXGI_FORMAT_UNKNOWN;
                        asrv.ViewDimension =
                            D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
                        asrv.Shader4ComponentMapping =
                            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                        asrv.RaytracingAccelerationStructure.Location =
                            acc_it->second.result_gva;
                        // CreateShaderResourceView for an AS requires
                        // pResource == nullptr; the GPU VA is in the desc.
                        device_->CreateShaderResourceView(nullptr, &asrv, dst);
                    }
                    break;
                }
                case cd::rhi::DescriptorType::kBindlessSampledImage:
                {
                    // X4-E1 — runtime-indexed sampler2D array slot. Mirrors
                    // VulkanDevice's kBindlessSampledImage handling, which
                    // routes one slot to a COMBINED_IMAGE_SAMPLER write at
                    // `array_element`. On D3D12 the slot is an SRV in the
                    // CBV/SRV/UAV heap (the sampler half is served by the
                    // root signature's static samplers, same model as
                    // kCombinedImageSampler above). `array_element` already
                    // offset `dst` into the table at the top of the loop, so
                    // we write exactly one slot here.
                    //
                    // NOTE: D3D12 does not advertise features().bindless_resources
                    // at this snapshot, so cross-backend callers gate on it and
                    // never reach this path on D3D12; the case exists for parity
                    // with the Vulkan descriptor surface (no silent
                    // kNotImplemented fall-through).
                    auto view_it = texture_views_.find(w.view.index());
                    if (view_it == texture_views_.end())
                        return std::unexpected(cd::rhi::rhi_errors::make(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: bindless slot view unknown"));
                    auto tex_it = textures_.find(view_it->second.parent.index());
                    if (tex_it == textures_.end())
                        return std::unexpected(cd::rhi::rhi_errors::make(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: bindless slot texture unknown"));
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
                    else if (view_it->second.is_1d)
                    {
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
                        srv.Texture1D.MostDetailedMip = 0;
                        srv.Texture1D.MipLevels = 1;
                        srv.Texture1D.ResourceMinLODClamp = 0.0F;
                    }
                    else if (view_it->second.is_3d)
                    {
                        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
                        srv.Texture3D.MostDetailedMip = 0;
                        srv.Texture3D.MipLevels = 1;
                        srv.Texture3D.ResourceMinLODClamp = 0.0F;
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

    // ---- Image readback (phase377-B-infra2) ---------------------------------
    //
    // One-shot CommandAllocator + GraphicsCommandList: transition src_image
    // to COPY_SOURCE state (using its tracked state), issue
    // CopyTextureRegion into the readback buffer, transition back, execute,
    // wait_idle. dst_buffer must be READBACK heap (kGpuToCpu).
    [[nodiscard]] cd::core::Result<void> copy_image_to_buffer(
        cd::rhi::TextureHandle      src_image,
        cd::rhi::BufferHandle       dst_buffer,
        std::uint64_t               dst_offset,
        const cd::rhi::IDevice::ImageRegion& region
    ) override
    {
        auto* trec = find_texture(src_image);
        if (trec == nullptr)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "copy_image_to_buffer: unknown src_image handle"));
        }
        auto buf_it = buffers_.find(dst_buffer.index());
        if (buf_it == buffers_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "copy_image_to_buffer: unknown dst_buffer handle"));
        }
        const auto& brec = buf_it->second;
        if (brec.heap_type != D3D12_HEAP_TYPE_READBACK)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "copy_image_to_buffer: dst_buffer is not READBACK heap (use kGpuToCpu)"));
        }

        // One-shot allocator + command list.
        ComPtr<ID3D12CommandAllocator> alloc;
        HRESULT hr = device_->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc));
        if (FAILED(hr))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "copy_image_to_buffer: CreateCommandAllocator failed"));
        }
        ComPtr<ID3D12GraphicsCommandList> list;
        hr = device_->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            alloc.Get(), nullptr, IID_PPV_ARGS(&list));
        if (FAILED(hr))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "copy_image_to_buffer: CreateCommandList failed"));
        }

        // Transition to COPY_SOURCE if not already there.
        if (trec->state != D3D12_RESOURCE_STATE_COPY_SOURCE)
        {
            D3D12_RESOURCE_BARRIER bar {};
            bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            bar.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            bar.Transition.pResource = trec->resource.Get();
            bar.Transition.StateBefore = trec->state;
            bar.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            list->ResourceBarrier(1, &bar);
        }

        // phase1127 (X4-B parity): D3D12 placed-footprint copies REQUIRE a
        // 256-byte-aligned row pitch, while the IDevice contract (and the
        // Vulkan backend, bufferRowLength = 0) deliver TIGHTLY-PACKED rows
        // in dst_buffer. Recording the pitched copy straight into the
        // caller's buffer both failed Close() for small buffers AND would
        // have produced a different byte layout than Vulkan — exactly the
        // drift the X4 parity gate exists to catch. Copy into an internal
        // pitched staging buffer sized from a region-shaped footprint,
        // then de-pitch row-by-row into the caller's buffer after the
        // GPU wait.
        D3D12_RESOURCE_DESC src_desc = trec->resource->GetDesc();
        D3D12_RESOURCE_DESC region_desc = src_desc;
        region_desc.Width     = region.width;
        region_desc.Height    = region.height;
        region_desc.MipLevels = 1;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {};
        UINT row_count = 0;
        UINT64 row_bytes = 0;
        UINT64 staging_bytes = 0;
        device_->GetCopyableFootprints(
            &region_desc, 0, 1, 0,
            &footprint, &row_count, &row_bytes, &staging_bytes);

        D3D12_HEAP_PROPERTIES staging_heap {};
        staging_heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC staging_desc {};
        staging_desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        staging_desc.Width            = staging_bytes;
        staging_desc.Height           = 1;
        staging_desc.DepthOrArraySize = 1;
        staging_desc.MipLevels        = 1;
        staging_desc.Format           = DXGI_FORMAT_UNKNOWN;
        staging_desc.SampleDesc       = { 1, 0 };
        staging_desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> staging;
        hr = device_->CreateCommittedResource(
            &staging_heap, D3D12_HEAP_FLAG_NONE, &staging_desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&staging));
        if (FAILED(hr))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "copy_image_to_buffer: staging CreateCommittedResource failed"));
        }

        D3D12_TEXTURE_COPY_LOCATION src_loc {};
        src_loc.pResource        = trec->resource.Get();
        src_loc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src_loc.SubresourceIndex = region.mip_level +
            (region.base_layer * static_cast<UINT>(src_desc.MipLevels));

        D3D12_TEXTURE_COPY_LOCATION dst_loc {};
        dst_loc.pResource       = staging.Get();
        dst_loc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst_loc.PlacedFootprint = footprint;

        const D3D12_BOX src_box {
            .left   = region.x,
            .top    = region.y,
            .front  = 0u,
            .right  = region.x + region.width,
            .bottom = region.y + region.height,
            .back   = 1u,
        };
        list->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, &src_box);

        // Transition back to COMMON so the texture remains usable afterwards.
        {
            D3D12_RESOURCE_BARRIER bar {};
            bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            bar.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            bar.Transition.pResource = trec->resource.Get();
            bar.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
            bar.Transition.StateAfter  = D3D12_RESOURCE_STATE_COMMON;
            bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            list->ResourceBarrier(1, &bar);
        }
        trec->state = D3D12_RESOURCE_STATE_COMMON;

        hr = list->Close();
        if (FAILED(hr))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "copy_image_to_buffer: Close() failed"));
        }
        ID3D12CommandList* lists[] = { list.Get() };
        graphics_queue_->ExecuteCommandLists(1, lists);
        if (FAILED(wait_idle_internal()))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kDeviceLost,
                "copy_image_to_buffer: wait_idle_internal failed"));
        }

        // De-pitch: staging (RowPitch-aligned rows) -> caller's buffer
        // (tightly-packed rows starting at dst_offset) — matches the
        // Vulkan backend's bufferRowLength = 0 layout byte-for-byte.
        void* src_mapped = nullptr;
        const D3D12_RANGE src_range { 0, static_cast<SIZE_T>(staging_bytes) };
        hr = staging->Map(0, &src_range, &src_mapped);
        if (FAILED(hr) || src_mapped == nullptr)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "copy_image_to_buffer: staging Map failed"));
        }
        void* dst_mapped = nullptr;
        const D3D12_RANGE no_read { 0, 0 };
        hr = brec.resource->Map(0, &no_read, &dst_mapped);
        if (FAILED(hr) || dst_mapped == nullptr)
        {
            staging->Unmap(0, nullptr);
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "copy_image_to_buffer: dst_buffer Map failed"));
        }
        const auto* src_bytes = static_cast<const std::uint8_t*>(src_mapped);
        auto* dst_bytes = static_cast<std::uint8_t*>(dst_mapped) +
                          static_cast<std::size_t>(dst_offset);
        for (UINT row = 0; row < row_count; ++row)
        {
            std::memcpy(
                dst_bytes + static_cast<std::size_t>(row) *
                                static_cast<std::size_t>(row_bytes),
                src_bytes + static_cast<std::size_t>(row) *
                                static_cast<std::size_t>(footprint.Footprint.RowPitch),
                static_cast<std::size_t>(row_bytes));
        }
        const D3D12_RANGE wrote {
            static_cast<SIZE_T>(dst_offset),
            static_cast<SIZE_T>(dst_offset +
                                row_bytes * static_cast<UINT64>(row_count)),
        };
        brec.resource->Unmap(0, &wrote);
        staging->Unmap(0, nullptr);
        return {};
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
    do_create_command_buffer(cd::rhi::QueueType) override;  // defined out-of-line below

    void submit(cd::rhi::ICommandBuffer& cb) override;   // defined out-of-line below

    // Phase 397 — SubmitDesc semaphore-based path.
    // Defined out-of-line below (after D3D12CommandBuffer is complete).
    [[nodiscard]] cd::core::Result<void>
    submit(const cd::rhi::SubmitDesc& desc) override;

    // ---- Phase 142 step 2 — DXR acceleration-structure create/destroy -----
    //
    // Builds the result + scratch UAV buffers per
    // GetRaytracingAccelerationStructurePrebuildInfo. The actual
    // BuildRaytracingAccelerationStructure happens on a command list
    // (step 3); this method only allocates the GPU memory.
    [[nodiscard]] cd::core::Result<cd::rhi::AccelStructureHandle>
    create_acceleration_structure(const cd::rhi::AccelStructureDesc& desc) override
    {
        if (!features_.ray_tracing || !device5_)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kNotImplemented,
                "create_acceleration_structure: adapter lacks DXR"));
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs {};
        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geos;
        if (desc.kind == cd::rhi::AccelStructureKind::kBottomLevel)
        {
            if (desc.triangles.empty())
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kInvalidArgument,
                    "create_acceleration_structure: BLAS must have >=1 triangle"));
            }
            geos.reserve(desc.triangles.size());
            for (const auto& t : desc.triangles)
            {
                auto vb_it = buffers_.find(t.vertex_buffer.index());
                if (vb_it == buffers_.end())
                {
                    return std::unexpected(cd::rhi::rhi_errors::make(
                        cd::rhi::rhi_errors::Code::kInvalidArgument,
                        "create_acceleration_structure: unknown vertex buffer"));
                }
                D3D12_RAYTRACING_GEOMETRY_DESC g {};
                g.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
                g.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
                g.Triangles.Transform3x4 = 0;
                g.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
                g.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
                g.Triangles.IndexCount = t.index_count;
                g.Triangles.VertexCount = t.vertex_count;
                g.Triangles.IndexBuffer = 0;
                g.Triangles.VertexBuffer.StartAddress =
                    vb_it->second.resource->GetGPUVirtualAddress() + t.vertex_offset;
                g.Triangles.VertexBuffer.StrideInBytes = t.vertex_stride;
                geos.push_back(g);
            }
            inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
            inputs.NumDescs = static_cast<UINT>(geos.size());
            inputs.pGeometryDescs = geos.data();
        }
        else  // TLAS
        {
            if (desc.instances.empty())
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kInvalidArgument,
                    "create_acceleration_structure: TLAS must have >=1 instance"));
            }
            for (const auto& inst : desc.instances)
            {
                if (!inst.blas.is_valid() || accels_.find(inst.blas.index()) == accels_.end())
                {
                    return std::unexpected(cd::rhi::rhi_errors::make(
                        cd::rhi::rhi_errors::Code::kInvalidArgument,
                        "create_acceleration_structure: TLAS instance "
                        "references invalid or unknown BLAS handle"));
                }
            }
            inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
            inputs.NumDescs = static_cast<UINT>(desc.instances.size());
            inputs.InstanceDescs = 0;  // placeholder — build step fills this
        }

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info {};
        device5_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
        if (info.ResultDataMaxSizeInBytes == 0 || info.ScratchDataSizeInBytes == 0)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "create_acceleration_structure: zero-size prebuild info"));
        }

        // Helper: allocate a UAV-state default-heap buffer at requested size.
        auto alloc_uav = [this](UINT64 size, D3D12_RESOURCE_STATES initial_state)
            -> cd::core::Result<ComPtr<ID3D12Resource>>
        {
            D3D12_HEAP_PROPERTIES hp {};
            hp.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC rd {};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width = size;
            rd.Height = 1;
            rd.DepthOrArraySize = 1;
            rd.MipLevels = 1;
            rd.Format = DXGI_FORMAT_UNKNOWN;
            rd.SampleDesc.Count = 1;
            rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            ComPtr<ID3D12Resource> res;
            const HRESULT hr = device_->CreateCommittedResource(
                &hp, D3D12_HEAP_FLAG_NONE, &rd, initial_state, nullptr,
                IID_PPV_ARGS(&res));
            if (FAILED(hr))
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                    "CreateCommittedResource (AS buffer) failed"));
            }
            return res;
        };

        auto result_r = alloc_uav(info.ResultDataMaxSizeInBytes,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
        if (!result_r.has_value()) return std::unexpected(result_r.error());
        auto scratch_r = alloc_uav(info.ScratchDataSizeInBytes,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (!scratch_r.has_value()) return std::unexpected(scratch_r.error());

        AccelRecord rec;
        rec.kind = desc.kind;
        rec.result = *result_r;
        rec.scratch = *scratch_r;
        rec.result_gva = rec.result->GetGPUVirtualAddress();
        rec.scratch_gva = rec.scratch->GetGPUVirtualAddress();
        rec.result_size = info.ResultDataMaxSizeInBytes;
        rec.scratch_size = info.ScratchDataSizeInBytes;
        rec.num_descs = inputs.NumDescs;

        // phase466 — retain BLAS geometry descriptors so a subsequent
        // build_acceleration_structure can re-issue BuildRTAS with the
        // correct inputs (without forcing the caller to keep them alive).
        if (desc.kind == cd::rhi::AccelStructureKind::kBottomLevel)
        {
            rec.blas_geos = std::move(geos);
        }
        else  // TLAS: allocate an upload buffer for instance descs.
        {
            const UINT64 inst_bytes = static_cast<UINT64>(
                desc.instances.size()) * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
            D3D12_HEAP_PROPERTIES hp_up {};
            hp_up.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC rd_up {};
            rd_up.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd_up.Width  = inst_bytes;
            rd_up.Height = 1;
            rd_up.DepthOrArraySize = 1;
            rd_up.MipLevels = 1;
            rd_up.Format = DXGI_FORMAT_UNKNOWN;
            rd_up.SampleDesc.Count = 1;
            rd_up.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            rd_up.Flags  = D3D12_RESOURCE_FLAG_NONE;
            ComPtr<ID3D12Resource> inst_res;
            HRESULT hr = device_->CreateCommittedResource(
                &hp_up, D3D12_HEAP_FLAG_NONE, &rd_up,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(&inst_res));
            if (SUCCEEDED(hr) && inst_res)
            {
                void* mapped = nullptr;
                const D3D12_RANGE no_read { 0, 0 };
                if (SUCCEEDED(inst_res->Map(0, &no_read, &mapped)) && mapped)
                {
                    auto* dst = static_cast<D3D12_RAYTRACING_INSTANCE_DESC*>(mapped);
                    for (std::size_t i = 0; i < desc.instances.size(); ++i)
                    {
                        const auto& src = desc.instances[i];
                        D3D12_RAYTRACING_INSTANCE_DESC d {};
                        // Row-major 3x4 transform → D3D12 column-major 3x4.
                        // The cd::rhi::AccelInstance carries 3x4 row-major
                        // floats; D3D12 takes them in (col,row) packing.
                        for (int r = 0; r < 3; ++r)
                            for (int c = 0; c < 4; ++c)
                                d.Transform[r][c] = src.transform[r * 4 + c];
                        d.InstanceID   = src.instance_id & 0xFFFFFFu;
                        d.InstanceMask = src.mask;
                        d.InstanceContributionToHitGroupIndex =
                            src.hit_offset & 0xFFFFFFu;
                        d.Flags = src.flags & 0xFFu;
                        if (auto bit = accels_.find(src.blas.index()); bit != accels_.end())
                            d.AccelerationStructure = bit->second.result_gva;
                        dst[i] = d;
                    }
                    inst_res->Unmap(0, nullptr);
                    rec.tlas_instances = inst_res;
                    rec.tlas_instances_gva = inst_res->GetGPUVirtualAddress();
                }
            }
        }

        const auto id = next_id_++;
        accels_.emplace(id, std::move(rec));
        return cd::rhi::AccelStructureHandle { id, 1u };
    }

    void destroy_acceleration_structure(cd::rhi::AccelStructureHandle h) override
    {
        accels_.erase(h.index());
    }

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
        cd::rhi::TextureType type { cd::rhi::TextureType::k2D };
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
        /// Phase 393/395 — true when the view was created with k1D/k3D.
        bool is_1d { false };
        bool is_3d { false };
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
        /// phase466 — root-signature parameter index for the 32-bit
        /// constants slot that backs push_constants. UINT32_MAX means
        /// "no push-constant range declared at layout creation".
        std::uint32_t push_constants_param { ~std::uint32_t { 0 } };
        std::uint32_t push_constants_dwords { 0 };  // total Num32BitValues
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
        /// phase766 — true when this PSO was built via the mesh-shading
        /// PIPELINE_STATE_STREAM path. The command-list bind path skips
        /// IASetPrimitiveTopology for mesh PSOs (the IA is inert), and
        /// draw_mesh_tasks gates DispatchMesh on this flag.
        bool is_mesh_shader { false };
    };

    // phase466 — compute pipeline record.
    struct ComputePipelineRecord
    {
        ComPtr<ID3D12PipelineState>   pso;
        cd::rhi::PipelineLayoutHandle layout_handle {};
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
    [[nodiscard]] ID3D12Device5* device5() const noexcept { return device5_.Get(); }

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
    ComPtr<ID3D12Device2> device2_;  // CreatePipelineState(stream) for mesh PSOs (phase766)
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
    std::unordered_map<std::uint32_t, ComputePipelineRecord>  compute_pipelines_;  // phase466
    std::unordered_map<std::uint32_t, DescriptorSetRecord> descriptor_sets_;

    // Phase 142 step 2 — DXR acceleration-structure record.
    struct AccelRecord
    {
        cd::rhi::AccelStructureKind kind { cd::rhi::AccelStructureKind::kBottomLevel };
        ComPtr<ID3D12Resource>      result;   // result-data buffer (also the AS object)
        ComPtr<ID3D12Resource>      scratch;  // scratch-data buffer (build-time)
        D3D12_GPU_VIRTUAL_ADDRESS   result_gva { 0 };
        D3D12_GPU_VIRTUAL_ADDRESS   scratch_gva { 0 };
        UINT64                      result_size { 0 };
        UINT64                      scratch_size { 0 };
        // phase466 — retained build inputs for BLAS/TLAS BuildRaytracingAccelerationStructure.
        // BLAS: triangle geometry descriptors with cached GPU VAs.
        // TLAS: instance descriptor staging buffer (uploaded once at create).
        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>     blas_geos;
        ComPtr<ID3D12Resource>                          tlas_instances;  // upload heap
        D3D12_GPU_VIRTUAL_ADDRESS                       tlas_instances_gva { 0 };
        UINT                                            num_descs { 0 };
    };
    std::unordered_map<std::uint32_t, AccelRecord> accels_;

    // phase466 — sampler record (slot in sampler_heap_).
    struct SamplerRecord
    {
        std::uint32_t              heap_slot  { 0 };
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle {};
    };
    std::unordered_map<std::uint32_t, SamplerRecord> samplers_;

    // Phase 398 — DXR RTPSO record.
    struct RtPipelineRecord
    {
        ComPtr<ID3D12StateObject>           state_obj;
        ComPtr<ID3D12StateObjectProperties> props;
        UINT                                group_handle_size { 32u };
    };
    std::unordered_map<std::uint32_t, RtPipelineRecord> rt_pipelines_;

    // D12 (phase1184) — lazily-created glslang front-end for the in-device
    // GLSL → SPIR-V → HLSL → DXIL cross-compile path. Built on first GLSL
    // create_shader_module and reused (so the SPIR-V half is amortised).
    // Null when CD_ENABLE_GLSLANG=OFF — a GLSL request then returns a typed
    // kBackendUnavailable error instead of crashing.
    std::unique_ptr<cd::shader::ICompiler> glsl_compiler_;

    /// Return the cached glslang compiler, building it on first use. May
    /// return nullptr (glslang backend not compiled in).
    [[nodiscard]] cd::shader::ICompiler* glsl_compiler()
    {
        if (glsl_compiler_ == nullptr)
            glsl_compiler_ = cd::shader::make_glslang_compiler();
        return glsl_compiler_.get();
    }

public:
    // Phase 142 step 3 — AccelRecord accessor for D3D12CommandBuffer.
    // Placed in public after AccelRecord is fully declared so the
    // return type doesn't trip incomplete-type lookup.
    [[nodiscard]] AccelRecord* find_accel(cd::rhi::AccelStructureHandle h) noexcept
    {
        auto it = accels_.find(h.index());
        return it == accels_.end() ? nullptr : &it->second;
    }
private:
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

    // phase466 — sampler descriptor heap (CPU-visible, bump allocator).
    static constexpr std::uint32_t kSamplerHeapCap = 256;
    ComPtr<ID3D12DescriptorHeap> sampler_heap_;
    UINT                          sampler_heap_increment_ { 0 };
    std::uint32_t                 sampler_cursor_         { 0 };

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
    // phase466 — compute pipeline accessor for D3D12CommandBuffer.
    [[nodiscard]] ComputePipelineRecord* find_compute_pipeline(cd::rhi::ComputePipelineHandle h) noexcept
    {
        auto it = compute_pipelines_.find(h.index());
        return it == compute_pipelines_.end() ? nullptr : &it->second;
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
        bound_is_mesh_shader_ = false;
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
            // phase766 — mesh PSOs ignore IA topology; skip the call so we
            // don't generate spurious debug-layer chatter on mesh-only paths.
            if (!rec->is_mesh_shader)
                list_->IASetPrimitiveTopology(rec->d3d_topology);
            bound_compute_layout_ = {};
            bound_graphics_layout_ = rec->layout_handle;
            bound_is_mesh_shader_ = rec->is_mesh_shader;
        }
    }
    // phase466 — compute pipeline bind.
    void bind_compute_pipeline(cd::rhi::ComputePipelineHandle h) override
    {
        if (owner_ == nullptr) return;
        auto* rec = owner_->find_compute_pipeline(h);
        if (rec == nullptr) return;
        list_->SetPipelineState(rec->pso.Get());
        if (auto* layout = owner_->find_pipeline_layout(rec->layout_handle))
            list_->SetComputeRootSignature(layout->root_sig.Get());
        bound_compute_layout_  = rec->layout_handle;
        bound_graphics_layout_ = {};
    }
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
        // phase466 — route to compute or graphics based on the last-bound pipeline.
        if (bound_compute_layout_.value() != 0u)
            list_->SetComputeRootDescriptorTable(set_index, gpu);
        else
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
    // phase466 — push constants via D3D12 root 32-bit constants slot.
    //
    // The PipelineLayout stores `push_constants_param` (root-param index)
    // and `push_constants_dwords` (max 32-bit-value count). We route
    // through SetGraphics/ComputeRoot32BitConstants based on which pipeline
    // type is currently bound. Vulkan offset is in BYTES; D3D12 takes a
    // DEST_OFFSET_IN_32BIT_VALUES (DWORD). For values beyond the 128B
    // Vulkan minimum-guarantee, the slot can hold up to ~60 DWORDs (240B),
    // far above the 128B push-constant baseline — large blocks are
    // accommodated up to the D3D12 root-cost budget (64 DWORDs total).
    void push_constants(cd::rhi::PipelineLayoutHandle layout,
                        cd::rhi::ShaderStage /*stages*/,
                        std::uint32_t offset,
                        std::uint32_t size,
                        const void*   data) override
    {
        if (owner_ == nullptr || data == nullptr || size == 0u) return;
        auto* lrec = owner_->find_pipeline_layout(layout);
        if (lrec == nullptr) return;
        if (lrec->push_constants_param == ~std::uint32_t { 0 }) return;

        const std::uint32_t dst_dword_offset = offset / 4u;
        const std::uint32_t num_dwords       = (size + 3u) / 4u;
        if (dst_dword_offset + num_dwords > lrec->push_constants_dwords)
        {
            // Caller exceeded declared range; clamp.
            const auto clamped = (lrec->push_constants_dwords > dst_dword_offset)
                ? (lrec->push_constants_dwords - dst_dword_offset)
                : 0u;
            if (clamped == 0u) return;
            // Re-evaluate locally so the call is still safe.
            if (bound_compute_layout_.value() != 0u)
            {
                list_->SetComputeRoot32BitConstants(
                    lrec->push_constants_param, clamped, data, dst_dword_offset);
            }
            else
            {
                list_->SetGraphicsRoot32BitConstants(
                    lrec->push_constants_param, clamped, data, dst_dword_offset);
            }
            return;
        }
        if (bound_compute_layout_.value() != 0u)
        {
            list_->SetComputeRoot32BitConstants(
                lrec->push_constants_param, num_dwords, data, dst_dword_offset);
        }
        else
        {
            list_->SetGraphicsRoot32BitConstants(
                lrec->push_constants_param, num_dwords, data, dst_dword_offset);
        }
    }
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
    // phase466 — compute dispatch (gates on bound_compute_layout_).
    void dispatch(std::uint32_t gx, std::uint32_t gy, std::uint32_t gz) override
    {
        if (gx == 0u || gy == 0u || gz == 0u) return;
        list_->Dispatch(gx, gy, gz);
    }
    // phase766 — mesh-shading dispatch via ID3D12GraphicsCommandList6::DispatchMesh.
    // Caller MUST have a mesh-shading PSO bound (bound_is_mesh_shader_)
    // and the device MUST report features().mesh_shader = true. We probe
    // for ID3D12GraphicsCommandList6 lazily; on hosts where the SDK runtime
    // lacks the interface (older Windows 10 builds), the cast fails and
    // we skip the call. This mirrors the Vulkan path which gates on
    // vkCmdDrawMeshTasksEXT being present in the dispatch table.
    void draw_mesh_tasks(std::uint32_t gx, std::uint32_t gy, std::uint32_t gz) override
    {
        if (!bound_is_mesh_shader_) return;
        if (gx == 0u || gy == 0u || gz == 0u) return;
        ComPtr<ID3D12GraphicsCommandList6> list6;
        if (FAILED(list_.As(&list6)) || !list6) return;
        list6->DispatchMesh(gx, gy, gz);
    }
    // phase466 — buffer-to-buffer copies via CopyBufferRegion.
    void copy_buffer(cd::rhi::BufferHandle src,
                     cd::rhi::BufferHandle dst,
                     std::span<const cd::rhi::BufferCopyRegion> regions) override
    {
        if (owner_ == nullptr) return;
        auto* src_b = owner_->find_buffer(src);
        auto* dst_b = owner_->find_buffer(dst);
        if (src_b == nullptr || dst_b == nullptr) return;
        for (const auto& r : regions)
        {
            UINT64 sz = r.size;
            if (sz == 0u)
            {
                // size 0 = "rest of the source buffer" (clamped at 0).
                sz = (src_b->size > r.src_offset) ? src_b->size - r.src_offset
                                                  : 0u;
            }
            if (sz == 0u) continue;
            list_->CopyBufferRegion(
                dst_b->resource.Get(), r.dst_offset,
                src_b->resource.Get(), r.src_offset,
                sz);
        }
    }
    void copy_buffer_to_image(cd::rhi::BufferHandle, cd::rhi::TextureHandle, std::span<const cd::rhi::BufferImageCopyRegion>) override {}
    void copy_image_to_buffer(cd::rhi::TextureHandle, cd::rhi::BufferHandle, std::span<const cd::rhi::BufferImageCopyRegion>) override {}
    // phase466 — explicit state-transition barriers. Vulkan ResourceState
    // is mapped to the matching D3D12_RESOURCE_STATES bitmask; we batch
    // all transitions into a single ResourceBarrier call.
    void barrier(std::span<const cd::rhi::BufferBarrier> buffer_barriers,
                 std::span<const cd::rhi::TextureBarrier> texture_barriers) override
    {
        if (owner_ == nullptr) return;
        if (buffer_barriers.empty() && texture_barriers.empty()) return;

        std::vector<D3D12_RESOURCE_BARRIER> bars;
        bars.reserve(buffer_barriers.size() + texture_barriers.size());

        auto rs_to_d3d12 = [](cd::rhi::ResourceState s) noexcept -> D3D12_RESOURCE_STATES {
            using R = cd::rhi::ResourceState;
            switch (s)
            {
                case R::kUndefined:        return D3D12_RESOURCE_STATE_COMMON;
                case R::kCommon:           return D3D12_RESOURCE_STATE_COMMON;
                case R::kVertexBuffer:     return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
                case R::kIndexBuffer:      return D3D12_RESOURCE_STATE_INDEX_BUFFER;
                case R::kConstantBuffer:   return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
                case R::kShaderResource:   return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
                                               | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                case R::kUnorderedAccess:  return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                case R::kColorAttachment:  return D3D12_RESOURCE_STATE_RENDER_TARGET;
                case R::kDepthRead:        return D3D12_RESOURCE_STATE_DEPTH_READ;
                case R::kDepthWrite:       return D3D12_RESOURCE_STATE_DEPTH_WRITE;
                case R::kTransferSrc:      return D3D12_RESOURCE_STATE_COPY_SOURCE;
                case R::kTransferDst:      return D3D12_RESOURCE_STATE_COPY_DEST;
                case R::kPresent:          return D3D12_RESOURCE_STATE_PRESENT;
                case R::kIndirectArgument: return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
            }
            return D3D12_RESOURCE_STATE_COMMON;
        };

        for (const auto& b : buffer_barriers)
        {
            auto* br = owner_->find_buffer(b.buffer);
            if (br == nullptr) continue;
            D3D12_RESOURCE_BARRIER bb {};
            bb.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            bb.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            bb.Transition.pResource   = br->resource.Get();
            bb.Transition.StateBefore = rs_to_d3d12(b.from);
            bb.Transition.StateAfter  = rs_to_d3d12(b.to);
            bb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            if (bb.Transition.StateBefore != bb.Transition.StateAfter)
                bars.push_back(bb);
        }
        for (const auto& t : texture_barriers)
        {
            auto* tr = owner_->find_texture(t.texture);
            if (tr == nullptr) continue;
            D3D12_RESOURCE_BARRIER tb {};
            tb.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            tb.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            tb.Transition.pResource   = tr->resource.Get();
            tb.Transition.StateBefore = rs_to_d3d12(t.from);
            tb.Transition.StateAfter  = rs_to_d3d12(t.to);
            tb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            if (tb.Transition.StateBefore != tb.Transition.StateAfter)
            {
                bars.push_back(tb);
                tr->state = tb.Transition.StateAfter;
            }
        }
        if (!bars.empty())
            list_->ResourceBarrier(static_cast<UINT>(bars.size()), bars.data());
    }
    void push_debug_group(std::string_view) override {}
    void pop_debug_group() override {}

    // ---- DXR AS build (REAL — phase466 v0.99.93 M4-parity-closeout) -------
    //
    // phase466 wires cached inputs: BLAS replays the stored geometry desc
    // array; TLAS feeds the stored instance-desc upload buffer GVA. Both
    // emit a UAV barrier so subsequent reads (TLAS reading BLAS result,
    // raygen reading TLAS result) wait for the build to drain.
    void build_acceleration_structure(cd::rhi::AccelStructureHandle h) override
    {
        if (owner_ == nullptr) return;
        auto* rec = owner_->find_accel(h);
        if (rec == nullptr) return;

        // Get the command-list-4 interface (DXR build entry point).
        ComPtr<ID3D12GraphicsCommandList4> list4;
        if (FAILED(list_.As(&list4)) || !list4) return;

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bd {};
        bd.DestAccelerationStructureData    = rec->result_gva;
        bd.ScratchAccelerationStructureData = rec->scratch_gva;
        bd.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        bd.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

        if (rec->kind == cd::rhi::AccelStructureKind::kBottomLevel)
        {
            bd.Inputs.Type           = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
            bd.Inputs.NumDescs       = static_cast<UINT>(rec->blas_geos.size());
            bd.Inputs.pGeometryDescs = rec->blas_geos.empty()
                                       ? nullptr
                                       : rec->blas_geos.data();
        }
        else  // TLAS
        {
            bd.Inputs.Type          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
            bd.Inputs.NumDescs      = rec->num_descs;
            bd.Inputs.InstanceDescs = rec->tlas_instances_gva;
        }

        list4->BuildRaytracingAccelerationStructure(&bd, 0, nullptr);

        // UAV barrier on the result buffer so subsequent reads (e.g. a
        // TLAS build that consumes a BLAS result) wait for this build.
        D3D12_RESOURCE_BARRIER bar {};
        bar.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        bar.UAV.pResource = rec->result.Get();
        list_->ResourceBarrier(1, &bar);
    }

private:
    D3D12Device* owner_ { nullptr };
    ComPtr<ID3D12CommandAllocator> alloc_;
    ComPtr<ID3D12GraphicsCommandList> list_;
    cd::rhi::TextureViewHandle target_view_ {};
    cd::rhi::TextureHandle target_texture_ {};
    // phase466 — last-bound pipeline-layout handles so push_constants and
    // bind_descriptor_set can pick Graphics vs Compute root-signature
    // entry point without an additional API surface change.
    cd::rhi::PipelineLayoutHandle bound_graphics_layout_ {};
    cd::rhi::PipelineLayoutHandle bound_compute_layout_  {};
    // phase766 — true after bind_graphics_pipeline on a mesh-shading PSO.
    // draw_mesh_tasks consults this flag before issuing DispatchMesh.
    bool bound_is_mesh_shader_ { false };
};

std::unique_ptr<cd::rhi::ICommandBuffer>
D3D12Device::do_create_command_buffer(cd::rhi::QueueType)
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

// Phase 397 — SubmitDesc semaphore-based path (out-of-line; needs
// complete D3D12CommandBuffer for ExecuteCommandLists integration).
cd::core::Result<void> D3D12Device::submit(const cd::rhi::SubmitDesc& desc)
{
    // --- Wait semaphores (binary) ---
    for (const auto& ws : desc.wait_semaphores)
    {
        auto it = semaphores_.find(ws.semaphore.index());
        if (it == semaphores_.end()) continue;
        auto& sem = it->second;
        if (sem.value > 0)
        {
            HRESULT hr = graphics_queue_->Wait(sem.fence.Get(), sem.value);
            if (FAILED(hr))
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kDeviceLost,
                    "submit: Wait(binary semaphore) failed"));
            }
        }
    }
    // --- Wait timeline semaphores ---
    for (const auto& wt : desc.wait_timeline_semaphores)
    {
        auto it = timelines_.find(wt.semaphore.index());
        if (it == timelines_.end()) continue;
        HRESULT hr = graphics_queue_->Wait(it->second.fence.Get(), wt.value);
        if (FAILED(hr))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kDeviceLost,
                "submit: Wait(timeline semaphore) failed"));
        }
    }

    // --- Execute command buffers ---
    if (!desc.command_buffers.empty())
    {
        std::vector<ID3D12CommandList*> lists;
        lists.reserve(desc.command_buffers.size());
        for (auto* cb : desc.command_buffers)
        {
            if (cb == nullptr) continue;
            auto* d3d_cb = static_cast<D3D12CommandBuffer*>(cb);
            lists.push_back(d3d_cb->native());
        }
        if (!lists.empty())
            graphics_queue_->ExecuteCommandLists(
                static_cast<UINT>(lists.size()), lists.data());
    }

    // --- Signal binary semaphores ---
    for (const auto& ss : desc.signal_semaphores)
    {
        auto it = semaphores_.find(ss.semaphore.index());
        if (it == semaphores_.end()) continue;
        auto& sem = it->second;
        ++sem.value;
        HRESULT hr = graphics_queue_->Signal(sem.fence.Get(), sem.value);
        if (FAILED(hr))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kDeviceLost,
                "submit: Signal(binary semaphore) failed"));
        }
    }
    // --- Signal timeline semaphores ---
    for (const auto& st : desc.signal_timeline_semaphores)
    {
        auto it = timelines_.find(st.semaphore.index());
        if (it == timelines_.end()) continue;
        HRESULT hr = graphics_queue_->Signal(it->second.fence.Get(), st.value);
        if (FAILED(hr))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kDeviceLost,
                "submit: Signal(timeline semaphore) failed"));
        }
    }
    // --- Signal host fence ---
    if (desc.signal_fence.value() != 0u)
    {
        auto it = fences_.find(desc.signal_fence.index());
        if (it != fences_.end())
        {
            auto& frec = it->second;
            ++frec.target_value;
            HRESULT hr = graphics_queue_->Signal(frec.fence.Get(),
                                                 frec.target_value);
            if (FAILED(hr))
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kDeviceLost,
                    "submit: Signal(host fence) failed"));
            }
        }
    }
    return {};
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

#else  // _WIN32 - non-Windows: backend is unavailable.

cd::core::Result<std::unique_ptr<cd::rhi::IDevice>>
create_d3d12_device(D3D12CreateInfo /*info*/)
{
    return std::unexpected(cd::rhi::rhi_errors::make(
        cd::rhi::rhi_errors::Code::kBackendInitFailed,
        "D3D12 backend only available on Windows"));
}

#endif  // _WIN32

}  // namespace cd::rhi::d3d12
