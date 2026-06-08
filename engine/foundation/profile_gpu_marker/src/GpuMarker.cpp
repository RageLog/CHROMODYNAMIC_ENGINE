// =============================================================================
// CHROMODYNAMIC — cd/profile/gpu_marker/GpuMarker.cpp
//
// Phase 606 — Stub implementation of GPU marker Recorder.
// Phase 739 — stub-wire RHI-contract-gap doc pass (FINALE-2 W3B B9 BLOCKED).
//             See GpuMarker.hpp "RHI_CONTRACT_GAP" block for the precise
//             cd::rhi additions required to lift this stub to real GPU
//             timestamps. Until those land, this implementation provides
//             deterministic, testable duration values via a 1 GHz monotonic
//             counter so the editor overlay + tests remain live.
//
// Wire-up path once the RHI gap closes (single-file change to this .cpp):
//   * Impl gains: QueryPoolHandle pool_; std::uint32_t next_index_;
//                 std::vector<std::uint64_t> raw_ticks_;
//   * begin_marker / end_marker call cmd.write_timestamp(stage, pool_, ix).
//   * resolve() calls device.get_query_pool_results(pool_, 0, n, dst, kWait)
//                then divides by device.gpu_tick_frequency() instead of
//                kStubTickFrequencyHz.
// =============================================================================
#include <cd/profile/gpu_marker/GpuMarker.hpp>

#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cd::profile::gpu_marker
{

namespace
{

// Stub GPU clock frequency: 1 GHz → 1 tick = 1 ns → 1e6 ticks = 1 ms.
// Replace with device.gpu_tick_frequency() when the IDevice API lands.
inline constexpr double kStubTickFrequencyHz = 1.0e9;

/// One in-flight (begun but not yet ended) GPU marker.
struct InFlight
{
    std::string   name;
    std::uint64_t start_tick { 0 };
    bool          active { false };
};

}  // namespace

// ---------------------------------------------------------------------------
// Recorder::Impl
// ---------------------------------------------------------------------------

struct Recorder::Impl
{
    // Monotonic counter substituting GPU timestamps until write_timestamp lands.
    std::uint64_t monotonic_counter { 0 };

    // In-flight table — slots are reused via a free-list.
    std::vector<InFlight>      in_flight;
    std::vector<std::uint32_t> free_list;

    // Completed (but not yet resolved) samples.
    // After resolve() is called, duration_ms_computed is populated.
    std::vector<GpuMarkerSample> pending;

    // Resolved samples (post-resolve()).
    std::vector<GpuMarkerSample> resolved;

    [[nodiscard]] std::uint32_t alloc_slot()
    {
        if (!free_list.empty())
        {
            const std::uint32_t idx = free_list.back();
            free_list.pop_back();
            return idx;
        }
        in_flight.push_back({});
        return static_cast<std::uint32_t>(in_flight.size() - 1U);
    }

    [[nodiscard]] std::uint64_t next_tick() noexcept
    {
        return monotonic_counter++;
    }
};

// ---------------------------------------------------------------------------
// Recorder
// ---------------------------------------------------------------------------

Recorder::Recorder()
    : impl_(std::make_unique<Impl>())
{
}

Recorder::~Recorder() = default;

MarkerHandle Recorder::begin_marker(cd::rhi::ICommandBuffer& cmd,
                                    std::string_view         name)
{
    // Emit a debug-group for capture-tool visibility.
    cmd.push_debug_group(name);

    const std::uint64_t tick = impl_->next_tick();

    const std::uint32_t idx = impl_->alloc_slot();
    InFlight& slot  = impl_->in_flight[idx];
    slot.name       = std::string(name);
    slot.start_tick = tick;
    slot.active     = true;

    return MarkerHandle { idx };
}

void Recorder::end_marker(cd::rhi::ICommandBuffer& cmd, MarkerHandle handle)
{
    if (!handle.valid())
        return;

    // Emit matching pop before touching state so debug tools see the balanced pair.
    cmd.pop_debug_group();

    const std::uint64_t tick = impl_->next_tick();

    if (handle.index >= static_cast<std::uint32_t>(impl_->in_flight.size()))
        return;

    InFlight& slot = impl_->in_flight[handle.index];
    if (!slot.active)
        return;

    GpuMarkerSample sample;
    sample.name           = std::move(slot.name);
    sample.gpu_start_tick = slot.start_tick;
    sample.gpu_end_tick   = tick;
    // duration_ms_computed populated by resolve().
    sample.duration_ms_computed = 0.0;

    slot.active = false;
    impl_->free_list.push_back(handle.index);

    impl_->pending.push_back(std::move(sample));
}

void Recorder::resolve(cd::rhi::IDevice& /*device*/)
{
    // TODO: when IDevice::gpu_tick_frequency() exists, use:
    //   const double freq = static_cast<double>(device.gpu_tick_frequency());
    // For now, use the stub frequency.
    const double freq = kStubTickFrequencyHz;

    for (auto& s : impl_->pending)
    {
        const std::uint64_t delta = s.gpu_end_tick - s.gpu_start_tick;
        // ticks / (ticks/s) = seconds; * 1000.0 = milliseconds.
        s.duration_ms_computed =
            (freq > 0.0)
                ? (static_cast<double>(delta) / freq) * 1000.0
                : 0.0;
        impl_->resolved.push_back(std::move(s));
    }
    impl_->pending.clear();
}

std::span<const GpuMarkerSample> Recorder::samples() const noexcept
{
    return { impl_->resolved.data(), impl_->resolved.size() };
}

void Recorder::clear() noexcept
{
    impl_->pending.clear();
    impl_->resolved.clear();
    impl_->in_flight.clear();
    impl_->free_list.clear();
    impl_->monotonic_counter = 0;
}

}  // namespace cd::profile::gpu_marker
