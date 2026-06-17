// =============================================================================
// CHROMODYNAMIC — cd/profile/gpu_marker/GpuMarker.cpp
//
// Phase 606 — GPU marker Recorder.
// Phase 739 — stub-wire RHI-contract-gap doc pass (FINALE-2 W3B B9 BLOCKED).
// BAND5-foundation (2026-06-16) — RHI gap CLOSED. The A-QUERY subsystem
//             (phase1218) shipped create_query_pool / write_timestamp /
//             get_query_results (ns-resolved). Recorder now owns a kTimestamp
//             QueryPool (via prepare) and reads real nanoseconds at resolve.
//             A 1 GHz monotonic counter remains the fallback for devices
//             without features().timestamp_queries (Null, headless). See the
//             GpuMarker.hpp banner + ADR-20260616-band5-foundation-scope.md.
// =============================================================================
#include <cd/profile/gpu_marker/GpuMarker.hpp>

#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace cd::profile::gpu_marker
{

namespace
{

// Stub GPU clock frequency: 1 GHz → 1 tick = 1 ns → 1e6 ticks = 1 ms. Used
// only when the device has no timestamp support (Null / headless). On a real
// device IDevice::get_query_results returns nanoseconds directly.
inline constexpr double kStubTickFrequencyHz = 1.0e9;

// 1 ns expressed in milliseconds (real path divides ns deltas by this scale).
inline constexpr double kNanosecondsPerMillisecond = 1.0e6;

/// One in-flight (begun but not yet ended) GPU marker.
struct InFlight
{
    std::string   name;
    std::uint64_t start_tick { 0 };
    std::uint32_t begin_slot { 0 };  ///< Timestamp slot (real path only).
    std::uint32_t end_slot   { 0 };  ///< Timestamp slot (real path only).
    bool          active { false };
};

}  // namespace

// ---------------------------------------------------------------------------
// Recorder::Impl
// ---------------------------------------------------------------------------

/// A completed sample paired with the timestamp slots it occupies (real path).
struct PendingSample
{
    GpuMarkerSample sample;
    std::uint32_t   begin_slot { 0 };
    std::uint32_t   end_slot   { 0 };
};

struct Recorder::Impl
{
    // Monotonic counter substituting GPU timestamps on devices without
    // timestamp support; advanced only on the stub path.
    std::uint64_t monotonic_counter { 0 };

    // In-flight table — slots are reused via a free-list.
    std::vector<InFlight>      in_flight;
    std::vector<std::uint32_t> free_list;

    // Completed (but not yet resolved) samples.
    std::vector<PendingSample> pending;

    // Resolved samples (post-resolve()).
    std::vector<GpuMarkerSample> resolved;

    // ---- Real RHI timestamp path (active when pool.is_valid()) ------------
    cd::rhi::IDevice*       device { nullptr };  ///< Non-owning; owner of pool.
    cd::rhi::QueryPoolHandle pool {};            ///< kTimestamp pool, 2 slots/marker.
    std::uint32_t           pool_capacity { 0 };  ///< Slot count == 2 * max_markers.
    std::uint32_t           next_timestamp_slot { 0 };  ///< Per-frame write cursor.

    [[nodiscard]] bool timestamps_active() const noexcept { return pool.is_valid(); }

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

    void release_pool() noexcept
    {
        if (device != nullptr && pool.is_valid())
        {
            device->destroy_query_pool(pool);
        }
        pool = {};
        pool_capacity = 0;
        next_timestamp_slot = 0;
    }
};

// ---------------------------------------------------------------------------
// Recorder
// ---------------------------------------------------------------------------

Recorder::Recorder()
    : impl_(std::make_unique<Impl>())
{
}

Recorder::~Recorder()
{
    impl_->release_pool();
}

bool Recorder::prepare(cd::rhi::IDevice& device, std::uint32_t max_markers)
{
    // Devices without timestamp support stay on the monotonic-counter path.
    if (!device.features().timestamp_queries)
    {
        return false;
    }

    const std::uint32_t needed_slots = max_markers * 2U;  // begin + end per marker.

    // Already have a pool large enough on this same device — nothing to do.
    if (impl_->pool.is_valid() && impl_->device == &device &&
        impl_->pool_capacity >= needed_slots)
    {
        return true;
    }

    // Grow / re-target: release any existing pool first (idempotent).
    impl_->release_pool();
    impl_->device = &device;

    cd::rhi::QueryPoolDesc desc {};
    desc.type  = cd::rhi::QueryType::kTimestamp;
    desc.count = needed_slots;

    auto created = device.create_query_pool(desc);
    if (!created.has_value())
    {
        // Backend declined (e.g. NotImplemented despite the feature flag):
        // fall back to the stub path cleanly.
        impl_->device = nullptr;
        return false;
    }

    impl_->pool                = created.value();
    impl_->pool_capacity       = needed_slots;
    impl_->next_timestamp_slot = 0;
    return true;
}

MarkerHandle Recorder::begin_marker(cd::rhi::ICommandBuffer& cmd,
                                    std::string_view         name)
{
    // Emit a debug-group for capture-tool visibility.
    cmd.push_debug_group(name);

    const std::uint32_t idx = impl_->alloc_slot();
    InFlight& slot  = impl_->in_flight[idx];
    slot.name       = std::string(name);
    slot.active     = true;

    if (impl_->timestamps_active() &&
        impl_->next_timestamp_slot + 1U < impl_->pool_capacity)
    {
        slot.begin_slot = impl_->next_timestamp_slot++;
        slot.end_slot   = impl_->next_timestamp_slot++;
        slot.start_tick = 0;  // Filled from the device at resolve().
        cmd.write_timestamp(impl_->pool, slot.begin_slot);
    }
    else
    {
        // Stub path (or pool exhausted this frame): monotonic counter.
        slot.begin_slot = ~0U;
        slot.end_slot   = ~0U;
        slot.start_tick = impl_->next_tick();
    }

    return MarkerHandle { idx };
}

void Recorder::end_marker(cd::rhi::ICommandBuffer& cmd, MarkerHandle handle)
{
    if (!handle.valid())
        return;

    // Emit matching pop before touching state so debug tools see the balanced pair.
    cmd.pop_debug_group();

    if (handle.index >= static_cast<std::uint32_t>(impl_->in_flight.size()))
        return;

    InFlight& slot = impl_->in_flight[handle.index];
    if (!slot.active)
        return;

    const bool real_path = slot.begin_slot != ~0U;

    PendingSample entry;
    entry.sample.name           = std::move(slot.name);
    entry.sample.gpu_start_tick = slot.start_tick;
    entry.begin_slot            = slot.begin_slot;
    entry.end_slot              = slot.end_slot;
    // duration_ms_computed + (real path) gpu_*_tick populated by resolve().

    if (real_path)
    {
        cmd.write_timestamp(impl_->pool, slot.end_slot);
        entry.sample.gpu_end_tick = 0;  // Filled from the device at resolve().
    }
    else
    {
        entry.sample.gpu_end_tick = impl_->next_tick();
    }
    entry.sample.duration_ms_computed = 0.0;

    slot.active = false;
    impl_->free_list.push_back(handle.index);

    impl_->pending.push_back(std::move(entry));
}

void Recorder::resolve(cd::rhi::IDevice& device)
{
    // Real path: read the device's resolved timestamp slots (in nanoseconds).
    // The caller must already have waited on the producing submit. We read the
    // whole written range once, then index per-marker.
    std::vector<std::uint64_t> ns_slots;
    bool real_ok = false;
    if (impl_->timestamps_active() && impl_->next_timestamp_slot > 0U)
    {
        ns_slots.resize(impl_->next_timestamp_slot);
        const auto r = device.get_query_results(
            impl_->pool, 0U, impl_->next_timestamp_slot, ns_slots);
        real_ok = r.has_value();
    }

    for (auto& entry : impl_->pending)
    {
        GpuMarkerSample& s = entry.sample;

        if (entry.begin_slot != ~0U && real_ok &&
            entry.end_slot < ns_slots.size())
        {
            // Real timestamps are already in nanoseconds.
            s.gpu_start_tick = ns_slots[entry.begin_slot];
            s.gpu_end_tick   = ns_slots[entry.end_slot];
            const std::uint64_t delta_ns = s.gpu_end_tick - s.gpu_start_tick;
            s.duration_ms_computed =
                static_cast<double>(delta_ns) / kNanosecondsPerMillisecond;
        }
        else
        {
            // Stub path: monotonic ticks at a 1 GHz clock.
            const std::uint64_t delta = s.gpu_end_tick - s.gpu_start_tick;
            s.duration_ms_computed =
                (static_cast<double>(delta) / kStubTickFrequencyHz) * 1000.0;
        }

        impl_->resolved.push_back(std::move(s));
    }
    impl_->pending.clear();

    // The timestamp slots have been consumed; rewind the per-frame cursor so
    // the next frame reuses the pool from the start (the consumer is expected
    // to reset_query_pool before re-recording).
    impl_->next_timestamp_slot = 0;
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
    impl_->next_timestamp_slot = 0;
    // Note: the QueryPool (if any) is retained across clear() — clear() resets
    // frame data, not the device-owned pool created by prepare(). The pool is
    // released in the destructor or re-targeted by a later prepare().
}

}  // namespace cd::profile::gpu_marker
