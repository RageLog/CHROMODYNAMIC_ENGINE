// =============================================================================
// CHROMODYNAMIC — engine/asset/audio_streamer/tests/test_audio_ring_buffer.cpp
// Sprint-3 — AudioRingBuffer + AudioFormatDescriptor unit tests.
//
// Coverage:
//   R1  AudioFormatDescriptor: bytes_per_frame mono-16 correct
//   R2  AudioFormatDescriptor: bytes_per_frame stereo-16 correct
//   R3  AudioFormatDescriptor: bytes_per_frame stereo-32 correct
//   R4  AudioFormatDescriptor: is_valid true for well-formed descriptor
//   R5  AudioFormatDescriptor: is_valid false when channels == 0
//   R6  AudioFormatDescriptor: is_valid false when sample_rate == 0
//   R7  AudioFormatDescriptor: is_valid false when bits_per_sample == 0
//   R8  AudioFormatDescriptor: from_decoded copies channels/rate/bits correctly
//   R9  Ring: initial state is empty / not full
//   R10 Ring: write + read exact-fill round-trip
//   R11 Ring: write one chunk, read back same bytes
//   R12 Ring: full fill — is_full == true, frames_free == 0
//   R13 Ring: drain to empty — is_empty == true, frames_available == 0
//   R14 Ring: wraparound write (write past end of storage, read wraps)
//   R15 Ring: partial write when nearly full — clamps to free space
//   R16 Ring: partial read when insufficient data — underrun triggered
//   R17 Ring: underrun_count starts at 0, increments on short read
//   R18 Ring: underrun_count does NOT increment on zero-frame request
//   R19 Ring: read from empty buffer returns empty span + underrun
//   R20 Ring: loop point — read wraps cursor at loop_end_frame
//   R21 Ring: loop point — read from loop_end_frame immediately wraps
//   R22 Ring: clear_loop_points disables looping
//   R23 Ring: set_loop_points invalid (end <= start) treated as clear
//   R24 Ring: reset clears cursors and underrun_count; loop points preserved
//   R25 Ring: write returns 0 when ring is full
//   R26 Ring: write respects frame alignment (sub-frame trailing bytes ignored)
//   R27 Ring: multiple partial writes fill ring correctly
//   R28 Ring: read_cursor_frames / write_cursor_frames advance correctly
//   R29 Ring: frames_available = write - read before and after writes
//   R30 Ring: capacity_frames reflects constructor argument
//   R31 Ring: capacity_frames minimum clamp to 2
//   R32 Ring: loop read that spans wrap boundary stops at loop_end_frame
//   R33 Ring: write_chunk returns number of bytes actually written
//   R34 Ring: write_chunk with zero-length span returns 0
//   R35 Ring: read_frames with 0 request returns empty span, no underrun
//   R36 Ring: successive loop wraps (read loops 3× through a 4-frame clip)
//   R37 Ring: write after read (producer refills after consumer drains)
//   R38 Ring: is_looping reflects loop_end_frame state
//   R39 Ring: loop_start_frame / loop_end_frame accessors reflect set values
// =============================================================================

#include <cd/asset/audio_streamer/AudioStreamer.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace
{

using cd::asset::audio_streamer::AudioFormatDescriptor;
using cd::asset::audio_streamer::AudioRingBuffer;
using cd::asset::audio_streamer::DecodedAudio;

// Convenience: build a simple stereo-16 format.
[[nodiscard]] AudioFormatDescriptor stereo16() noexcept
{
    return AudioFormatDescriptor{ 2U, 48000U, 16U };
}

// Convenience: build a mono-16 format.
[[nodiscard]] AudioFormatDescriptor mono16() noexcept
{
    return AudioFormatDescriptor{ 1U, 44100U, 16U };
}

// Build a byte vector of `frames` frames for the given format.
// Each frame is filled with a repeating counter byte for easy verification.
[[nodiscard]] std::vector<std::uint8_t>
make_pcm(const AudioFormatDescriptor& fmt, const std::uint64_t frames)
{
    const std::size_t total = static_cast<std::size_t>(frames) *
                              static_cast<std::size_t>(fmt.bytes_per_frame());
    std::vector<std::uint8_t> out(total);
    for (std::size_t i = 0U; i < total; ++i)
    {
        out[i] = static_cast<std::uint8_t>(i & 0xFFU);
    }
    return out;
}

}  // namespace

// ---- R1-R8: AudioFormatDescriptor -------------------------------------------

TEST(AudioFormatDescriptor, BytesPerFrameMono16)
{
    const AudioFormatDescriptor d { 1U, 44100U, 16U };
    EXPECT_EQ(d.bytes_per_frame(), 2U);  // 1 ch × 2 bytes
}

TEST(AudioFormatDescriptor, BytesPerFrameStereo16)
{
    EXPECT_EQ(stereo16().bytes_per_frame(), 4U);  // 2 ch × 2 bytes
}

TEST(AudioFormatDescriptor, BytesPerFrameStereo32)
{
    const AudioFormatDescriptor d { 2U, 48000U, 32U };
    EXPECT_EQ(d.bytes_per_frame(), 8U);  // 2 ch × 4 bytes
}

TEST(AudioFormatDescriptor, IsValidWellFormed)
{
    EXPECT_TRUE(stereo16().is_valid());
}

TEST(AudioFormatDescriptor, IsValidFalseWhenChannelsZero)
{
    const AudioFormatDescriptor d { 0U, 48000U, 16U };
    EXPECT_FALSE(d.is_valid());
}

TEST(AudioFormatDescriptor, IsValidFalseWhenSampleRateZero)
{
    const AudioFormatDescriptor d { 2U, 0U, 16U };
    EXPECT_FALSE(d.is_valid());
}

TEST(AudioFormatDescriptor, IsValidFalseWhenBitsZero)
{
    const AudioFormatDescriptor d { 2U, 48000U, 0U };
    EXPECT_FALSE(d.is_valid());
}

TEST(AudioFormatDescriptor, FromDecodedCopiesFields)
{
    DecodedAudio dec;
    dec.channels        = 2U;
    dec.sample_rate     = 48000U;
    dec.bits_per_sample = 16U;
    dec.frame_count     = 100U;

    const AudioFormatDescriptor d = AudioFormatDescriptor::from_decoded(dec);
    EXPECT_EQ(d.channels,        2U);
    EXPECT_EQ(d.sample_rate,     48000U);
    EXPECT_EQ(d.bits_per_sample, 16U);
    EXPECT_TRUE(d.is_valid());
}

// ---- R9-R13: Initial state + basic fill/drain -------------------------------

TEST(AudioRingBuffer, InitialStateIsEmpty)
{
    AudioRingBuffer ring { stereo16(), 64U };
    EXPECT_TRUE(ring.is_empty());
    EXPECT_FALSE(ring.is_full());
    EXPECT_EQ(ring.frames_available(), 0U);
    EXPECT_EQ(ring.frames_free(),      64U);
    EXPECT_EQ(ring.underrun_count(),   0U);
    EXPECT_EQ(ring.read_cursor_frames(),  0U);
    EXPECT_EQ(ring.write_cursor_frames(), 0U);
}

TEST(AudioRingBuffer, WriteReadRoundTrip)
{
    // Write 8 frames, read back 8 frames — bytes must match exactly.
    const AudioFormatDescriptor fmt = stereo16();  // bpf = 4
    AudioRingBuffer ring { fmt, 16U };

    const auto pcm = make_pcm(fmt, 8U);
    const std::size_t written = ring.write_chunk(pcm);
    EXPECT_EQ(written, 8U * 4U);
    EXPECT_EQ(ring.frames_available(), 8U);

    const auto out = ring.read_frames(8U);
    ASSERT_EQ(out.size(), 8U * 4U);
    EXPECT_EQ(std::memcmp(out.data(), pcm.data(), out.size()), 0);

    EXPECT_TRUE(ring.is_empty());
    EXPECT_EQ(ring.underrun_count(), 0U);
}

TEST(AudioRingBuffer, WriteOneChunkReadBackSameBytes)
{
    AudioRingBuffer ring { mono16(), 32U };  // bpf = 2
    const std::vector<std::uint8_t> chunk { 0xAAU, 0xBBU, 0xCCU, 0xDDU };  // 2 frames
    static_cast<void>(ring.write_chunk(chunk));
    const auto out = ring.read_frames(2U);
    ASSERT_EQ(out.size(), 4U);
    EXPECT_EQ(out[0], 0xAAU);
    EXPECT_EQ(out[1], 0xBBU);
    EXPECT_EQ(out[2], 0xCCU);
    EXPECT_EQ(out[3], 0xDDU);
}

TEST(AudioRingBuffer, FullFillReturnsIsFull)
{
    const AudioFormatDescriptor fmt = stereo16();
    AudioRingBuffer ring { fmt, 8U };

    const auto pcm = make_pcm(fmt, 8U);
    const std::size_t written = ring.write_chunk(pcm);
    EXPECT_EQ(written, 8U * 4U);
    EXPECT_TRUE(ring.is_full());
    EXPECT_EQ(ring.frames_free(), 0U);
    EXPECT_EQ(ring.frames_available(), 8U);
}

TEST(AudioRingBuffer, DrainToEmpty)
{
    const AudioFormatDescriptor fmt = mono16();
    AudioRingBuffer ring { fmt, 16U };

    static_cast<void>(ring.write_chunk(make_pcm(fmt, 10U)));
    EXPECT_EQ(ring.frames_available(), 10U);

    static_cast<void>(ring.read_frames(10U));
    EXPECT_TRUE(ring.is_empty());
    EXPECT_EQ(ring.frames_available(), 0U);
    EXPECT_EQ(ring.underrun_count(), 0U);
}

// ---- R14: Wraparound write --------------------------------------------------

TEST(AudioRingBuffer, WraparoundWrite)
{
    // Write 6 frames into a 8-frame ring, read 6, then write 6 more.
    // The second write must wrap around the ring boundary.
    const AudioFormatDescriptor fmt = mono16();  // bpf = 2
    AudioRingBuffer ring { fmt, 8U };

    const auto first_chunk = make_pcm(fmt, 6U);
    static_cast<void>(ring.write_chunk(first_chunk));
    static_cast<void>(ring.read_frames(6U));  // drain first 6 frames

    // Now write_pos is at frame 6 in the ring (mod 8). Writing 6 more wraps.
    std::vector<std::uint8_t> second_chunk(12U, std::uint8_t { 0x55U });  // 6 frames
    const std::size_t written = ring.write_chunk(second_chunk);
    EXPECT_EQ(written, 12U);
    EXPECT_EQ(ring.frames_available(), 6U);

    const auto out = ring.read_frames(6U);
    ASSERT_EQ(out.size(), 12U);
    for (std::size_t i = 0U; i < out.size(); ++i)
    {
        EXPECT_EQ(out[i], 0x55U) << "mismatch at byte " << i;
    }
}

// ---- R15: Partial write when nearly full ------------------------------------

TEST(AudioRingBuffer, PartialWriteWhenNearlyFull)
{
    const AudioFormatDescriptor fmt = mono16();
    AudioRingBuffer ring { fmt, 4U };

    // Fill 3 frames, leaving 1 free.
    static_cast<void>(ring.write_chunk(make_pcm(fmt, 3U)));
    EXPECT_EQ(ring.frames_free(), 1U);

    // Try to write 3 more — only 1 should succeed.
    const std::size_t written = ring.write_chunk(make_pcm(fmt, 3U));
    EXPECT_EQ(written, 1U * static_cast<std::size_t>(fmt.bytes_per_frame()));
    EXPECT_TRUE(ring.is_full());
}

// ---- R16-R19: Underrun detection -------------------------------------------

TEST(AudioRingBuffer, PartialReadTriggersUnderrun)
{
    const AudioFormatDescriptor fmt = mono16();
    AudioRingBuffer ring { fmt, 16U };

    static_cast<void>(ring.write_chunk(make_pcm(fmt, 4U)));

    // Ask for 10 frames — only 4 available → underrun.
    const auto out = ring.read_frames(10U);
    EXPECT_EQ(out.size(), 4U * static_cast<std::size_t>(fmt.bytes_per_frame()));
    EXPECT_EQ(ring.underrun_count(), 1U);
}

TEST(AudioRingBuffer, UnderrunCountStartsAtZero)
{
    AudioRingBuffer ring { stereo16(), 32U };
    EXPECT_EQ(ring.underrun_count(), 0U);
}

TEST(AudioRingBuffer, UnderrunNoIncrementOnZeroRequest)
{
    AudioRingBuffer ring { mono16(), 16U };
    // Ask for 0 frames — must NOT count as underrun.
    const auto out = ring.read_frames(0U);
    EXPECT_TRUE(out.empty());
    EXPECT_EQ(ring.underrun_count(), 0U);
}

TEST(AudioRingBuffer, ReadFromEmptyBufferUnderruns)
{
    AudioRingBuffer ring { stereo16(), 8U };
    ASSERT_TRUE(ring.is_empty());
    const auto out = ring.read_frames(4U);
    EXPECT_TRUE(out.empty());
    EXPECT_EQ(ring.underrun_count(), 1U);
}

// ---- R20-R23: Loop points ---------------------------------------------------

TEST(AudioRingBuffer, LoopPointWrapsReadCursor)
{
    // Ring with 16 frames capacity. Load 8 frames, loop between [0, 4).
    // Reading 4 frames should wrap cursor back to 0.
    const AudioFormatDescriptor fmt = mono16();
    AudioRingBuffer ring { fmt, 16U };

    static_cast<void>(ring.write_chunk(make_pcm(fmt, 8U)));
    ring.set_loop_points(0U, 4U);

    // Consume frames 0..3 — cursor now at 4, which is loop_end → wraps to 0.
    static_cast<void>(ring.read_frames(4U));
    EXPECT_EQ(ring.read_cursor_frames(), 0U);
    EXPECT_EQ(ring.underrun_count(), 0U);
}

TEST(AudioRingBuffer, LoopPointAtLoopEndWrapsImmediately)
{
    // If read_cursor_ is already at loop_end_frame when read_frames() is
    // called, it wraps to loop_start before issuing data.
    const AudioFormatDescriptor fmt = mono16();
    AudioRingBuffer ring { fmt, 16U };

    static_cast<void>(ring.write_chunk(make_pcm(fmt, 8U)));
    ring.set_loop_points(2U, 6U);

    // Advance read to exactly loop_end by reading 6 frames.
    static_cast<void>(ring.read_frames(6U));
    // Now read_cursor_ == 6 == loop_end → next read wraps to loop_start.
    // Write more data to ensure there's something to read from frame 2.
    // (The buffer already has frames 6..7 remaining from the original fill.)
    EXPECT_EQ(ring.read_cursor_frames(), 2U);
}

TEST(AudioRingBuffer, ClearLoopPointsDisablesLoop)
{
    const AudioFormatDescriptor fmt = mono16();
    AudioRingBuffer ring { fmt, 16U };

    ring.set_loop_points(0U, 4U);
    EXPECT_TRUE(ring.is_looping());

    ring.clear_loop_points();
    EXPECT_FALSE(ring.is_looping());
    EXPECT_EQ(ring.loop_end_frame(), 0U);
    EXPECT_EQ(ring.loop_start_frame(), 0U);
}

TEST(AudioRingBuffer, SetLoopPointsInvalidRangeTreatedAsClear)
{
    AudioRingBuffer ring { mono16(), 16U };

    // end <= start: must clear rather than set bad loop.
    ring.set_loop_points(8U, 4U);
    EXPECT_FALSE(ring.is_looping());

    // end == start: also invalid.
    ring.set_loop_points(4U, 4U);
    EXPECT_FALSE(ring.is_looping());
}

// ---- R24: Reset -------------------------------------------------------------

TEST(AudioRingBuffer, ResetClearsCursorsAndUnderruns)
{
    const AudioFormatDescriptor fmt = mono16();
    AudioRingBuffer ring { fmt, 16U };

    ring.set_loop_points(0U, 8U);
    static_cast<void>(ring.write_chunk(make_pcm(fmt, 16U)));
    static_cast<void>(ring.read_frames(32U));  // triggers underrun

    EXPECT_GT(ring.underrun_count(), 0U);

    ring.reset();

    // Cursors and underruns cleared.
    EXPECT_EQ(ring.read_cursor_frames(),  0U);
    EXPECT_EQ(ring.write_cursor_frames(), 0U);
    EXPECT_EQ(ring.underrun_count(),      0U);
    EXPECT_TRUE(ring.is_empty());

    // Loop points preserved.
    EXPECT_TRUE(ring.is_looping());
    EXPECT_EQ(ring.loop_start_frame(), 0U);
    EXPECT_EQ(ring.loop_end_frame(),   8U);
}

// ---- R25-R26: Write boundary guards ----------------------------------------

TEST(AudioRingBuffer, WriteReturnsZeroWhenFull)
{
    const AudioFormatDescriptor fmt = mono16();
    AudioRingBuffer ring { fmt, 4U };

    static_cast<void>(ring.write_chunk(make_pcm(fmt, 4U)));
    ASSERT_TRUE(ring.is_full());

    const std::size_t written = ring.write_chunk(make_pcm(fmt, 2U));
    EXPECT_EQ(written, 0U);
}

TEST(AudioRingBuffer, WriteRespectsFrameAlignment)
{
    // Provide data.size() that is NOT a multiple of bytes_per_frame.
    // The trailing sub-frame bytes must be silently ignored.
    const AudioFormatDescriptor fmt = stereo16();  // bpf = 4
    AudioRingBuffer ring { fmt, 8U };

    // 5 bytes = 1 full frame (4 bytes) + 1 leftover byte.
    const std::vector<std::uint8_t> odd_data(5U, 0xEEU);
    const std::size_t written = ring.write_chunk(odd_data);
    // Only one full frame (4 bytes) should have been accepted.
    EXPECT_EQ(written, 4U);
    EXPECT_EQ(ring.frames_available(), 1U);
}

// ---- R27: Multiple partial writes -------------------------------------------

TEST(AudioRingBuffer, MultiplePartialWritesFillRingCorrectly)
{
    const AudioFormatDescriptor fmt = mono16();  // bpf = 2
    AudioRingBuffer ring { fmt, 8U };

    // Write in three chunks: 3 + 3 + 3 frames. Only 8 fit → third is partial.
    static_cast<void>(ring.write_chunk(make_pcm(fmt, 3U)));
    static_cast<void>(ring.write_chunk(make_pcm(fmt, 3U)));
    static_cast<void>(ring.write_chunk(make_pcm(fmt, 3U)));

    EXPECT_TRUE(ring.is_full());
    EXPECT_EQ(ring.frames_available(), 8U);
}

// ---- R28-R29: Cursor arithmetic ---------------------------------------------

TEST(AudioRingBuffer, CursorAdvancesCorrectly)
{
    const AudioFormatDescriptor fmt = mono16();
    AudioRingBuffer ring { fmt, 32U };

    static_cast<void>(ring.write_chunk(make_pcm(fmt, 10U)));
    EXPECT_EQ(ring.write_cursor_frames(), 10U);
    EXPECT_EQ(ring.read_cursor_frames(),   0U);

    static_cast<void>(ring.read_frames(6U));
    EXPECT_EQ(ring.read_cursor_frames(),  6U);
    EXPECT_EQ(ring.write_cursor_frames(), 10U);
}

TEST(AudioRingBuffer, FramesAvailableEqualsWriteMinusRead)
{
    const AudioFormatDescriptor fmt = stereo16();
    AudioRingBuffer ring { fmt, 64U };

    static_cast<void>(ring.write_chunk(make_pcm(fmt, 20U)));
    EXPECT_EQ(ring.frames_available(), 20U);

    static_cast<void>(ring.read_frames(7U));
    EXPECT_EQ(ring.frames_available(), 13U);
}

// ---- R30-R31: Capacity ------------------------------------------------------

TEST(AudioRingBuffer, CapacityFramesMatchesConstructorArg)
{
    AudioRingBuffer ring { mono16(), 128U };
    EXPECT_EQ(ring.capacity_frames(), 128U);
}

TEST(AudioRingBuffer, CapacityFramesMinimumClampToTwo)
{
    // Passing capacity 0 or 1 must be clamped to 2.
    AudioRingBuffer ring0 { mono16(), 0U };
    EXPECT_GE(ring0.capacity_frames(), 2U);

    AudioRingBuffer ring1 { mono16(), 1U };
    EXPECT_GE(ring1.capacity_frames(), 2U);
}

// ---- R32: Loop boundary truncates read --------------------------------------

TEST(AudioRingBuffer, LoopReadStopsAtLoopEndFrame)
{
    // Loop region [0, 4). Fill 8 frames. Read 6 — must stop at frame 4.
    const AudioFormatDescriptor fmt = mono16();
    AudioRingBuffer ring { fmt, 16U };

    static_cast<void>(ring.write_chunk(make_pcm(fmt, 8U)));
    ring.set_loop_points(0U, 4U);

    // Reading 6 should only deliver 4 (loop end boundary).
    const auto out = ring.read_frames(6U);
    EXPECT_EQ(out.size(), 4U * static_cast<std::size_t>(fmt.bytes_per_frame()));
    // After the capped read underrun fires for the 2-frame shortfall.
    EXPECT_EQ(ring.underrun_count(), 1U);
    // Cursor wraps to loop start.
    EXPECT_EQ(ring.read_cursor_frames(), 0U);
}

// ---- R33-R35: write_chunk / read_frames zero / size guards ------------------

TEST(AudioRingBuffer, WriteChunkReturnsBytesWritten)
{
    const AudioFormatDescriptor fmt = mono16();
    AudioRingBuffer ring { fmt, 8U };

    const auto pcm = make_pcm(fmt, 5U);
    const std::size_t written = ring.write_chunk(pcm);
    EXPECT_EQ(written, 5U * static_cast<std::size_t>(fmt.bytes_per_frame()));
}

TEST(AudioRingBuffer, WriteChunkZeroLengthReturnsZero)
{
    AudioRingBuffer ring { stereo16(), 8U };
    const std::size_t written = ring.write_chunk({});
    EXPECT_EQ(written, 0U);
    EXPECT_TRUE(ring.is_empty());
}

TEST(AudioRingBuffer, ReadFramesZeroRequestEmpty)
{
    const AudioFormatDescriptor fmt = mono16();
    AudioRingBuffer ring { fmt, 8U };
    static_cast<void>(ring.write_chunk(make_pcm(fmt, 4U)));

    const auto out = ring.read_frames(0U);
    EXPECT_TRUE(out.empty());
    EXPECT_EQ(ring.underrun_count(), 0U);
    // Cursor did not advance.
    EXPECT_EQ(ring.read_cursor_frames(), 0U);
}

// ---- R36: Successive loop wraps ---------------------------------------------

TEST(AudioRingBuffer, SuccessiveLoopWraps)
{
    // 4-frame clip, loop [0, 4). Write once, read 3× through the loop.
    const AudioFormatDescriptor fmt = mono16();
    AudioRingBuffer ring { fmt, 8U };

    static_cast<void>(ring.write_chunk(make_pcm(fmt, 4U)));
    ring.set_loop_points(0U, 4U);

    // Each read of 4 frames should wrap the cursor back to 0.
    // We re-write the same 4 frames after each drain to simulate a streaming
    // producer (otherwise the ring is empty and we'd underrun).
    for (std::uint32_t i = 0U; i < 3U; ++i)
    {
        static_cast<void>(ring.write_chunk(make_pcm(fmt, 4U)));
        const auto out = ring.read_frames(4U);
        EXPECT_EQ(out.size(), 4U * static_cast<std::size_t>(fmt.bytes_per_frame())) << "loop " << i;
        EXPECT_EQ(ring.read_cursor_frames(), 0U) << "loop " << i;
    }
    EXPECT_EQ(ring.underrun_count(), 0U);
}

// ---- R37: Write after read --------------------------------------------------

TEST(AudioRingBuffer, WriteAfterReadRefillsBuffer)
{
    const AudioFormatDescriptor fmt = mono16();
    AudioRingBuffer ring { fmt, 8U };

    static_cast<void>(ring.write_chunk(make_pcm(fmt, 8U)));
    static_cast<void>(ring.read_frames(8U));
    ASSERT_TRUE(ring.is_empty());

    // Producer refills.
    const std::vector<std::uint8_t> refill(8U * static_cast<std::size_t>(fmt.bytes_per_frame()), 0x7FU);
    static_cast<void>(ring.write_chunk(refill));
    EXPECT_EQ(ring.frames_available(), 8U);

    const auto out = ring.read_frames(8U);
    ASSERT_EQ(out.size(), refill.size());
    for (const auto b : out)
    {
        EXPECT_EQ(b, 0x7FU);
    }
}

// ---- R38-R39: Loop accessors ------------------------------------------------

TEST(AudioRingBuffer, IsLoopingReflectsLoopEndFrame)
{
    AudioRingBuffer ring { mono16(), 16U };
    EXPECT_FALSE(ring.is_looping());

    ring.set_loop_points(0U, 8U);
    EXPECT_TRUE(ring.is_looping());

    ring.clear_loop_points();
    EXPECT_FALSE(ring.is_looping());
}

TEST(AudioRingBuffer, LoopAccessorsReflectSetValues)
{
    AudioRingBuffer ring { mono16(), 32U };
    ring.set_loop_points(4U, 16U);

    EXPECT_EQ(ring.loop_start_frame(), 4U);
    EXPECT_EQ(ring.loop_end_frame(),   16U);
}
