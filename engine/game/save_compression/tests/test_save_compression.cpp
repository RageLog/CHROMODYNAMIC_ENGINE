// =============================================================================
// CHROMODYNAMIC — test_save_compression.cpp
// Phase 661 — Unit tests for cd::game::save_compression (Sprint-1 RLE)
//
// Test plan (Arrange / Act / Assert pattern, isolated per-test):
//
//   T1  round_trip_random_data
//       Round-trip through compress+decompress preserves data byte-for-byte.
//
//   T2  all_zero_compresses_small
//       1 MiB of zeroes → blob significantly smaller than input;
//       ratio < 0.02 (at most 2% overhead — RLE collapses zero runs into
//       128-byte repeat packets: 2 bytes output per 128 input → 1.5% ratio).
//
//   T3  all_different_ratio_le_one_point_zero_plus_overhead
//       A strictly alternating 0x00/0xFF sequence cannot be compressed;
//       ratio ≤ 1.01 (at most 1% overhead from packet headers).
//       Verifies the worst-case overhead bound documented in the header.
//
//   T4  empty_input_handled_gracefully
//       compress_rle({}) → blob empty, original_size == 0, ratio is NaN.
//       decompress_rle on that result → empty vector (no nullopt).
//
//   T5  decompress_rejects_truncated_blob
//       A blob that claims a literal packet of 10 bytes but contains only
//       5 payload bytes must return std::nullopt (not UB, not wrong data).
//
//   T6  decompress_rejects_original_size_mismatch
//       A valid blob paired with an inflated original_size must return
//       std::nullopt so callers cannot silently read garbage past the
//       decoded region.
//
//   T7  benchmark_returns_sane_stats
//       benchmark() on a 64 KiB all-zero buffer returns input_bytes==65536,
//       output_bytes < input_bytes, ratio < 1.0, time_ms >= 0.0.
// =============================================================================

#include <cd/game/save_compression/SaveCompression.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

namespace cd::game::save_compression::tests
{

// ============================================================================
// T1 — round-trip preserves all bytes
// ============================================================================
TEST(SaveCompressionRle, RoundTripRandomData)
{
    // Arrange: a pseudo-random 256-byte pattern (deterministic — no stdlib rand).
    std::vector<std::uint8_t> raw(256);
    for (std::size_t i = 0; i < raw.size(); ++i)
    {
        // Simple LCG: produces a varied pattern with some short runs.
        raw[i] = static_cast<std::uint8_t>((i * 6364136223846793005ULL + 1442695040888963407ULL) >> 56U);
    }

    // Act
    const CompressedSave cs  = compress_rle(raw);
    const auto           out = decompress_rle(cs);

    // Assert
    ASSERT_TRUE(out.has_value()) << "decompress_rle returned nullopt unexpectedly";
    EXPECT_EQ(out->size(), raw.size());
    EXPECT_EQ(*out, raw) << "Round-trip byte mismatch";
}

// ============================================================================
// T2 — all-zero input compresses to a tiny blob
// ============================================================================
TEST(SaveCompressionRle, AllZeroCompressesSmall)
{
    // Arrange: 1 MiB of zeroes simulates uninitialized struct-field runs.
    constexpr std::size_t kSize = 1U << 20U;  // 1 MiB
    const std::vector<std::uint8_t> raw(kSize, 0x00U);

    // Act
    const CompressedSave cs = compress_rle(raw);

    // Assert — blob should be tiny (repeat packets: 2 bytes per 128 input bytes)
    // Theoretical minimum for 1 MiB: ceil(1048576 / 128) * 2 = 16384 bytes.
    // Allow 2% headroom for the encoder flushing a sub-128 trailing packet.
    EXPECT_EQ(cs.original_size, kSize);
    EXPECT_LT(cs.ratio, 0.02)
        << "Expected ratio < 2% for all-zero input, got " << cs.ratio;

    // Verify round-trip.
    const auto out = decompress_rle(cs);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(*out, raw);
}

// ============================================================================
// T3 — incompressible alternating input: ratio not more than ~1% overhead
// ============================================================================
TEST(SaveCompressionRle, AllDifferentRatioLeOne)
{
    // Arrange: 4 KiB alternating 0x00 / 0xFF — worst-case for repeat RLE
    // (every pair is a 2-byte run, which is a break-even repeat packet).
    // The encoder will emit repeat packets of length 1 for each pair, which
    // gives: 2 bytes output per 2 bytes input = ratio 1.0 exactly.
    // A fully shuffled buffer gives literal packets: 1 header per 128 bytes.
    constexpr std::size_t kSize = 4096U;
    std::vector<std::uint8_t> raw(kSize);
    for (std::size_t i = 0; i < kSize; ++i)
    {
        raw[i] = (i % 2U == 0U) ? 0x00U : 0xFFU;
    }

    // Act
    const CompressedSave cs = compress_rle(raw);

    // Assert — ratio must not exceed 1.01 (1% overhead cap from the header doc)
    EXPECT_LE(cs.ratio, 1.01)
        << "Incompressible input expanded by more than 1%: ratio=" << cs.ratio;

    // Round-trip.
    const auto out = decompress_rle(cs);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(*out, raw);
}

// ============================================================================
// T4 — empty input handled gracefully
// ============================================================================
TEST(SaveCompressionRle, EmptyInputHandledGracefully)
{
    // Arrange
    const std::vector<std::uint8_t> empty_raw;

    // Act — compress
    const CompressedSave cs = compress_rle(empty_raw);

    // Assert compress
    EXPECT_TRUE(cs.blob.empty());
    EXPECT_EQ(cs.original_size, 0U);
    EXPECT_TRUE(std::isnan(cs.ratio)) << "Expected NaN ratio for empty input";

    // Act — decompress the empty CompressedSave
    const auto out = decompress_rle(cs);

    // Assert decompress
    ASSERT_TRUE(out.has_value()) << "decompress_rle should return empty vector for empty input";
    EXPECT_TRUE(out->empty());
}

// ============================================================================
// T5 — decompress rejects a truncated blob (corrupted literal payload)
// ============================================================================
TEST(SaveCompressionRle, DecompressRejectsTruncatedBlob)
{
    // Arrange: craft a blob where the header claims 10 literal bytes but only
    // 5 payload bytes are present.
    // Header byte for 10-byte literal packet: bit7=0, count-1=9 → 0x09.
    CompressedSave corrupt;
    corrupt.original_size = 10U;
    corrupt.blob.push_back(0x09U);  // literal header: count=10
    // Only 5 payload bytes instead of the promised 10.
    for (std::uint8_t i = 0U; i < 5U; ++i)
    {
        corrupt.blob.push_back(i);
    }
    corrupt.ratio = 0.6;

    // Act
    const auto out = decompress_rle(corrupt);

    // Assert
    EXPECT_FALSE(out.has_value())
        << "decompress_rle should return nullopt for truncated literal payload";
}

// ============================================================================
// T6 — decompress rejects original_size mismatch
// ============================================================================
TEST(SaveCompressionRle, DecompressRejectsOriginalSizeMismatch)
{
    // Arrange: compress a 16-byte buffer, then lie about original_size.
    const std::vector<std::uint8_t> raw(16U, 0xABU);
    CompressedSave cs = compress_rle(raw);

    // Corrupt: claim the original was 999 bytes.
    cs.original_size = 999U;

    // Act
    const auto out = decompress_rle(cs);

    // Assert
    EXPECT_FALSE(out.has_value())
        << "decompress_rle should return nullopt when decoded size != original_size";
}

// ============================================================================
// T7 — benchmark returns sane stats
// ============================================================================
TEST(SaveCompressionRle, BenchmarkReturnsSaneStats)
{
    // Arrange: 64 KiB all-zero buffer (compresses well).
    constexpr std::size_t kSize = 64U * 1024U;
    const std::vector<std::uint8_t> raw(kSize, 0x00U);

    // Act
    const CompressionStats stats = benchmark(raw);

    // Assert
    EXPECT_EQ(stats.input_bytes, kSize);
    EXPECT_LT(stats.output_bytes, stats.input_bytes)
        << "All-zero buffer should compress; output_bytes should be < input_bytes";
    EXPECT_LT(stats.ratio, 1.0)
        << "ratio should be < 1.0 for compressible input";
    EXPECT_GE(stats.time_ms, 0.0)
        << "Elapsed time must be non-negative";
}

}  // namespace cd::game::save_compression::tests
