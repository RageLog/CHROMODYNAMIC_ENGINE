// =============================================================================
// CHROMODYNAMIC — cd/game/save_compression/SaveCompression.cpp
// Phase 661 — Sprint-1 RLE compress/decompress implementation.
//
// Packet format (reproduced here for impl clarity):
//   Each packet = 1 header byte + payload.
//   Header byte layout:
//     bit 7       : 0 = literal run,  1 = repeat run
//     bits [6:0]  : count − 1  (so value 0 means count=1, value 127 = count=128)
//
//   Literal packet  : header (bit7=0, count-1 in [6:0]) + count literal bytes.
//   Repeat packet   : header (bit7=1, count-1 in [6:0]) + 1 repeated byte.
//
// Encoder decision:
//   Scan from the current position to find the longest run of identical bytes
//   (up to 128).  If the run is ≥ 2 bytes, emit a repeat packet.  Otherwise,
//   accumulate literal bytes until we hit a repeat of ≥ 2 or exhaust input,
//   then flush the literal packet (max 128 literals per packet to fit [6:0]).
//
// Decoder:
//   Read header; dispatch on bit 7; copy/fill output; advance; repeat.
// =============================================================================
#include <cd/game/save_compression/SaveCompression.hpp>

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>

namespace cd::game::save_compression
{

namespace
{

// Maximum bytes representable in a single packet (count field is 7 bits → 1..128).
constexpr std::uint8_t kMaxPacketLen = 128U;

// Bit 7 of the header byte distinguishes repeat (1) from literal (0).
constexpr std::uint8_t kRepeatFlag = 0x80U;

// -------------------------------------------------------------------------
// encode_rle_into() — write packets into `out` from `src`.
// Called by compress_rle() after pre-reserving a worst-case buffer.
// -------------------------------------------------------------------------
void encode_rle_into(std::span<const std::uint8_t> src,
                     std::vector<std::uint8_t>&    out)
{
    const std::uint8_t* const begin = src.data();
    const std::uint8_t* const end   = begin + src.size();
    const std::uint8_t*       pos   = begin;

    // Temporary buffer for a pending literal packet.
    // Maximum 128 bytes; we flush when full or when a repeat run is found.
    std::uint8_t literal_buf[kMaxPacketLen];
    std::uint8_t literal_count = 0U;

    auto flush_literals = [&]()
    {
        if (literal_count == 0U) return;
        // Header: bit7=0, bits[6:0] = count-1.
        out.push_back(static_cast<std::uint8_t>(literal_count - 1U));
        out.insert(out.end(), literal_buf, literal_buf + literal_count);
        literal_count = 0U;
    };

    while (pos < end)
    {
        // Count run length of identical bytes starting at pos.
        const std::uint8_t run_byte = *pos;
        const std::uint8_t* run_end = pos + 1;
        while (run_end < end &&
               *run_end == run_byte &&
               static_cast<std::uint8_t>(run_end - pos) < kMaxPacketLen)
        {
            ++run_end;
        }
        const auto run_len = static_cast<std::uint8_t>(run_end - pos);

        if (run_len >= 2U)
        {
            // Emit pending literals first (they precede this run).
            flush_literals();
            // Emit repeat packet: header bit7=1, count-1 in [6:0].
            out.push_back(static_cast<std::uint8_t>(kRepeatFlag | (run_len - 1U)));
            out.push_back(run_byte);
            pos = run_end;
        }
        else
        {
            // Single non-repeating byte: accumulate into literal buffer.
            literal_buf[literal_count++] = run_byte;
            ++pos;
            if (literal_count == kMaxPacketLen)
            {
                flush_literals();
            }
        }
    }

    // Flush any remaining literal bytes.
    flush_literals();
}

}  // namespace

// =============================================================================
// compress_rle
// =============================================================================
CompressedSave compress_rle(std::span<const std::uint8_t> raw)
{
    CompressedSave result;
    result.original_size = raw.size();

    if (raw.empty())
    {
        result.ratio = std::numeric_limits<double>::quiet_NaN();
        return result;
    }

    // Worst-case: every byte is unique → 1 header per byte + 1 data byte
    // per literal packet header (max 128 literals per packet, so worst case
    // overhead is 1/128 bytes per input byte — but per-byte accumulation
    // means 2 output bytes per input byte at the absolute floor for size-1
    // packets; in practice packets are 128 literals + 1 header → negligible).
    // Reserve generously: original + (original/128 + 1) header bytes.
    const std::size_t worst_case = raw.size() + (raw.size() / 128U) + 1U;
    result.blob.reserve(worst_case);

    encode_rle_into(raw, result.blob);

    result.ratio = static_cast<double>(result.blob.size()) /
                   static_cast<double>(result.original_size);

    return result;
}

// =============================================================================
// decompress_rle
// =============================================================================
std::optional<std::vector<std::uint8_t>>
decompress_rle(const CompressedSave& compressed)
{
    // Validate: empty blob with non-zero original_size is corrupt.
    if (compressed.blob.empty())
    {
        if (compressed.original_size == 0U)
        {
            return std::vector<std::uint8_t>{};
        }
        return std::nullopt;
    }

    std::vector<std::uint8_t> out;
    out.reserve(compressed.original_size);

    const std::uint8_t* const blob_begin = compressed.blob.data();
    const std::uint8_t* const blob_end   = blob_begin + compressed.blob.size();
    const std::uint8_t*       pos        = blob_begin;

    while (pos < blob_end)
    {
        const std::uint8_t header = *pos++;
        const bool         is_repeat = (header & kRepeatFlag) != 0U;
        const std::uint8_t count     = static_cast<std::uint8_t>((header & 0x7FU) + 1U);

        if (is_repeat)
        {
            // Repeat packet: needs exactly one payload byte.
            if (pos >= blob_end)
            {
                return std::nullopt;  // truncated blob
            }
            const std::uint8_t value = *pos++;
            for (std::uint8_t i = 0U; i < count; ++i)
            {
                out.push_back(value);
            }
        }
        else
        {
            // Literal packet: needs [count] payload bytes.
            if (static_cast<std::size_t>(blob_end - pos) < count)
            {
                return std::nullopt;  // truncated blob
            }
            out.insert(out.end(), pos, pos + count);
            pos += count;
        }
    }

    // Final size check: decoded output must exactly match original_size.
    if (out.size() != compressed.original_size)
    {
        return std::nullopt;
    }

    return out;
}

// =============================================================================
// benchmark
// =============================================================================
CompressionStats benchmark(std::span<const std::uint8_t> raw)
{
    using clock = std::chrono::high_resolution_clock;

    const auto t0 = clock::now();
    const CompressedSave cs = compress_rle(raw);
    const auto t1 = clock::now();

    const double elapsed_ms =
        static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()
        ) / 1.0e6;

    CompressionStats stats;
    stats.input_bytes  = raw.size();
    stats.output_bytes = cs.blob.size();
    stats.ratio        = cs.ratio;
    stats.time_ms      = elapsed_ms;
    return stats;
}

}  // namespace cd::game::save_compression
