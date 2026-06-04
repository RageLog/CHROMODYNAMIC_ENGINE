// =============================================================================
// CHROMODYNAMIC — cd/profile/gpu_marker/GpuMarker.hpp
//
// Phase 606 — GPU marker recorder (cpu/gpu classification companion to
//             cd::profile::cpu_marker_overlay, Phase 587).
// Phase 739 — stub-wire RHI-contract-gap doc pass (FINALE-2 W3B B9).
//             write_timestamp / QueryPool / gpu_tick_frequency are NOT in
//             the cd::rhi surface yet; this header pins down the exact
//             contract additions required to lift the stub to real timing.
//
// Provides:
//   GpuMarkerSample — one resolved GPU marker event (name, gpu_start_tick,
//                     gpu_end_tick, duration_ms_computed).
//   MarkerHandle    — opaque index returned by Recorder::begin_marker().
//   Recorder        — accumulates GPU markers per command buffer; resolves
//                     raw ticks to milliseconds via IDevice::gpu_tick_frequency
//                     once available. Until ICommandBuffer::write_timestamp()
//                     lands, a stub implementation uses a monotonic counter so
//                     the library compiles and the API surface is exercised.
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
// RHI_CONTRACT_GAP — phase739 (B9 BLOCKED)
// -----------------------------------------------------------------------------
// Lifting Recorder from monotonic-counter stub to real GPU timestamps requires
// the following additions to cd::rhi. Each entry maps 1:1 to the existing
// Vulkan / D3D12 / Metal primitive so backends translate directly.
//
// 1. cd::rhi::QueryPoolDesc + IDevice::create_query_pool / destroy_query_pool
//      struct QueryPoolDesc { QueryType type; std::uint32_t count; };
//      enum class QueryType { kTimestamp, kPipelineStatistics, kOcclusion };
//      Result<QueryPoolHandle> create_query_pool(const QueryPoolDesc&);
//      void                    destroy_query_pool(QueryPoolHandle);
//    Vulkan: vkCreateQueryPool(VK_QUERY_TYPE_TIMESTAMP, count).
//    D3D12 : ID3D12Device::CreateQueryHeap(D3D12_QUERY_HEAP_TYPE_TIMESTAMP).
//    Metal : MTLCounterSampleBufferDescriptor + MTLDevice newCounterSampleBufferWithDescriptor.
//
// 2. ICommandBuffer::reset_query_pool(QueryPoolHandle, first, count)
//    Vulkan: vkCmdResetQueryPool — REQUIRED once per frame before writes.
//    D3D12 : no-op (heap entries are write-only per submit).
//
// 3. ICommandBuffer::write_timestamp(PipelineStage stage,
//                                    QueryPoolHandle pool,
//                                    std::uint32_t index)
//    Vulkan: vkCmdWriteTimestamp2(stage_mask, pool, index).
//    D3D12 : ID3D12GraphicsCommandList::EndQuery(heap,
//               D3D12_QUERY_TYPE_TIMESTAMP, index).
//    Metal : MTLComputeCommandEncoder sampleCountersInBuffer: atSampleIndex:
//               (or render-encoder sampleCountAtEnd/Begin attachments).
//
// 4. IDevice::get_query_pool_results(QueryPoolHandle pool,
//                                    std::uint32_t first, std::uint32_t count,
//                                    std::span<std::uint64_t> dst,
//                                    QueryResultFlags flags)
//    Vulkan: vkGetQueryPoolResults(..., VK_QUERY_RESULT_64_BIT |
//                                       VK_QUERY_RESULT_WAIT_BIT).
//    D3D12 : ResolveQueryData into a readback buffer, then map.
//    Metal : resolveCounters:inRange:destinationBuffer:.
//
// 5. IDevice::gpu_tick_frequency() -> std::uint64_t  (ticks per second)
//    Vulkan: VkPhysicalDeviceLimits::timestampPeriod (ns/tick) inverted.
//    D3D12 : ID3D12CommandQueue::GetTimestampFrequency().
//    Metal : MTLDevice sampleTimestamps:gpuTimestamp:  delta over a known
//               wall-clock window (or MTLCounterSampleBuffer scale).
//
// 6. DeviceFeatures::timestamp_queries is already declared (Descriptors.hpp
//    line 460). Backends must populate it from
//    VkPhysicalDeviceLimits::timestampComputeAndGraphics (Vulkan) or
//    D3D12_FEATURE_DATA_D3D12_OPTIONS3::WriteBufferImmediateSupportFlags
//    + COMMAND_LIST_SUPPORT_FLAG_DIRECT (D3D12). Recorder::resolve() will
//    branch on this flag to keep the stub path live on backends without
//    timestamp support (mobile GLES2 fallback, headless null device).
//
// Once items 1–5 land, GpuMarker.cpp swaps `monotonic_counter` for a
// QueryPoolHandle owned by Impl, allocates two indices per begin/end pair,
// emits cmd.write_timestamp(...) inside begin_marker / end_marker, and
// resolve() calls device.get_query_pool_results into a u64 vector that
// then feeds gpu_start_tick / gpu_end_tick. The duration_ms math (delta
// / freq * 1000.0) is unchanged — only the data source differs.
//
// Tracking: docs/FINALE_PLAN.md FINALE-2 B9; resumes when items 1–5 ship
// (suggested ADR: ADR-YYYYMMDD-rhi-query-pool-timestamps.md).
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

    /// Open a named GPU region on `cmd`. Returns an opaque handle that MUST
    /// be passed to end_marker(). The handle is invalidated after end_marker().
    ///
    /// Stub: emits a push_debug_group on cmd for capture-tool visibility and
    /// records a monotonic counter tick. Real timestamp write comes when
    /// ICommandBuffer::write_timestamp lands.
    [[nodiscard]] MarkerHandle begin_marker(cd::rhi::ICommandBuffer& cmd,
                                            std::string_view         name);

    /// Close the GPU region identified by handle and record its end tick.
    /// Calling end_marker() with an invalid handle is a no-op.
    ///
    /// Stub: emits pop_debug_group on cmd.
    void end_marker(cd::rhi::ICommandBuffer& cmd, MarkerHandle handle);

    /// Resolve raw ticks to duration_ms_computed using the device GPU tick
    /// frequency. Call once per frame after GPU work finishes (fence/wait_idle).
    ///
    /// Stub: uses an internal frequency of 1 GHz (1e9 ticks/s) so that
    /// duration_ms_computed = (end_tick - start_tick) * 1e-6. When
    /// IDevice::gpu_tick_frequency() exists, replace the stub frequency.
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
