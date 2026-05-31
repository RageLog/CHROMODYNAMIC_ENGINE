// =============================================================================
// CHROMODYNAMIC — cd/profile/gpu_marker/GpuMarker.hpp
//
// Phase 606 — GPU marker recorder (cpu/gpu classification companion to
//             cd::profile::cpu_marker_overlay, Phase 587).
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
//   * TODO: real GPU timestamps when ICommandBuffer::write_timestamp lands.
//     Replace stub monotonic counter with actual pipeline-stage timestamp
//     writes and query pool readback (Vulkan: vkCmdWriteTimestamp2 +
//     vkGetQueryPoolResults; D3D12: ID3D12CommandList::EndQuery +
//     ResolveQueryData; Metal: sampleCountersInBuffer).
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
