// =============================================================================
// CHROMODYNAMIC — cd/rhi/NullCommandBuffer.hpp
// ADR-001 (Sprint S3.1) — headless command-buffer recorder.
//
// Records calls into a counter struct for unit testing without a GPU.
// =============================================================================
#pragma once

#include <cd/rhi/ICommandBuffer.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cd::rhi
{

struct NullCommandLog
{
    // Counters are 64-bit so they can hold any std::span<>::size() value
    // verbatim and call sites do not need a narrowing static_cast.
    std::uint64_t begin_count { 0 }, end_count { 0 };
    std::uint64_t begin_pass_count { 0 }, end_pass_count { 0 };
    std::uint64_t draws { 0 }, indexed_draws { 0 }, dispatches { 0 };
    std::uint64_t buffer_barriers { 0 }, texture_barriers { 0 };
    std::uint64_t bind_graphics_pipeline { 0 }, bind_compute_pipeline { 0 };
    std::uint64_t set_viewport { 0 }, set_scissor { 0 };
    std::uint64_t copies { 0 }, push_constants_count { 0 };
    std::vector<std::string> debug_groups;

    /// phase1115: deterministic lane-merge — counters sum, debug groups
    /// append IN CALL ORDER of absorb() (the recorder absorbs lanes in
    /// lane order, which is the determinism contract under test).
    void absorb(const NullCommandLog& o)
    {
        begin_count += o.begin_count;
        end_count += o.end_count;
        begin_pass_count += o.begin_pass_count;
        end_pass_count += o.end_pass_count;
        draws += o.draws;
        indexed_draws += o.indexed_draws;
        dispatches += o.dispatches;
        buffer_barriers += o.buffer_barriers;
        texture_barriers += o.texture_barriers;
        bind_graphics_pipeline += o.bind_graphics_pipeline;
        bind_compute_pipeline += o.bind_compute_pipeline;
        set_viewport += o.set_viewport;
        set_scissor += o.set_scissor;
        copies += o.copies;
        push_constants_count += o.push_constants_count;
        debug_groups.insert(debug_groups.end(),
                            o.debug_groups.begin(), o.debug_groups.end());
    }
};

class NullCommandBuffer final : public ICommandBuffer
{
public:
    NullCommandBuffer() noexcept = default;

    /// phase1115 (X1-FU-F): reference implementation of parallel pass
    /// recording. Lanes are headless NullCommandBuffers; finish()
    /// absorbs their logs into the primary IN LANE ORDER and closes
    /// the pass — the deterministic-merge contract the unit tests pin.
    [[nodiscard]] std::unique_ptr<IParallelPassRecorder>
    begin_parallel_render_pass(const RenderPassBeginInfo& info,
                               std::uint32_t lane_count) override;

    void begin() override
    {
        ++log_.begin_count;
        recording_ = true;
    }

    void end() override
    {
        ++log_.end_count;
        recording_ = false;
    }

    void begin_render_pass(const RenderPassBeginInfo&) override
    {
        ++log_.begin_pass_count;
    }

    void end_render_pass() override
    {
        ++log_.end_pass_count;
    }

    void bind_graphics_pipeline(GraphicsPipelineHandle) override
    {
        ++log_.bind_graphics_pipeline;
    }

    void bind_compute_pipeline(ComputePipelineHandle) override
    {
        ++log_.bind_compute_pipeline;
    }

    void bind_descriptor_set(std::uint32_t, DescriptorSetHandle) override
    {
    }

    void bind_vertex_buffer(std::uint32_t, BufferHandle, std::uint64_t) override
    {
    }

    void bind_index_buffer(BufferHandle, std::uint64_t, IndexType) override
    {
    }

    void push_constants(PipelineLayoutHandle, ShaderStage, std::uint32_t, std::uint32_t, const void*) override
    {
        ++log_.push_constants_count;
    }

    void set_viewport(const Viewport&) override
    {
        ++log_.set_viewport;
    }

    void set_scissor(const Rect2D&) override
    {
        ++log_.set_scissor;
    }

    void draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override
    {
        ++log_.draws;
    }

    void draw_indexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override
    {
        ++log_.indexed_draws;
    }

    void dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override
    {
        ++log_.dispatches;
    }

    void copy_buffer(BufferHandle, BufferHandle, std::span<const BufferCopyRegion> regions) override
    {
        log_.copies += regions.size();
    }

    void copy_buffer_to_image(BufferHandle, TextureHandle, std::span<const BufferImageCopyRegion> regions) override
    {
        log_.copies += regions.size();
    }

    void copy_image_to_buffer(TextureHandle, BufferHandle, std::span<const BufferImageCopyRegion> regions) override
    {
        log_.copies += regions.size();
    }

    void barrier(std::span<const BufferBarrier> bb, std::span<const TextureBarrier> tb) override
    {
        log_.buffer_barriers += bb.size();
        log_.texture_barriers += tb.size();
    }

    void push_debug_group(std::string_view name) override
    {
        log_.debug_groups.emplace_back(name);
    }

    void pop_debug_group() override
    {
    }

    [[nodiscard]] const NullCommandLog& log() const noexcept
    {
        return log_;
    }

    [[nodiscard]] bool is_recording() const noexcept
    {
        return recording_;
    }

    /// phase1115: lane-merge entry point (used by the recorder below).
    void absorb_log(const NullCommandLog& o)
    {
        log_.absorb(o);
    }

private:
    NullCommandLog log_ {};
    bool recording_ { false };
};

/// phase1115 — Null lanes. Each lane is its own NullCommandBuffer
/// (thread-confined by contract); finish() merges lane logs into the
/// primary in lane order and ends the pass on the primary.
class NullParallelPassRecorder final : public IParallelPassRecorder
{
public:
    NullParallelPassRecorder(NullCommandBuffer& primary, std::uint32_t lanes)
        : primary_ { &primary }
        , lanes_ { lanes == 0 ? 1u : lanes }
    {
        lane_buffers_ = std::vector<NullCommandBuffer>(lanes_);
    }

    [[nodiscard]] std::uint32_t lane_count() const noexcept override
    {
        return lanes_;
    }

    [[nodiscard]] IDrawRecorder& lane(std::uint32_t i) noexcept override
    {
        return lane_buffers_[i < lanes_ ? i : lanes_ - 1u];
    }

    void finish() override
    {
        if (finished_)
            return;
        for (auto& lb : lane_buffers_)
            primary_->absorb_log(lb.log());
        primary_->end_render_pass();
        finished_ = true;
    }

private:
    NullCommandBuffer*             primary_;
    std::uint32_t                  lanes_;
    std::vector<NullCommandBuffer> lane_buffers_;
    bool                           finished_ { false };
};

inline std::unique_ptr<IParallelPassRecorder>
NullCommandBuffer::begin_parallel_render_pass(const RenderPassBeginInfo& info,
                                              std::uint32_t lane_count)
{
    begin_render_pass(info);
    return std::make_unique<NullParallelPassRecorder>(*this, lane_count);
}

}  // namespace cd::rhi
