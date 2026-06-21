// =============================================================================
// CHROMODYNAMIC — cd/asset/audio_streamer/AudioRingBuffer.cpp
// Sprint-3 — CPU ring-buffer streaming layer.
//
// Single-producer / single-consumer circular PCM byte buffer.
//
// Layout:
//   * storage_ holds capacity_frames_ * bytes_per_frame bytes.
//   * write_cursor_ and read_cursor_ are monotonically increasing frame
//     counts; ring position = cursor % capacity_frames_.
//   * frames_available() = write_cursor_ - read_cursor_ (no overflow risk
//     within the 2^64 range for any practical session length).
//
// Loop handling:
//   * When loop_end_frame_ > 0 and read_cursor_ reaches loop_end_frame_,
//     read_cursor_ is reset to loop_start_frame_.  This is a
//     single-consumer operation so no lock is needed.
//   * Loop coordinates are in the clip's absolute frame timeline. They do
//     NOT affect write_cursor_; the producer is free to keep writing
//     monotonically — the consumer's cursor reset produces the perceived
//     loop.
//
// Underrun:
//   * If the consumer requests more frames than are available, the read
//     returns only the available frames and increments underrun_count_.
//     The caller fills the gap with silence.
//
// Thread safety:
//   * write_chunk() is called only from the producer thread.
//   * read_frames() is called only from the consumer thread.
//   * Shared state between threads (write_cursor_) is read by the consumer
//     as a plain uint64 load.  In the SPSC regime this is safe on all
//     architectures where C++ implementation sizeof(uint64_t) == 8 and the
//     target guarantees naturally aligned 64-bit loads are atomic (x86-64,
//     ARM64, RISC-V RV64).  For a standards-compliant strict guarantee,
//     promote write_cursor_ / read_cursor_ to std::atomic<uint64_t> with
//     acquire/release ordering; the class interface is already designed for
//     that upgrade without API change.
//   * All other methods (reset, set_loop_points, capacity queries) must be
//     called from a single thread with no concurrent producer/consumer.
// =============================================================================

#include <cd/asset/audio_streamer/AudioStreamer.hpp>

#include <algorithm>
#include <cassert>
#include <cstring>

namespace cd::asset::audio_streamer
{

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

AudioRingBuffer::AudioRingBuffer(AudioFormatDescriptor format,
                                 const std::uint64_t   capacity_frames)
    : format_          { format }
    , capacity_frames_ { capacity_frames < 2U ? 2U : capacity_frames }
{
    assert(format_.is_valid() && "AudioRingBuffer: format must be valid (channels/rate/bits non-zero)");
    const std::uint64_t byte_cap = capacity_frames_ * static_cast<std::uint64_t>(format_.bytes_per_frame());
    storage_.assign(static_cast<std::size_t>(byte_cap), std::uint8_t { 0U });
    read_scratch_.reserve(static_cast<std::size_t>(byte_cap));
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void AudioRingBuffer::set_loop_points(const std::uint64_t loop_start_frame,
                                      const std::uint64_t loop_end_frame) noexcept
{
    // Validate: end must be strictly greater than start (or end == 0 = off).
    if (loop_end_frame != 0U && loop_end_frame <= loop_start_frame)
    {
        // Invalid range — treat as "clear" to avoid undefined seek behaviour.
        loop_start_frame_ = 0U;
        loop_end_frame_   = 0U;
        return;
    }
    loop_start_frame_ = loop_start_frame;
    loop_end_frame_   = loop_end_frame;
}

void AudioRingBuffer::clear_loop_points() noexcept
{
    loop_start_frame_ = 0U;
    loop_end_frame_   = 0U;
}

void AudioRingBuffer::reset() noexcept
{
    write_cursor_  = 0U;
    read_cursor_   = 0U;
    underrun_count_ = 0U;
    std::ranges::fill(storage_, std::uint8_t { 0U });
    // Loop points are preserved across reset so callers don't need to re-set
    // them after seeking a clip back to the start.
}

// ---------------------------------------------------------------------------
// Producer — write_chunk
// ---------------------------------------------------------------------------

std::size_t AudioRingBuffer::write_chunk(
    const std::span<const std::uint8_t> data) noexcept
{
    if (data.empty())
    {
        return 0U;
    }

    const std::uint32_t bpf = format_.bytes_per_frame();
    if (bpf == 0U)
    {
        return 0U;
    }

    // Free bytes, aligned down to a frame boundary.
    const std::uint64_t free_frames = frames_free();
    if (free_frames == 0U)
    {
        return 0U;
    }

    // Clamp incoming bytes to full frames that fit.
    const std::uint64_t incoming_frames  = data.size() / static_cast<std::size_t>(bpf);
    const std::uint64_t writable_frames  = (incoming_frames < free_frames) ? incoming_frames : free_frames;
    const std::size_t   writable_bytes   = static_cast<std::size_t>(writable_frames) * static_cast<std::size_t>(bpf);

    if (writable_bytes == 0U)
    {
        return 0U;
    }

    // Ring position of the write head in bytes.
    const std::size_t cap_bytes = storage_.size();
    const std::size_t write_pos = static_cast<std::size_t>(write_cursor_ % capacity_frames_) * static_cast<std::size_t>(bpf);

    // How many bytes until we hit the end of the ring storage?
    const std::size_t to_end = cap_bytes - write_pos;

    if (writable_bytes <= to_end)
    {
        // Contiguous write — no wraparound needed.
        std::memcpy(storage_.data() + write_pos, data.data(), writable_bytes);
    }
    else
    {
        // Wraparound: split into two memcpy calls.
        std::memcpy(storage_.data() + write_pos, data.data(), to_end);
        std::memcpy(storage_.data(), data.data() + to_end, writable_bytes - to_end);
    }

    write_cursor_ += writable_frames;
    return writable_bytes;
}

// ---------------------------------------------------------------------------
// Consumer — read_frames
// ---------------------------------------------------------------------------

std::span<const std::uint8_t> AudioRingBuffer::read_frames(
    const std::uint64_t frame_count) noexcept
{
    if (frame_count == 0U)
    {
        return {};
    }

    const std::uint32_t bpf = format_.bytes_per_frame();
    if (bpf == 0U)
    {
        return {};
    }

    // Apply loop wrap: if read_cursor_ >= loop_end_frame, snap back.
    if (loop_end_frame_ > 0U && read_cursor_ >= loop_end_frame_)
    {
        read_cursor_ = loop_start_frame_;
    }

    // Determine how many frames are actually available from the ring.
    const std::uint64_t available = frames_available();

    // Also cap at the loop boundary if looping is enabled.
    std::uint64_t capped_available = available;
    if (loop_end_frame_ > 0U && read_cursor_ + available > loop_end_frame_)
    {
        capped_available = loop_end_frame_ - read_cursor_;
    }

    const std::uint64_t readable_frames = (frame_count < capped_available) ? frame_count : capped_available;

    if (readable_frames == 0U)
    {
        // Underrun: no data to supply.
        ++underrun_count_;
        return {};
    }

    const std::size_t readable_bytes = static_cast<std::size_t>(readable_frames) * static_cast<std::size_t>(bpf);
    const std::size_t cap_bytes      = storage_.size();

    // Ring position of the read head in bytes.
    const std::size_t read_pos = static_cast<std::size_t>(read_cursor_ % capacity_frames_) * static_cast<std::size_t>(bpf);
    const std::size_t to_end   = cap_bytes - read_pos;

    const std::uint8_t* result_ptr = nullptr;
    if (readable_bytes <= to_end)
    {
        // Contiguous region — return a direct view into storage.
        result_ptr = storage_.data() + read_pos;
    }
    else
    {
        // Wrapped region — copy into scratch buffer for a contiguous view.
        read_scratch_.resize(readable_bytes);
        std::memcpy(read_scratch_.data(), storage_.data() + read_pos, to_end);
        std::memcpy(read_scratch_.data() + to_end, storage_.data(), readable_bytes - to_end);
        result_ptr = read_scratch_.data();
    }

    read_cursor_ += readable_frames;

    // If looping: snap cursor back to loop start if we just consumed up to
    // the loop-end boundary.
    if (loop_end_frame_ > 0U && read_cursor_ >= loop_end_frame_)
    {
        read_cursor_ = loop_start_frame_;
    }

    if (readable_frames < frame_count)
    {
        // Partial read counts as underrun for the unfilled remainder.
        ++underrun_count_;
    }

    return { result_ptr, readable_bytes };
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

const AudioFormatDescriptor& AudioRingBuffer::format() const noexcept
{
    return format_;
}

std::uint64_t AudioRingBuffer::capacity_frames() const noexcept
{
    return capacity_frames_;
}

std::uint64_t AudioRingBuffer::frames_available() const noexcept
{
    return write_cursor_ - read_cursor_;
}

std::uint64_t AudioRingBuffer::frames_free() const noexcept
{
    const std::uint64_t used = frames_available();
    return (used < capacity_frames_) ? (capacity_frames_ - used) : 0U;
}

bool AudioRingBuffer::is_empty() const noexcept
{
    return frames_available() == 0U;
}

bool AudioRingBuffer::is_full() const noexcept
{
    return frames_free() == 0U;
}

std::uint64_t AudioRingBuffer::read_cursor_frames() const noexcept
{
    return read_cursor_;
}

std::uint64_t AudioRingBuffer::write_cursor_frames() const noexcept
{
    return write_cursor_;
}

std::uint64_t AudioRingBuffer::underrun_count() const noexcept
{
    return underrun_count_;
}

std::uint64_t AudioRingBuffer::loop_start_frame() const noexcept
{
    return loop_start_frame_;
}

std::uint64_t AudioRingBuffer::loop_end_frame() const noexcept
{
    return loop_end_frame_;
}

bool AudioRingBuffer::is_looping() const noexcept
{
    return loop_end_frame_ > 0U;
}

}  // namespace cd::asset::audio_streamer
