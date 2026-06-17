// =============================================================================
// CHROMODYNAMIC — cd/profile/gpu_marker/GpuMarker.hpp
//
// Phase 606 — GPU marker recorder (cpu/gpu classification companion to
//             cd::profile::cpu_marker_overlay, Phase 587).
// Phase 739 — stub-wire RHI-contract-gap doc pass (FINALE-2 W3B B9).
//             write_timestamp / QueryPool / gpu_tick_frequency are NOT in
//             the cd::rhi surface yet; this header pins down the exact
//             contract additions required to lift the stub to real timing.
// BAND5-foundation (2026-06-16) — the A-QUERY subsystem shipped (phase1218):
//             IDevice::create_query_pool(QueryType::kTimestamp),
//             ICommandBuffer::write_timestamp, IDevice::get_query_results
//             (resolves timestamp slots to NANOSECONDS — the backend applies
//             the device timestamp period for us). Recorder is now wired to a
//             real RHI timestamp QueryPool: call prepare(device, max_markers)
//             before recording on a device whose features().timestamp_queries
//             is true, and resolve(device) reads real ns via get_query_results.
//             Devices WITHOUT timestamp support (Null, headless, mobile GLES2)
//             stay on the deterministic 1 GHz monotonic-counter path with no
//             API change — prepare() is a no-op there and resolve() falls back.
//             See ADR-20260616-band5-foundation-scope.md §profile_gpu_marker.
//
// Provides:
//   GpuMarkerSample — one resolved GPU marker event (name, gpu_start_tick,
//                     gpu_end_tick, duration_ms_computed).
//   MarkerHandle    — opaque index returned by Recorder::begin_marker().
//   Recorder        — accumulates GPU markers per command buffer. On a device
//                     with timestamp_queries, prepare() owns a kTimestamp
//                     QueryPool (2 slots per marker), begin/end_marker emit
//                     write_timestamp, and resolve() reads real nanoseconds via
//                     IDevice::get_query_results. Without timestamp support a
//                     1 GHz monotonic counter keeps the API exercised + tested.
//   Scope           — RAII wrapper around Recorder::begin_marker / end_marker.
//
// Design notes:
//   * Recorder is NOT thread-safe — it is designed to be used from a single
//     recording thread (the command-buffer recording pattern). Multiple
//     Recorders may be used concurrently on different threads, each owning
//     their own ICommandBuffer.
//   * resolve() converts raw tick deltas to duration_ms_computed using
//     device gpu_tick_frequency. Call it once after the GPU work completes
//     (after wait_idle / fence signal) before reading samples().
//
// -----------------------------------------------------------------------------
// RHI_CONTRACT_GAP — CLOSED (BAND5-foundation, was phase739 B9 BLOCKED)
// -----------------------------------------------------------------------------
// The five cd::rhi additions this stub was waiting for ALL shipped in the
// A-QUERY subsystem (phase1218). They map 1:1 to the wired calls below:
//
//   IDevice::create_query_pool(QueryPoolDesc{QueryType::kTimestamp, count})
//       Vulkan vkCreateQueryPool / D3D12 CreateQueryHeap / Metal counter buffer.
//   ICommandBuffer::reset_query_pool(pool, first, count)
//       Vulkan vkCmdResetQueryPool (required); D3D12/Metal no-op.
//   ICommandBuffer::write_timestamp(pool, index)
//       Vulkan vkCmdWriteTimestamp2 / D3D12 EndQuery(TIMESTAMP) / Metal sample.
//   IDevice::get_query_results(pool, first, count, std::span<uint64> out)
//       Returns kTimestamp slots already in NANOSECONDS — the backend applies
//       the device timestamp period, so the lib needs NO gpu_tick_frequency().
//   DeviceFeatures::timestamp_queries
//       Recorder::prepare() gates pool creation on this flag; resolve() reads
//       real ns when a pool exists and otherwise falls back to the stub clock.
//
// Recorder owns the QueryPool end-to-end (created in prepare, destroyed in the
// destructor / clear), so the integration needs NO renderer-side plumbing: the
// consumer only has to (1) call prepare(device, max_markers) once, (2) record
// begin/end_marker on its command buffer as before, and (3) call resolve(device)
// after the producing submit has completed (fence/wait_idle) — the same lifetime
// the stub already required. Verified deterministically by a fake timestamp
// IDevice in the gtest; on-GPU (RTX 3080 / Vulkan) verification runs through the
// renderer's existing query-pool device tests, not this foundation binary
// (that would force a foundation→backend link). See
// ADR-20260616-band5-foundation-scope.md §profile_gpu_marker.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

// Forward-declare RHI types so this header stays lightweight and does not
// pull in the full RHI header tree into every consumer.
namespace cd::rhi
{
class ICommandBuffer;
class IDevice;
}  // namespace cd::rhi

namespace cd::profile::gpu_marker
{

// ---------------------------------------------------------------------------
// GpuMarkerSample — one resolved GPU marker event
// ---------------------------------------------------------------------------

/// A single completed and resolved GPU marker event.
/// After Recorder::resolve(), duration_ms_computed is valid.
/// gpu_start_tick and gpu_end_tick are raw device-clock ticks (or stub
/// monotonic counter values until write_timestamp lands).
struct GpuMarkerSample
{
    std::string name;                  ///< Region label (copy; safe after clear).
    std::uint64_t gpu_start_tick { 0 };  ///< Raw GPU tick at marker begin.
    std::uint64_t gpu_end_tick   { 0 };  ///< Raw GPU tick at marker end.
    double duration_ms_computed  { 0.0 }; ///< Computed from ticks + frequency.
};

// ---------------------------------------------------------------------------
// MarkerHandle — opaque reference to an in-flight begin_marker()
// ---------------------------------------------------------------------------

/// Returned by Recorder::begin_marker(); consumed by Recorder::end_marker().
/// The value is an index into the Recorder's internal in-flight table.
struct MarkerHandle
{
    std::uint32_t index { ~0U };  ///< ~0 = invalid / not begun.

    [[nodiscard]] bool valid() const noexcept { return index != ~0U; }
};

// ---------------------------------------------------------------------------
// Recorder — per-command-buffer GPU marker accumulator
// ---------------------------------------------------------------------------

class Recorder
{
public:
    Recorder();
    ~Recorder();

    Recorder(const Recorder&)            = delete;
    Recorder& operator=(const Recorder&) = delete;
    Recorder(Recorder&&)                 = delete;
    Recorder& operator=(Recorder&&)      = delete;

    /// Enable the real RHI timestamp path. On a device whose
    /// features().timestamp_queries is true this allocates a kTimestamp
    /// QueryPool with `2 * max_markers` slots (one begin + one end per marker);
    /// begin/end_marker then emit write_timestamp into it and resolve() reads
    /// real nanoseconds via IDevice::get_query_results. On a device WITHOUT
    /// timestamp support (Null, headless) this is a no-op and the recorder
    /// stays on the deterministic 1 GHz monotonic-counter stub — no API change.
    ///
    /// Idempotent per device: a second prepare() on the same already-created
    /// pool is a no-op; preparing with a larger max_markers grows the pool.
    /// Returns true when a real timestamp pool is active afterward.
    bool prepare(cd::rhi::IDevice& device, std::uint32_t max_markers);

    /// Open a named GPU region on `cmd`. Returns an opaque handle that MUST
    /// be passed to end_marker(). The handle is invalidated after end_marker().
    ///
    /// Emits a push_debug_group on cmd for capture-tool visibility. When a
    /// timestamp pool is active (see prepare) also emits write_timestamp at the
    /// marker's begin slot; otherwise records a monotonic counter tick.
    [[nodiscard]] MarkerHandle begin_marker(cd::rhi::ICommandBuffer& cmd,
                                            std::string_view         name);

    /// Close the GPU region identified by handle and record its end tick.
    /// Calling end_marker() with an invalid handle is a no-op.
    ///
    /// Emits pop_debug_group on cmd. When a timestamp pool is active also emits
    /// write_timestamp at the marker's end slot.
    void end_marker(cd::rhi::ICommandBuffer& cmd, MarkerHandle handle);

    /// Resolve durations into duration_ms_computed. Call once per frame after
    /// the producing GPU submit has completed (fence/wait_idle).
    ///
    /// Real path (timestamp pool active): reads the slot values via
    /// IDevice::get_query_results — they are already in NANOSECONDS — and sets
    /// duration_ms_computed = (end_ns - start_ns) * 1e-6. Stub path: a 1 GHz
    /// monotonic counter gives duration_ms_computed = (end - start) * 1e-6.
    void resolve(cd::rhi::IDevice& device);

    /// View of all resolved samples. Valid after resolve(); invalidated by clear().
    [[nodiscard]] std::span<const GpuMarkerSample> samples() const noexcept;

    /// Discard all completed and in-flight samples. Reset the counter.
    void clear() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ---------------------------------------------------------------------------
// Scope — RAII wrapper for Recorder::begin_marker / end_marker
// ---------------------------------------------------------------------------

/// Construct at the top of a named GPU region; the destructor calls end_marker().
class Scope
{
public:
    Scope(Recorder& recorder, cd::rhi::ICommandBuffer& cmd, std::string_view name)
        : recorder_(recorder)
        , cmd_(cmd)
        , handle_(recorder.begin_marker(cmd, name))
    {
    }

    ~Scope()
    {
        if (handle_.valid())
            recorder_.end_marker(cmd_, handle_);
    }

    Scope(const Scope&)            = delete;
    Scope& operator=(const Scope&) = delete;
    Scope(Scope&&)                 = delete;
    Scope& operator=(Scope&&)      = delete;

private:
    Recorder&                recorder_;
    cd::rhi::ICommandBuffer& cmd_;
    MarkerHandle             handle_;
};

}  // namespace cd::profile::gpu_marker
