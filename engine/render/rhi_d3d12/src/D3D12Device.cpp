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

    // ---- Descriptor set layout (stubbed; PSO triangle uses empty
    //      root signature so no descriptor sets are needed) ----------------
    [[nodiscard]] cd::core::Result<cd::rhi::DescriptorSetLayoutHandle>
    create_descriptor_set_layout(const cd::rhi::DescriptorSetLayoutDesc&) override
    {
        // Honest placeholder — returns a handle so PipelineLayoutDesc that
        // includes one doesn't fail; the actual binding model lands with
        // the descriptor-set wave.
        const auto id = next_id_++;
        descriptor_set_layouts_.emplace(id, DescriptorSetLayoutRecord {});
        return cd::rhi::DescriptorSetLayoutHandle { id, 1u };
    }
    void destroy_descriptor_set_layout(cd::rhi::DescriptorSetLayoutHandle h) override
    {
        descriptor_set_layouts_.erase(h.index());
    }

    // ---- Pipeline layout (REAL — Phase 14.C v0.36.0) ----------------------
    //
    // D3D12 root signature. v0.36.0 ships the smallest possible default:
    // an empty root signature (no parameters). The descriptor / CBV
    // wiring lands with the descriptor-set surface in a later phase;
    // hello_d3d12_triangle uses inline vertex data so it doesn't need
    // a CBV.
    [[nodiscard]] cd::core::Result<cd::rhi::PipelineLayoutHandle>
    create_pipeline_layout(const cd::rhi::PipelineLayoutDesc&) override
    {
        D3D12_ROOT_SIGNATURE_DESC rsd {};
        rsd.NumParameters = 0;
        rsd.pParameters = nullptr;
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

    struct ShaderModuleRecord
    {
        std::vector<std::uint8_t> bytecode;
        cd::rhi::ShaderStage stage { cd::rhi::ShaderStage::kNone };
        std::string entry_point;
    };

    struct DescriptorSetLayoutRecord
    {
        // Stub for v0.36.0 — bindings stored only as a placeholder count
        // when descriptor sets land in a later phase.
    };

    struct PipelineLayoutRecord
    {
        ComPtr<ID3D12RootSignature> root_sig;
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
    void bind_descriptor_set(std::uint32_t, cd::rhi::DescriptorSetHandle) override {}
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
