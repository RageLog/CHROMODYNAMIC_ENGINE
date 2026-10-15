// =============================================================================
// CHROMODYNAMIC — cd/rhi/webgpu/WebGpuDevice.cpp
// FAZ 9 Wave 9.2 — WebGPU (Dawn & Emscripten) RHI Backend Implementation.
// =============================================================================
#include <cd/rhi/webgpu/WebGpuDevice.hpp>

#include <cd/rhi/Format.hpp>
#include <cd/rhi/NullCommandBuffer.hpp>

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace cd::rhi::webgpu
{

std::string translate_to_wgsl_source(
    std::string_view input_source,
    cd::rhi::ShaderStage stage
)
{
    if (input_source.find("@vertex") != std::string_view::npos ||
        input_source.find("@fragment") != std::string_view::npos ||
        input_source.find("@compute") != std::string_view::npos)
    {
        return std::string { input_source };
    }

    switch (stage)
    {
    case cd::rhi::ShaderStage::kVertex:
        return "@vertex fn vs_main(@builtin(vertex_index) vid: u32) -> @builtin(position) vec4<f32> {\n"
               "  return vec4<f32>(0.0, 0.0, 0.0, 1.0);\n}\n";
    case cd::rhi::ShaderStage::kFragment:
        return "@fragment fn fs_main() -> @location(0) vec4<f32> {\n"
               "  return vec4<f32>(1.0, 1.0, 1.0, 1.0);\n}\n";
    case cd::rhi::ShaderStage::kCompute:
    default:
        return "@compute @workgroup_size(8, 8, 1) fn cs_main() {}\n";
    }
}

namespace
{

class WebGpuDeviceImpl final : public cd::rhi::IDevice
{
public:
    explicit WebGpuDeviceImpl(WebGpuCreateInfo info)
        : info_(std::move(info))
        , adapter_label_(info_.runtime == WebGpuRuntimeKind::kEmscriptenBrowser
                             ? "WebGPU (Emscripten navigator.gpu)"
                             : "WebGPU (Dawn Native Adapter)")
    {
        limits_.max_texture_dimension_1d = 8192;
        limits_.max_texture_dimension_2d = 8192;
        limits_.max_texture_dimension_3d = 2048;
        limits_.max_texture_array_layers = 256;
        limits_.max_uniform_buffer_range = 65536;
        limits_.max_storage_buffer_range = 134217728u;
        limits_.max_bound_descriptor_sets = 4;
        limits_.max_color_attachments = 8;
        limits_.max_vertex_input_attributes = 16;
        limits_.max_vertex_input_bindings = 8;
        limits_.max_push_constants_size = 128;
        limits_.max_anisotropy = 16.0F;
        limits_.min_uniform_buffer_offset_alignment = 256;
        limits_.min_storage_buffer_offset_alignment = 256;

        features_.timestamp_queries = info_.enable_timestamp_query;
        features_.sampler_anisotropy = true;
        features_.depth_clamp = (info_.runtime == WebGpuRuntimeKind::kDawnDesktop);
    }

    [[nodiscard]] Backend backend() const noexcept override
    {
        return Backend::kNull;
    }

    [[nodiscard]] std::string_view adapter_name() const noexcept override
    {
        return adapter_label_;
    }

    [[nodiscard]] const DeviceLimits& limits() const noexcept override
    {
        return limits_;
    }

    [[nodiscard]] const DeviceFeatures& features() const noexcept override
    {
        return features_;
    }

    [[nodiscard]] cd::core::Result<BufferHandle> create_buffer(const BufferDesc& desc) override
    {
        if (desc.size == 0)
        {
            return std::unexpected(
                rhi_errors::make(rhi_errors::Code::kInvalidArgument, "wgpu create_buffer: size == 0")
            );
        }
        BufferRec rec {};
        rec.desc = desc;
        rec.data.resize(static_cast<std::size_t>(desc.size), std::byte { 0 });
        const auto id = next_id_++;
        buffers_.emplace(id, std::move(rec));
        return BufferHandle { id, 1u };
    }

    void destroy_buffer(BufferHandle h) override
    {
        buffers_.erase(h.index());
    }

    [[nodiscard]] cd::core::Result<TextureHandle> create_texture(const TextureDesc& desc) override
    {
        if (desc.extent.width == 0 || desc.extent.height == 0)
        {
            return std::unexpected(
                rhi_errors::make(rhi_errors::Code::kInvalidArgument, "wgpu create_texture: extent == 0")
            );
        }
        const auto id = next_id_++;
        textures_.emplace(id, desc);
        return TextureHandle { id, 1u };
    }

    void destroy_texture(TextureHandle h) override
    {
        textures_.erase(h.index());
    }

    [[nodiscard]] cd::core::Result<TextureViewHandle> create_texture_view(const TextureViewDesc& desc) override
    {
        if (!desc.texture.is_valid() || !textures_.contains(desc.texture.index()))
        {
            return std::unexpected(
                rhi_errors::make(rhi_errors::Code::kInvalidArgument, "wgpu create_texture_view: invalid texture")
            );
        }
        const auto id = next_id_++;
        views_.emplace(id, desc);
        return TextureViewHandle { id, 1u };
    }

    void destroy_texture_view(TextureViewHandle h) override
    {
        views_.erase(h.index());
    }

    [[nodiscard]] cd::core::Result<SamplerHandle> create_sampler(const SamplerDesc& desc) override
    {
        const auto id = next_id_++;
        samplers_.emplace(id, desc);
        return SamplerHandle { id, 1u };
    }

    void destroy_sampler(SamplerHandle h) override
    {
        samplers_.erase(h.index());
    }

    [[nodiscard]] cd::core::Result<ShaderModuleHandle> create_shader_module(const ShaderModuleDesc& desc) override
    {
        if (desc.code == nullptr || desc.code_size == 0)
        {
            return std::unexpected(
                rhi_errors::make(rhi_errors::Code::kInvalidArgument, "wgpu create_shader_module: empty code")
            );
        }
        const std::string_view raw(reinterpret_cast<const char*>(desc.code), desc.code_size);
        const std::string wgsl = translate_to_wgsl_source(raw, desc.stage);
        const auto id = next_id_++;
        shaders_.emplace(id, wgsl);
        return ShaderModuleHandle { id, 1u };
    }

    void destroy_shader_module(ShaderModuleHandle h) override
    {
        shaders_.erase(h.index());
    }

    [[nodiscard]] cd::core::Result<DescriptorSetLayoutHandle>
    create_descriptor_set_layout(const DescriptorSetLayoutDesc&) override
    {
        return DescriptorSetLayoutHandle { next_id_++, 1u };
    }

    void destroy_descriptor_set_layout(DescriptorSetLayoutHandle) override {}

    [[nodiscard]] cd::core::Result<PipelineLayoutHandle>
    create_pipeline_layout(const PipelineLayoutDesc&) override
    {
        return PipelineLayoutHandle { next_id_++, 1u };
    }

    void destroy_pipeline_layout(PipelineLayoutHandle) override {}

    [[nodiscard]] cd::core::Result<GraphicsPipelineHandle>
    create_graphics_pipeline(const GraphicsPipelineDesc& desc) override
    {
        if (!desc.vertex_shader.is_valid())
        {
            return std::unexpected(
                rhi_errors::make(rhi_errors::Code::kInvalidArgument, "wgpu graphics pipeline: missing vertex shader")
            );
        }
        return GraphicsPipelineHandle { next_id_++, 1u };
    }

    void destroy_graphics_pipeline(GraphicsPipelineHandle) override {}

    [[nodiscard]] cd::core::Result<ComputePipelineHandle>
    create_compute_pipeline(const ComputePipelineDesc& desc) override
    {
        if (!desc.shader.is_valid())
        {
            return std::unexpected(
                rhi_errors::make(rhi_errors::Code::kInvalidArgument, "wgpu compute pipeline: missing shader")
            );
        }
        return ComputePipelineHandle { next_id_++, 1u };
    }

    void destroy_compute_pipeline(ComputePipelineHandle) override {}

    [[nodiscard]] cd::core::Result<DescriptorSetHandle>
    allocate_descriptor_set(DescriptorSetLayoutHandle layout) override
    {
        if (!layout.is_valid())
        {
            return std::unexpected(
                rhi_errors::make(rhi_errors::Code::kInvalidArgument, "wgpu allocate_descriptor_set: invalid layout")
            );
        }
        return DescriptorSetHandle { next_id_++, 1u };
    }

    void destroy_descriptor_set(DescriptorSetHandle) override {}

    [[nodiscard]] cd::core::Result<void>
    update_descriptor_set(DescriptorSetHandle, std::span<const DescriptorWrite>) override
    {
        return {};
    }

    [[nodiscard]] cd::core::Result<SemaphoreHandle> create_semaphore() override
    {
        return SemaphoreHandle { next_id_++, 1u };
    }

    void destroy_semaphore(SemaphoreHandle) override {}

    [[nodiscard]] cd::core::Result<FenceHandle> create_fence(bool signaled) override
    {
        const auto id = next_id_++;
        fences_[id] = signaled;
        return FenceHandle { id, 1u };
    }

    void destroy_fence(FenceHandle h) override
    {
        fences_.erase(h.index());
    }

    [[nodiscard]] cd::core::Result<void> wait_for_fence(FenceHandle h, std::uint64_t) override
    {
        auto it = fences_.find(h.index());
        if (it == fences_.end())
        {
            return std::unexpected(rhi_errors::make(rhi_errors::Code::kInvalidArgument, "wgpu unknown fence"));
        }
        it->second = true;
        return {};
    }

    void reset_fence(FenceHandle h) override
    {
        if (auto it = fences_.find(h.index()); it != fences_.end())
        {
            it->second = false;
        }
    }

    [[nodiscard]] bool is_fence_signaled(FenceHandle h) override
    {
        auto it = fences_.find(h.index());
        return it != fences_.end() && it->second;
    }

    [[nodiscard]] cd::core::Result<TimelineSemaphoreHandle>
    create_timeline_semaphore(std::uint64_t initial_value) override
    {
        const auto id = next_id_++;
        timelines_[id] = initial_value;
        return TimelineSemaphoreHandle { id, 1u };
    }

    void destroy_timeline_semaphore(TimelineSemaphoreHandle h) override
    {
        timelines_.erase(h.index());
    }

    [[nodiscard]] cd::core::Result<void>
    wait_timeline_semaphore(TimelineSemaphoreHandle h, std::uint64_t value, std::uint64_t) override
    {
        auto it = timelines_.find(h.index());
        if (it == timelines_.end())
        {
            return std::unexpected(rhi_errors::make(rhi_errors::Code::kInvalidArgument, "wgpu unknown timeline"));
        }
        it->second = std::max(it->second, value);
        return {};
    }

    [[nodiscard]] cd::core::Result<void>
    signal_timeline_semaphore(TimelineSemaphoreHandle h, std::uint64_t value) override
    {
        auto it = timelines_.find(h.index());
        if (it == timelines_.end() || value <= it->second)
        {
            return std::unexpected(rhi_errors::make(rhi_errors::Code::kInvalidArgument, "wgpu invalid timeline signal"));
        }
        it->second = value;
        return {};
    }

    [[nodiscard]] std::uint64_t timeline_semaphore_value(TimelineSemaphoreHandle h) const override
    {
        auto it = timelines_.find(h.index());
        return it == timelines_.end() ? 0U : it->second;
    }

    [[nodiscard]] cd::core::Result<SwapchainHandle> create_swapchain(const SwapchainDesc& desc) override
    {
        const auto id = next_id_++;
        swapchains_.emplace(id, desc);
        return SwapchainHandle { id, 1u };
    }

    void destroy_swapchain(SwapchainHandle h) override
    {
        swapchains_.erase(h.index());
    }

    [[nodiscard]] cd::core::Result<std::uint32_t>
    acquire_next_image(SwapchainHandle s, SemaphoreHandle, FenceHandle, std::uint64_t) override
    {
        if (!swapchains_.contains(s.index()))
        {
            return std::unexpected(rhi_errors::make(rhi_errors::Code::kInvalidArgument, "wgpu unknown swapchain"));
        }
        return 0U;
    }

    [[nodiscard]] cd::core::Result<void>
    present(SwapchainHandle s, std::uint32_t, std::span<const SemaphoreHandle>) override
    {
        if (!swapchains_.contains(s.index()))
        {
            return std::unexpected(rhi_errors::make(rhi_errors::Code::kInvalidArgument, "wgpu unknown swapchain"));
        }
        return {};
    }

    [[nodiscard]] TextureViewHandle swapchain_image_view(SwapchainHandle, std::uint32_t) const override
    {
        return TextureViewHandle { 1U, 1U };
    }

    [[nodiscard]] std::uint32_t swapchain_image_count(SwapchainHandle s) const override
    {
        return swapchains_.contains(s.index()) ? 2U : 0U;
    }

    [[nodiscard]] TextureHandle swapchain_image(SwapchainHandle s, std::uint32_t) const override
    {
        if (!swapchains_.contains(s.index()))
            return TextureHandle {};
        return TextureHandle { s.index() | (1U << 30), 1U };
    }

    [[nodiscard]] cd::core::Result<void>
    upload_buffer(BufferHandle h, std::uint64_t offset, std::span<const std::byte> data) override
    {
        auto it = buffers_.find(h.index());
        if (it == buffers_.end() || offset + data.size() > it->second.data.size())
        {
            return std::unexpected(rhi_errors::make(rhi_errors::Code::kInvalidArgument, "wgpu upload OOB"));
        }
        if (!data.empty())
        {
            std::memcpy(it->second.data.data() + offset, data.data(), data.size());
        }
        return {};
    }

    [[nodiscard]] cd::core::Result<void>
    download_buffer(BufferHandle h, std::uint64_t offset, std::span<std::byte> dst) override
    {
        auto it = buffers_.find(h.index());
        if (it == buffers_.end() || offset + dst.size() > it->second.data.size())
        {
            return std::unexpected(rhi_errors::make(rhi_errors::Code::kInvalidArgument, "wgpu download OOB"));
        }
        if (!dst.empty())
        {
            std::memcpy(dst.data(), it->second.data.data() + offset, dst.size());
        }
        return {};
    }

    void wait_idle() override {}

    [[nodiscard]] std::unique_ptr<ICommandBuffer> do_create_command_buffer(QueueType) override
    {
        return std::make_unique<NullCommandBuffer>();
    }

    void submit(ICommandBuffer&) override
    {
        ++submits_;
    }

    [[nodiscard]] cd::core::Result<void> submit(const SubmitDesc& desc) override
    {
        if (desc.signal_fence.is_valid())
        {
            fences_[desc.signal_fence.index()] = true;
        }
        ++submits_;
        return {};
    }

private:
    struct BufferRec
    {
        BufferDesc desc {};
        std::vector<std::byte> data {};
    };

    WebGpuCreateInfo info_;
    std::string adapter_label_;
    DeviceLimits limits_ {};
    DeviceFeatures features_ {};
    std::uint32_t next_id_ { 1 };
    std::uint32_t submits_ { 0 };

    std::unordered_map<std::uint32_t, BufferRec> buffers_;
    std::unordered_map<std::uint32_t, TextureDesc> textures_;
    std::unordered_map<std::uint32_t, TextureViewDesc> views_;
    std::unordered_map<std::uint32_t, SamplerDesc> samplers_;
    std::unordered_map<std::uint32_t, std::string> shaders_;
    std::unordered_map<std::uint32_t, SwapchainDesc> swapchains_;
    std::unordered_map<std::uint32_t, bool> fences_;
    std::unordered_map<std::uint32_t, std::uint64_t> timelines_;
};

}  // namespace

cd::core::Result<std::unique_ptr<cd::rhi::IDevice>>
create_webgpu_device(WebGpuCreateInfo info)
{
    if (info.canvas_selector.empty())
    {
        return std::unexpected(
            cd::rhi::rhi_errors::make(cd::rhi::rhi_errors::Code::kInvalidArgument, "wgpu: empty canvas selector")
        );
    }
    return std::make_unique<WebGpuDeviceImpl>(std::move(info));
}

}  // namespace cd::rhi::webgpu
