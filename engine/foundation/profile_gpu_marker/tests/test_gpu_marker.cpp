// =============================================================================
// CHROMODYNAMIC — cd::profile::gpu_marker tests
// Phase 606
//
// Tests validate the API surface and stub counter behavior.
// NullCommandBuffer and NullDevice are used so no GPU is required.
// =============================================================================
#include <cd/profile/gpu_marker/GpuMarker.hpp>

#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/NullCommandBuffer.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <vector>

using cd::profile::gpu_marker::MarkerHandle;
using cd::profile::gpu_marker::Recorder;
using cd::profile::gpu_marker::Scope;

// ---------------------------------------------------------------------------
// Test 1: begin_marker + end_marker round-trip
//   * handle returned by begin_marker is valid.
//   * After end_marker + resolve, one sample is visible in samples().
//   * Sample name matches.
//   * duration_ms_computed > 0 (stub counter ticks differ by at least 1).
// ---------------------------------------------------------------------------
TEST(GpuMarker_Recorder, BeginEndRoundTrip)
{
    cd::rhi::NullCommandBuffer cmd;
    cd::rhi::NullDevice        dev;
    Recorder                   rec;

    EXPECT_TRUE(rec.samples().empty());

    const MarkerHandle h = rec.begin_marker(cmd, "shadow_pass");
    EXPECT_TRUE(h.valid());

    rec.end_marker(cmd, h);

    // Before resolve: resolved bucket is empty (sample sits in pending).
    EXPECT_TRUE(rec.samples().empty());

    rec.resolve(dev);

    const auto s = rec.samples();
    ASSERT_EQ(s.size(), 1U);
    EXPECT_EQ(s[0].name, "shadow_pass");
    // end_tick must be strictly after start_tick.
    EXPECT_GT(s[0].gpu_end_tick, s[0].gpu_start_tick);
    // With stub 1 GHz clock, 1 tick = 1e-6 ms, so duration_ms_computed > 0.
    EXPECT_GT(s[0].duration_ms_computed, 0.0);

    // Debug group bookkeeping: NullCommandBuffer tracks push_debug_group calls.
    EXPECT_FALSE(cmd.log().debug_groups.empty());
    EXPECT_EQ(cmd.log().debug_groups[0], "shadow_pass");
}

// ---------------------------------------------------------------------------
// Test 2: RAII Scope — constructor calls begin_marker, destructor calls
//         end_marker, one sample committed after scope exits.
// ---------------------------------------------------------------------------
TEST(GpuMarker_Scope, RaiiScopeCommitsSample)
{
    cd::rhi::NullCommandBuffer cmd;
    cd::rhi::NullDevice        dev;
    Recorder                   rec;

    {
        Scope sc(rec, cmd, "geometry_pass");
        // Pending is non-empty only internally — resolved bucket stays empty
        // until resolve().
    }
    // Destructor ran → end_marker called → sample in pending.
    rec.resolve(dev);

    const auto s = rec.samples();
    ASSERT_EQ(s.size(), 1U);
    EXPECT_EQ(s[0].name, "geometry_pass");
    EXPECT_GT(s[0].gpu_end_tick, s[0].gpu_start_tick);
}

// ---------------------------------------------------------------------------
// Test 3: Multiple markers in flight — 3 sequential markers, all resolved.
//   * Tick ordering: each begin_marker/end_marker call advances the counter.
//   * All 3 samples present after resolve().
// ---------------------------------------------------------------------------
TEST(GpuMarker_Recorder, MultipleInFlight)
{
    cd::rhi::NullCommandBuffer cmd;
    cd::rhi::NullDevice        dev;
    Recorder                   rec;

    const MarkerHandle h0 = rec.begin_marker(cmd, "depth_pre");
    const MarkerHandle h1 = rec.begin_marker(cmd, "lighting");
    const MarkerHandle h2 = rec.begin_marker(cmd, "post_fx");

    rec.end_marker(cmd, h0);
    rec.end_marker(cmd, h1);
    rec.end_marker(cmd, h2);

    rec.resolve(dev);

    const auto s = rec.samples();
    ASSERT_EQ(s.size(), 3U);

    // Names must match insertion order.
    EXPECT_EQ(s[0].name, "depth_pre");
    EXPECT_EQ(s[1].name, "lighting");
    EXPECT_EQ(s[2].name, "post_fx");

    // Each sample must have end_tick > start_tick.
    for (const auto& sample : s)
    {
        EXPECT_GT(sample.gpu_end_tick, sample.gpu_start_tick)
            << "Failed for sample: " << sample.name;
        EXPECT_GT(sample.duration_ms_computed, 0.0)
            << "Zero duration for sample: " << sample.name;
    }
}

// ---------------------------------------------------------------------------
// Test 4: clear() empties resolved and resets the counter.
//   * After clear(), samples() is empty.
//   * New markers recorded after clear() start from tick 0 again.
// ---------------------------------------------------------------------------
TEST(GpuMarker_Recorder, ClearEmptiesSamples)
{
    cd::rhi::NullCommandBuffer cmd;
    cd::rhi::NullDevice        dev;
    Recorder                   rec;

    {
        const MarkerHandle h = rec.begin_marker(cmd, "before_clear");
        rec.end_marker(cmd, h);
    }
    rec.resolve(dev);
    ASSERT_EQ(rec.samples().size(), 1U);

    rec.clear();
    EXPECT_TRUE(rec.samples().empty());

    // After clear, new markers still work correctly.
    const MarkerHandle h2 = rec.begin_marker(cmd, "after_clear");
    rec.end_marker(cmd, h2);
    rec.resolve(dev);

    const auto s = rec.samples();
    ASSERT_EQ(s.size(), 1U);
    EXPECT_EQ(s[0].name, "after_clear");
    // Counter reset: start_tick should be 0 (first begin after clear).
    EXPECT_EQ(s[0].gpu_start_tick, 0U);
}

// ---------------------------------------------------------------------------
// Test 5: end_marker with invalid handle is a no-op (no crash, no sample).
// ---------------------------------------------------------------------------
TEST(GpuMarker_Recorder, InvalidHandleNoOp)
{
    cd::rhi::NullCommandBuffer cmd;
    cd::rhi::NullDevice        dev;
    Recorder                   rec;

    const MarkerHandle bad {};
    EXPECT_FALSE(bad.valid());

    // Must not crash.
    rec.end_marker(cmd, bad);
    rec.resolve(dev);
    EXPECT_TRUE(rec.samples().empty());
}

// ---------------------------------------------------------------------------
// Real RHI timestamp path (A-QUERY) — fake timestamp device.
//
// FakeTimestampDevice reports features().timestamp_queries == true, hands out a
// kTimestamp QueryPool, records the per-slot write order, and returns canned
// nanosecond values from get_query_results — so the real begin/end/resolve code
// path is exercised deterministically with no GPU. On-GPU (RTX 3080 / Vulkan)
// verification lives in the renderer's query-pool device tests; linking a real
// backend into this foundation binary would cross the foundation→backend layer.
// ---------------------------------------------------------------------------
namespace
{

// cd::rhi::NullDevice is `final`, so (following the test_material_recreate.cpp
// pattern) this is a forwarding IDevice that owns a NullDevice and delegates
// every call to it, overriding only features() + the four A-QUERY entry points
// the Recorder uses (create/destroy_query_pool, get_query_results).
class FakeTimestampDevice final : public cd::rhi::IDevice
{
public:
    FakeTimestampDevice() { feats_.timestamp_queries = true; }

    // ---- faked: timestamp feature + A-QUERY surface ----
    [[nodiscard]] const cd::rhi::DeviceFeatures& features() const noexcept override
    {
        return feats_;
    }

    [[nodiscard]] cd::core::Result<cd::rhi::QueryPoolHandle>
    create_query_pool(const cd::rhi::QueryPoolDesc& desc) override
    {
        last_pool_count_ = desc.count;
        // Slot value == 100 * (slot_index + 1) nanoseconds, so each begin/end
        // pair (slots 0/1, 2/3, ...) has a known, strictly-positive delta.
        ns_.assign(desc.count, 0U);
        for (std::uint32_t i = 0; i < desc.count; ++i)
        {
            ns_[i] = 100U * static_cast<std::uint64_t>(i + 1U);
        }
        return cd::rhi::QueryPoolHandle { ++pool_id_, 1U };
    }

    void destroy_query_pool(cd::rhi::QueryPoolHandle) override { ++destroy_count_; }

    [[nodiscard]] cd::core::Result<void>
    get_query_results(cd::rhi::QueryPoolHandle pool,
                      std::uint32_t            first,
                      std::uint32_t            count,
                      std::span<std::uint64_t> out) override
    {
        if (!pool.is_valid() || first + count > ns_.size() || out.size() < count)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument, "fake: bad range"));
        }
        for (std::uint32_t i = 0; i < count; ++i)
        {
            out[i] = ns_[first + i];
        }
        return {};
    }

    [[nodiscard]] std::uint32_t last_pool_count() const noexcept { return last_pool_count_; }
    [[nodiscard]] std::uint32_t destroy_count() const noexcept { return destroy_count_; }

    // ---- pure forwards (introspection) ----
    [[nodiscard]] cd::rhi::Backend backend() const noexcept override { return inner_.backend(); }
    [[nodiscard]] std::string_view adapter_name() const noexcept override { return inner_.adapter_name(); }
    [[nodiscard]] const cd::rhi::DeviceLimits& limits() const noexcept override { return inner_.limits(); }

    // ---- pure forwards (resources) ----
    [[nodiscard]] cd::core::Result<cd::rhi::BufferHandle> create_buffer(const cd::rhi::BufferDesc& d) override { return inner_.create_buffer(d); }
    void destroy_buffer(cd::rhi::BufferHandle h) override { inner_.destroy_buffer(h); }
    [[nodiscard]] cd::core::Result<cd::rhi::TextureHandle> create_texture(const cd::rhi::TextureDesc& d) override { return inner_.create_texture(d); }
    void destroy_texture(cd::rhi::TextureHandle h) override { inner_.destroy_texture(h); }
    [[nodiscard]] cd::core::Result<cd::rhi::TextureViewHandle> create_texture_view(const cd::rhi::TextureViewDesc& d) override { return inner_.create_texture_view(d); }
    void destroy_texture_view(cd::rhi::TextureViewHandle h) override { inner_.destroy_texture_view(h); }
    [[nodiscard]] cd::core::Result<cd::rhi::SamplerHandle> create_sampler(const cd::rhi::SamplerDesc& d) override { return inner_.create_sampler(d); }
    void destroy_sampler(cd::rhi::SamplerHandle h) override { inner_.destroy_sampler(h); }
    [[nodiscard]] cd::core::Result<cd::rhi::ShaderModuleHandle> create_shader_module(const cd::rhi::ShaderModuleDesc& d) override { return inner_.create_shader_module(d); }
    void destroy_shader_module(cd::rhi::ShaderModuleHandle h) override { inner_.destroy_shader_module(h); }
    [[nodiscard]] cd::core::Result<cd::rhi::DescriptorSetLayoutHandle> create_descriptor_set_layout(const cd::rhi::DescriptorSetLayoutDesc& d) override { return inner_.create_descriptor_set_layout(d); }
    void destroy_descriptor_set_layout(cd::rhi::DescriptorSetLayoutHandle h) override { inner_.destroy_descriptor_set_layout(h); }
    [[nodiscard]] cd::core::Result<cd::rhi::PipelineLayoutHandle> create_pipeline_layout(const cd::rhi::PipelineLayoutDesc& d) override { return inner_.create_pipeline_layout(d); }
    void destroy_pipeline_layout(cd::rhi::PipelineLayoutHandle h) override { inner_.destroy_pipeline_layout(h); }
    [[nodiscard]] cd::core::Result<cd::rhi::GraphicsPipelineHandle> create_graphics_pipeline(const cd::rhi::GraphicsPipelineDesc& d) override { return inner_.create_graphics_pipeline(d); }
    void destroy_graphics_pipeline(cd::rhi::GraphicsPipelineHandle h) override { inner_.destroy_graphics_pipeline(h); }
    [[nodiscard]] cd::core::Result<cd::rhi::ComputePipelineHandle> create_compute_pipeline(const cd::rhi::ComputePipelineDesc& d) override { return inner_.create_compute_pipeline(d); }
    void destroy_compute_pipeline(cd::rhi::ComputePipelineHandle h) override { inner_.destroy_compute_pipeline(h); }

    // ---- pure forwards (descriptor sets) ----
    [[nodiscard]] cd::core::Result<cd::rhi::DescriptorSetHandle> allocate_descriptor_set(cd::rhi::DescriptorSetLayoutHandle l) override { return inner_.allocate_descriptor_set(l); }
    void destroy_descriptor_set(cd::rhi::DescriptorSetHandle h) override { inner_.destroy_descriptor_set(h); }
    [[nodiscard]] cd::core::Result<void> update_descriptor_set(cd::rhi::DescriptorSetHandle s, std::span<const cd::rhi::DescriptorWrite> w) override { return inner_.update_descriptor_set(s, w); }

    // ---- pure forwards (sync) ----
    [[nodiscard]] cd::core::Result<cd::rhi::SemaphoreHandle> create_semaphore() override { return inner_.create_semaphore(); }
    void destroy_semaphore(cd::rhi::SemaphoreHandle h) override { inner_.destroy_semaphore(h); }
    [[nodiscard]] cd::core::Result<cd::rhi::FenceHandle> create_fence(bool s) override { return inner_.create_fence(s); }
    void destroy_fence(cd::rhi::FenceHandle h) override { inner_.destroy_fence(h); }
    [[nodiscard]] cd::core::Result<void> wait_for_fence(cd::rhi::FenceHandle f, std::uint64_t t) override { return inner_.wait_for_fence(f, t); }
    void reset_fence(cd::rhi::FenceHandle f) override { inner_.reset_fence(f); }
    [[nodiscard]] bool is_fence_signaled(cd::rhi::FenceHandle f) override { return inner_.is_fence_signaled(f); }
    [[nodiscard]] cd::core::Result<cd::rhi::TimelineSemaphoreHandle> create_timeline_semaphore(std::uint64_t v) override { return inner_.create_timeline_semaphore(v); }
    void destroy_timeline_semaphore(cd::rhi::TimelineSemaphoreHandle h) override { inner_.destroy_timeline_semaphore(h); }
    [[nodiscard]] cd::core::Result<void> wait_timeline_semaphore(cd::rhi::TimelineSemaphoreHandle h, std::uint64_t v, std::uint64_t t) override { return inner_.wait_timeline_semaphore(h, v, t); }
    [[nodiscard]] cd::core::Result<void> signal_timeline_semaphore(cd::rhi::TimelineSemaphoreHandle h, std::uint64_t v) override { return inner_.signal_timeline_semaphore(h, v); }
    [[nodiscard]] std::uint64_t timeline_semaphore_value(cd::rhi::TimelineSemaphoreHandle h) const override { return inner_.timeline_semaphore_value(h); }

    // ---- pure forwards (swapchain) ----
    [[nodiscard]] cd::core::Result<std::uint32_t> acquire_next_image(cd::rhi::SwapchainHandle s, cd::rhi::SemaphoreHandle sig, cd::rhi::FenceHandle f, std::uint64_t t) override { return inner_.acquire_next_image(s, sig, f, t); }
    [[nodiscard]] cd::core::Result<void> present(cd::rhi::SwapchainHandle s, std::uint32_t i, std::span<const cd::rhi::SemaphoreHandle> w) override { return inner_.present(s, i, w); }
    [[nodiscard]] cd::rhi::TextureViewHandle swapchain_image_view(cd::rhi::SwapchainHandle s, std::uint32_t i) const override { return inner_.swapchain_image_view(s, i); }
    [[nodiscard]] std::uint32_t swapchain_image_count(cd::rhi::SwapchainHandle s) const override { return inner_.swapchain_image_count(s); }
    [[nodiscard]] cd::rhi::TextureHandle swapchain_image(cd::rhi::SwapchainHandle s, std::uint32_t i) const override { return inner_.swapchain_image(s, i); }
    [[nodiscard]] cd::core::Result<cd::rhi::SwapchainHandle> create_swapchain(const cd::rhi::SwapchainDesc& d) override { return inner_.create_swapchain(d); }
    void destroy_swapchain(cd::rhi::SwapchainHandle h) override { inner_.destroy_swapchain(h); }

    // ---- pure forwards (staging) ----
    [[nodiscard]] cd::core::Result<void> upload_buffer(cd::rhi::BufferHandle h, std::uint64_t o, std::span<const std::byte> d) override { return inner_.upload_buffer(h, o, d); }
    [[nodiscard]] cd::core::Result<void> download_buffer(cd::rhi::BufferHandle h, std::uint64_t o, std::span<std::byte> d) override { return inner_.download_buffer(h, o, d); }

    // ---- pure forwards (command buffers) ----
    [[nodiscard]] std::unique_ptr<cd::rhi::ICommandBuffer> do_create_command_buffer(cd::rhi::QueueType q) override { return inner_.create_command_buffer(q); }
    void submit(cd::rhi::ICommandBuffer& c) override { inner_.submit(c); }
    [[nodiscard]] cd::core::Result<void> submit(const cd::rhi::SubmitDesc& d) override { return inner_.submit(d); }
    void wait_idle() override { inner_.wait_idle(); }

private:
    cd::rhi::NullDevice        inner_;
    cd::rhi::DeviceFeatures    feats_ {};
    std::vector<std::uint64_t> ns_;
    std::uint32_t              pool_id_ { 0 };
    std::uint32_t              last_pool_count_ { 0 };
    std::uint32_t              destroy_count_ { 0 };
};

}  // namespace

// ---------------------------------------------------------------------------
// Test 6: prepare() returns false on a device without timestamp support, and
//         the recorder transparently stays on the stub path.
// ---------------------------------------------------------------------------
TEST(GpuMarker_RealPath, PrepareNoOpWithoutTimestampSupport)
{
    cd::rhi::NullCommandBuffer cmd;
    cd::rhi::NullDevice        dev;  // timestamp_queries == false
    Recorder                   rec;

    EXPECT_FALSE(rec.prepare(dev, 4));

    const MarkerHandle h = rec.begin_marker(cmd, "stub_region");
    rec.end_marker(cmd, h);
    rec.resolve(dev);

    const auto s = rec.samples();
    ASSERT_EQ(s.size(), 1U);
    EXPECT_GT(s[0].duration_ms_computed, 0.0);  // stub 1 GHz path still ticks
}

// ---------------------------------------------------------------------------
// Test 7: real timestamp path — prepare() creates a 2*N-slot pool, begin/end
//         emit write_timestamp, and resolve() reads real ns → ms.
//   * pool sized to 2 * max_markers.
//   * write_timestamp called once per begin and once per end (NullCommandBuffer
//     counts them).
//   * duration computed from canned ns: slot1(200ns) - slot0(100ns) = 100ns
//     → 1e-4 ms.
// ---------------------------------------------------------------------------
TEST(GpuMarker_RealPath, ResolvesRealNanoseconds)
{
    cd::rhi::NullCommandBuffer cmd;
    FakeTimestampDevice        dev;
    Recorder                   rec;

    ASSERT_TRUE(rec.prepare(dev, 3));
    EXPECT_EQ(dev.last_pool_count(), 6U);  // 2 slots * 3 markers

    const MarkerHandle h0 = rec.begin_marker(cmd, "depth");   // slots 0,1
    rec.end_marker(cmd, h0);
    const MarkerHandle h1 = rec.begin_marker(cmd, "lighting"); // slots 2,3
    rec.end_marker(cmd, h1);

    EXPECT_EQ(cmd.log().query_writes, 4U);  // 2 begins + 2 ends

    rec.resolve(dev);

    const auto s = rec.samples();
    ASSERT_EQ(s.size(), 2U);

    // depth: begin slot0=100ns, end slot1=200ns → delta 100ns = 1e-4 ms.
    EXPECT_EQ(s[0].name, "depth");
    EXPECT_EQ(s[0].gpu_start_tick, 100U);
    EXPECT_EQ(s[0].gpu_end_tick, 200U);
    EXPECT_DOUBLE_EQ(s[0].duration_ms_computed, 100.0 / 1.0e6);

    // lighting: begin slot2=300ns, end slot3=400ns → delta 100ns.
    EXPECT_EQ(s[1].name, "lighting");
    EXPECT_EQ(s[1].gpu_start_tick, 300U);
    EXPECT_EQ(s[1].gpu_end_tick, 400U);
    EXPECT_DOUBLE_EQ(s[1].duration_ms_computed, 100.0 / 1.0e6);
}

// ---------------------------------------------------------------------------
// Test 8: prepare() is idempotent — a second prepare with an equal-or-smaller
//         max_markers reuses the existing pool (no new create / destroy); the
//         destructor releases the pool exactly once.
// ---------------------------------------------------------------------------
TEST(GpuMarker_RealPath, PrepareIdempotentAndReleasesPool)
{
    FakeTimestampDevice dev;
    {
        Recorder rec;
        ASSERT_TRUE(rec.prepare(dev, 4));
        EXPECT_EQ(dev.last_pool_count(), 8U);

        // Smaller request reuses the pool — no destroy yet.
        ASSERT_TRUE(rec.prepare(dev, 2));
        EXPECT_EQ(dev.destroy_count(), 0U);
        EXPECT_EQ(dev.last_pool_count(), 8U);  // unchanged: kept the bigger pool
    }
    // Destructor released the pool exactly once.
    EXPECT_EQ(dev.destroy_count(), 1U);
}
