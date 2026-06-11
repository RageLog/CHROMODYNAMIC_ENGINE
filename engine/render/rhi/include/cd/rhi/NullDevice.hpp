// =============================================================================
// CHROMODYNAMIC — cd/rhi/NullDevice.hpp
// ADR-001 (Sprint S3.0) — headless reference RHI implementation.
//
// NullDevice satisfies the IDevice contract without touching a GPU. It is
// used for unit tests, headless tooling, and as a deterministic baseline
// that proves the abstract surface is implementable.
//
// All create_* calls succeed and return monotonically-allocated handles;
// destroy_* invalidates them. upload_buffer requires a kCpuToGpu memory
// usage; otherwise returns kInvalidArgument.
// =============================================================================
#pragma once

#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/NullCommandBuffer.hpp>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cd::rhi
{

class NullDevice final : public IDevice
{
public:
    NullDevice() noexcept
    {
        limits_.max_texture_dimension_1d = 16384;
        limits_.max_texture_dimension_2d = 16384;
        limits_.max_texture_dimension_3d = 2048;
        limits_.max_texture_array_layers = 2048;
        limits_.max_uniform_buffer_range = 65536;
        limits_.max_storage_buffer_range = 0xFFFFFFFFu;
        limits_.max_bound_descriptor_sets = 8;
        limits_.max_color_attachments = 8;
        limits_.max_vertex_input_attributes = 32;
        limits_.max_vertex_input_bindings = 16;
        limits_.max_push_constants_size = 256;
        limits_.max_anisotropy = 16.0f;
        limits_.min_uniform_buffer_offset_alignment = 64;
        limits_.min_storage_buffer_offset_alignment = 64;
    }

    [[nodiscard]] Backend backend() const noexcept override
    {
        return Backend::kNull;
    }

    [[nodiscard]] std::string_view adapter_name() const noexcept override
    {
        return "cd::rhi::NullDevice";
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
            return std::unexpected(rhi_errors::make(rhi_errors::Code::kInvalidArgument, "buffer size == 0"));
        }
        BufferRecord r;
        r.desc = desc;
        if (desc.memory == MemoryUsage::kCpuToGpu || desc.memory == MemoryUsage::kGpuToCpu ||
            desc.memory == MemoryUsage::kCpuRandomAccess)
        {
            r.cpu_storage.resize(static_cast<std::size_t>(desc.size));
        }
        const auto id = next_id_++;
        buffers_.emplace(id, std::move(r));
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
            return std::unexpected(rhi_errors::make(rhi_errors::Code::kInvalidArgument, "texture extent == 0"));
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
        if (!desc.texture.is_valid())
        {
            return std::unexpected(rhi_errors::make(rhi_errors::Code::kInvalidArgument, "view: invalid texture"));
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
            return std::unexpected(rhi_errors::make(rhi_errors::Code::kInvalidArgument, "shader module: empty code"));
        }
        const auto id = next_id_++;
        shaders_.emplace(id, desc);
        return ShaderModuleHandle { id, 1u };
    }

    void destroy_shader_module(ShaderModuleHandle h) override
    {
        shaders_.erase(h.index());
    }

    [[nodiscard]] cd::core::Result<void>
    upload_buffer(BufferHandle h, std::uint64_t offset, std::span<const std::byte> data) override
    {
        auto it = buffers_.find(h.index());
        if (it == buffers_.end())
        {
            return std::unexpected(rhi_errors::make(rhi_errors::Code::kInvalidArgument, "upload: unknown buffer"));
        }
        auto& rec = it->second;
        if (rec.cpu_storage.empty())
        {
            return std::unexpected(rhi_errors::make(rhi_errors::Code::kInvalidArgument, "upload: buffer is GPU-only"));
        }
        if (offset + data.size() > rec.cpu_storage.size())
        {
            return std::unexpected(rhi_errors::make(rhi_errors::Code::kInvalidArgument, "upload: out of bounds"));
        }
        if (!data.empty())
        {
            std::memcpy(rec.cpu_storage.data() + offset, data.data(), data.size());
        }
        return {};
    }

    [[nodiscard]] cd::core::Result<void>
    download_buffer(BufferHandle h, std::uint64_t offset, std::span<std::byte> dst) override
    {
        auto it = buffers_.find(h.index());
        if (it == buffers_.end())
            return std::unexpected(rhi_errors::make(rhi_errors::Code::kInvalidArgument,
                                                   "download: unknown buffer"));
        const auto& rec = it->second;
        if (rec.cpu_storage.empty())
            return std::unexpected(rhi_errors::make(rhi_errors::Code::kInvalidArgument,
                                                   "download: buffer is GPU-only"));
        if (offset + dst.size() > rec.cpu_storage.size())
            return std::unexpected(rhi_errors::make(rhi_errors::Code::kInvalidArgument,
                                                   "download: out of bounds"));
        if (!dst.empty())
            std::memcpy(dst.data(), rec.cpu_storage.data() + offset, dst.size());
        return {};
    }

    /// Read-back hook (test-only). Returns a span over the buffer's CPU storage,
    /// or empty if the buffer is GPU-only / unknown.
    // ---- Image readback (phase377-B-infra2) --------------------------------
    /// Null backend: writes `region.width * region.height * bytes_per_texel`
    /// zeros into `dst_buffer` at `dst_offset`. Always succeeds as long as
    /// the handles exist and the destination range fits in the buffer.
    [[nodiscard]] cd::core::Result<void> copy_image_to_buffer(
        TextureHandle     src_image,
        BufferHandle      dst_buffer,
        std::uint64_t     dst_offset,
        const ImageRegion& region
    ) override
    {
        if (!textures_.contains(src_image.index()))
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kInvalidArgument,
                "copy_image_to_buffer: unknown src_image handle"));
        }
        auto bit = buffers_.find(dst_buffer.index());
        if (bit == buffers_.end())
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kInvalidArgument,
                "copy_image_to_buffer: unknown dst_buffer handle"));
        }
        auto& rec = bit->second;
        if (rec.cpu_storage.empty())
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kInvalidArgument,
                "copy_image_to_buffer: dst_buffer is GPU-only"));
        }
        // Determine bytes to zero from the src texture's format.
        const auto tex_it = textures_.find(src_image.index());
        const auto& tex_desc = tex_it->second;
        const auto& fmt_info = info_of(tex_desc.format);
        // For non-compressed formats bytes_per_block == bytes_per_texel.
        const std::uint64_t bytes =
            static_cast<std::uint64_t>(region.width) *
            static_cast<std::uint64_t>(region.height) *
            fmt_info.bytes_per_block;
        if (dst_offset + bytes > rec.cpu_storage.size())
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kInvalidArgument,
                "copy_image_to_buffer: dst range out of bounds"));
        }
        // Null backend: no real GPU data — write zeros so callers get a
        // deterministic buffer (tests can distinguish "touched" from
        // "untouched" by comparing zero-filled vs uninitialised memory).
        std::memset(rec.cpu_storage.data() + dst_offset, 0, static_cast<std::size_t>(bytes));
        return {};
    }

    [[nodiscard]] std::span<const std::byte> peek_buffer(BufferHandle h) const noexcept
    {
        auto it = buffers_.find(h.index());
        if (it == buffers_.end() || it->second.cpu_storage.empty())
            return {};
        return { it->second.cpu_storage.data(), it->second.cpu_storage.size() };
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

    // ---- Pipeline / layout (S3.1) -----------------------------------------
    [[nodiscard]] cd::core::Result<DescriptorSetLayoutHandle>
    create_descriptor_set_layout(const DescriptorSetLayoutDesc&) override
    {
        return DescriptorSetLayoutHandle { next_id_++, 1u };
    }

    void destroy_descriptor_set_layout(DescriptorSetLayoutHandle) override
    {
    }

    [[nodiscard]] cd::core::Result<PipelineLayoutHandle> create_pipeline_layout(const PipelineLayoutDesc&) override
    {
        return PipelineLayoutHandle { next_id_++, 1u };
    }

    void destroy_pipeline_layout(PipelineLayoutHandle) override
    {
    }

    [[nodiscard]] cd::core::Result<GraphicsPipelineHandle>
    create_graphics_pipeline(const GraphicsPipelineDesc& desc) override
    {
        if (!desc.vertex_shader.is_valid())
        {
            return std::unexpected(
                rhi_errors::make(rhi_errors::Code::kInvalidArgument, "graphics pipeline: vertex shader required")
            );
        }
        return GraphicsPipelineHandle { next_id_++, 1u };
    }

    void destroy_graphics_pipeline(GraphicsPipelineHandle) override
    {
    }

    [[nodiscard]] cd::core::Result<ComputePipelineHandle>
    create_compute_pipeline(const ComputePipelineDesc& desc) override
    {
        if (!desc.shader.is_valid())
        {
            return std::unexpected(
                rhi_errors::make(rhi_errors::Code::kInvalidArgument, "compute pipeline: shader required")
            );
        }
        return ComputePipelineHandle { next_id_++, 1u };
    }

    void destroy_compute_pipeline(ComputePipelineHandle) override
    {
    }

    // ---- Descriptor sets (S3.4 — stubbed for headless backend) -----------
    [[nodiscard]] cd::core::Result<DescriptorSetHandle>
    allocate_descriptor_set(DescriptorSetLayoutHandle layout) override
    {
        if (!layout.is_valid())
        {
            return std::unexpected(
                rhi_errors::make(rhi_errors::Code::kInvalidArgument, "allocate_descriptor_set: invalid layout")
            );
        }
        return DescriptorSetHandle { next_id_++, 1u };
    }

    void destroy_descriptor_set(DescriptorSetHandle) override
    {
    }

    [[nodiscard]] cd::core::Result<void>
    update_descriptor_set(DescriptorSetHandle, std::span<const DescriptorWrite>) override
    {
        // Headless backend: writes are accepted unconditionally — there is no
        // GPU-side resource to validate against.
        return {};
    }

    // ---- Synchronization (S3.5) — stubbed for headless backend ----------
    [[nodiscard]] cd::core::Result<SemaphoreHandle> create_semaphore() override
    {
        return SemaphoreHandle { next_id_++, 1u };
    }

    void destroy_semaphore(SemaphoreHandle) override
    {
    }

    [[nodiscard]] cd::core::Result<FenceHandle> create_fence(bool signaled) override
    {
        const auto id = next_id_++;
        fence_signaled_[id] = signaled;
        return FenceHandle { id, 1u };
    }

    void destroy_fence(FenceHandle h) override
    {
        fence_signaled_.erase(h.index());
    }

    [[nodiscard]] cd::core::Result<void> wait_for_fence(FenceHandle h, std::uint64_t) override
    {
        // Headless: fences are instantly signaled if they exist at all.
        auto it = fence_signaled_.find(h.index());
        if (it == fence_signaled_.end())
        {
            return std::unexpected(
                rhi_errors::make(rhi_errors::Code::kInvalidArgument, "wait_for_fence: unknown fence")
            );
        }
        it->second = true;
        return {};
    }

    void reset_fence(FenceHandle h) override
    {
        if (auto it = fence_signaled_.find(h.index()); it != fence_signaled_.end())
        {
            it->second = false;
        }
    }

    [[nodiscard]] bool is_fence_signaled(FenceHandle h) override
    {
        auto it = fence_signaled_.find(h.index());
        return it != fence_signaled_.end() && it->second;
    }

    // ---- Timeline semaphores (Vulkan 1.2+ core) — headless stubs --------
    [[nodiscard]] cd::core::Result<TimelineSemaphoreHandle>
    create_timeline_semaphore(std::uint64_t initial_value) override
    {
        const auto id = next_id_++;
        timeline_value_[id] = initial_value;
        return TimelineSemaphoreHandle { id, 1u };
    }

    void destroy_timeline_semaphore(TimelineSemaphoreHandle h) override
    {
        timeline_value_.erase(h.index());
    }

    [[nodiscard]] cd::core::Result<void>
    wait_timeline_semaphore(TimelineSemaphoreHandle h, std::uint64_t value, std::uint64_t) override
    {
        auto it = timeline_value_.find(h.index());
        if (it == timeline_value_.end())
        {
            return std::unexpected(
                rhi_errors::make(rhi_errors::Code::kInvalidArgument, "wait_timeline_semaphore: unknown handle")
            );
        }
        // Headless: snap to the requested value (no real GPU work to wait on).
        if (it->second < value)
            it->second = value;
        return {};
    }

    [[nodiscard]] cd::core::Result<void>
    signal_timeline_semaphore(TimelineSemaphoreHandle h, std::uint64_t value) override
    {
        auto it = timeline_value_.find(h.index());
        if (it == timeline_value_.end())
        {
            return std::unexpected(
                rhi_errors::make(rhi_errors::Code::kInvalidArgument, "signal_timeline_semaphore: unknown handle")
            );
        }
        if (value <= it->second)
        {
            return std::unexpected(
                rhi_errors::make(
                    rhi_errors::Code::kInvalidArgument,
                    "signal_timeline_semaphore: value must strictly increase"
                )
            );
        }
        it->second = value;
        return {};
    }

    [[nodiscard]] std::uint64_t timeline_semaphore_value(TimelineSemaphoreHandle h) const override
    {
        auto it = timeline_value_.find(h.index());
        return it == timeline_value_.end() ? 0U : it->second;
    }

    // ---- Swapchain acquire / present (S3.5) — headless stubs -----------
    [[nodiscard]] cd::core::Result<std::uint32_t>
    acquire_next_image(SwapchainHandle s, SemaphoreHandle, FenceHandle, std::uint64_t) override
    {
        if (!swapchains_.contains(s.index()))
        {
            return std::unexpected(
                rhi_errors::make(rhi_errors::Code::kInvalidArgument, "acquire_next_image: unknown swapchain")
            );
        }
        // Headless: always hand back slot 0.
        return std::uint32_t { 0 };
    }

    [[nodiscard]] cd::core::Result<void>
    present(SwapchainHandle s, std::uint32_t, std::span<const SemaphoreHandle>) override
    {
        if (!swapchains_.contains(s.index()))
        {
            return std::unexpected(rhi_errors::make(rhi_errors::Code::kInvalidArgument, "present: unknown swapchain"));
        }
        return {};
    }

    [[nodiscard]] TextureViewHandle swapchain_image_view(SwapchainHandle, std::uint32_t) const override
    {
        return TextureViewHandle {};
    }

    [[nodiscard]] std::uint32_t swapchain_image_count(SwapchainHandle s) const override
    {
        return !swapchains_.contains(s.index()) ? 0U : 2U;
    }

    [[nodiscard]] TextureHandle swapchain_image(SwapchainHandle s, std::uint32_t) const override
    {
        if (!swapchains_.contains(s.index()))
            return TextureHandle {};
        // Headless: the swapchain is fictional — return a deterministic but
        // never-allocated handle so callers can compare/store without UB.
        return TextureHandle { s.index() | (1U << 31), 1U };
    }

    // ---- Command buffers (S3.1) -------------------------------------------
    [[nodiscard]] std::unique_ptr<ICommandBuffer> do_create_command_buffer(QueueType) override
    {
        return std::make_unique<NullCommandBuffer>();
    }

    void submit(ICommandBuffer&) override
    {
        ++submit_count_;
    }

    [[nodiscard]] cd::core::Result<void> submit(const SubmitDesc& desc) override
    {
        // Headless: honor sync hand-offs by snapping timeline values forward and
        // signaling the fence (mirrors the Vulkan semantics on a CPU-only model).
        for (const auto& s : desc.signal_timeline_semaphores)
        {
            auto it = timeline_value_.find(s.semaphore.index());
            if (it == timeline_value_.end())
            {
                return std::unexpected(
                    rhi_errors::make(rhi_errors::Code::kInvalidArgument, "submit: unknown signal timeline")
                );
            }
            if (s.value > it->second)
                it->second = s.value;
        }
        if (desc.signal_fence.is_valid())
        {
            auto it = fence_signaled_.find(desc.signal_fence.index());
            if (it == fence_signaled_.end())
            {
                return std::unexpected(rhi_errors::make(rhi_errors::Code::kInvalidArgument, "submit: unknown fence"));
            }
            it->second = true;
        }
        ++submit_count_;
        return {};
    }

    void wait_idle() override
    {
    }

    [[nodiscard]] std::uint32_t submit_count() const noexcept
    {
        return submit_count_;
    }

    [[nodiscard]] std::size_t live_buffer_count() const noexcept
    {
        return buffers_.size();
    }

    [[nodiscard]] std::size_t live_texture_count() const noexcept
    {
        return textures_.size();
    }

private:
    struct BufferRecord
    {
        BufferDesc desc {};
        std::vector<std::byte> cpu_storage;
    };

    DeviceLimits limits_ {};
    DeviceFeatures features_ {};
    std::uint32_t next_id_ { 1 };

    std::unordered_map<std::uint32_t, BufferRecord> buffers_;
    std::unordered_map<std::uint32_t, TextureDesc> textures_;
    std::unordered_map<std::uint32_t, TextureViewDesc> views_;
    std::unordered_map<std::uint32_t, SamplerDesc> samplers_;
    std::unordered_map<std::uint32_t, ShaderModuleDesc> shaders_;
    std::unordered_map<std::uint32_t, SwapchainDesc> swapchains_;
    std::unordered_map<std::uint32_t, bool> fence_signaled_;
    std::unordered_map<std::uint32_t, std::uint64_t> timeline_value_;
    std::uint32_t submit_count_ { 0 };
};

}  // namespace cd::rhi
