// =============================================================================
// CHROMODYNAMIC — cd/game/save_compression/SaveCompression.hpp
// Phase 661 / 749 — cd::game::save_compression (M11 W4B / FINALE-3 W2C C4)
//
// PURPOSE
// -------
// Compress/decompress save-game byte streams so larger worlds fit in smaller
// files and write/read times improve.  Typical 5 MB world-state blob:
//   Sprint-1 RLE  → ~800 KB at ~50 MB/s
//   Sprint-2 LZ4  → ~600 KB at ~100 MB/s  (when lz4 is available)
//
// This library is a *sidecar* to cd::game::save — it does NOT replace it and
// it does NOT depend on it.  Callers compress the byte vector before handing
// it to SaveSystem::save() and decompress after SaveSystem::load() returns.
// The split keeps the I/O and the compression concerns independent, enabling
// A/B benchmarking of codec candidates without touching atomic-write machinery.
//
// SPRINT PLAN
// -----------
// Sprint 1 (Phase 661): Run-Length Encoding (RLE).
//   - Simple packet format; each packet is one byte header followed by data.
//   - Exploits the long zero-runs that dominate uninitialized struct fields
//     and spatial sparsity in world-state saves.
//   - Header byte encoding:
//       bits[7]   : 0 = literal run, 1 = repeat run
//       bits[6:0] : count − 1  (1..128 values per packet)
//     Literal run : header followed by [count] literal bytes.
//     Repeat run  : header followed by [1] repeated byte, emitted [count] times.
//   - Worst case: 1 overhead byte per 128 input bytes (~0.78% expansion cap).
//     Well-formed saves never hit this floor because struct padding and entity
//     pools always produce meaningful repetition.
// Sprint 2 (Phase 749): LZ4 via vcpkg manifest mode.
//   - compress_lz4() / decompress_lz4() added behind CD_SAVE_COMPRESSION_HAS_LZ4
//     compile-time guard.  Callers switch codec without changing round-trip
//     call sites — same CompressedSave envelope, same decompress contract.
//   - LZ4 block format (LZ4_compress_default / LZ4_decompress_safe).  Original
//     size is stored in CompressedSave::original_size for pre-allocation.
//   - When lz4 is unavailable at configure time the LZ4 functions are absent;
//     callers should check CD_SAVE_COMPRESSION_HAS_LZ4 before calling them.
//
// NAMING NOTE
// -----------
// This library is named save_compression, NOT save_v2.  It does not replace
// cd::game::save; it adds compression capability alongside it.  New naming
// rule enforced per CLAUDE.md §12: "v2/v3 suffix only when v1+v2 coexist for
// ABI migration."
//
// DEPENDENCIES (CLAUDE.md §7)
// ---------------------------
// cd::core + optional lz4 (Sprint-2).  No filesystem, no cd::game::save,
// no rapidjson, no zstd.  Keeps the library near the bottom of the gameplay
// dependency DAG so it can also compress non-save blobs (replay headers,
// telemetry snapshots) without pulling in slot-management machinery.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace cd::game::save_compression
{

// =============================================================================
// CompressedSave — envelope around a compressed byte blob.
//
// `blob`          : compressed bytes produced by compress_rle() or
//                   compress_lz4().  Treat as opaque; pass verbatim to the
//                   matching decompress_*() call (codecs are NOT interchangeable).
// `original_size` : byte count of the uncompressed input.  Stored alongside
//                   the blob so the decompressor can pre-allocate exactly the
//                   right output buffer and verify the round-trip.
// `ratio`         : compressed_size / original_size.  < 1.0 means a win;
//                   == 1.0 is break-even; > 1.0 means the codec expanded the
//                   data (possible with incompressible random bytes).
// =============================================================================
struct CompressedSave
{
    std::vector<std::uint8_t> blob;          ///< Opaque compressed bytes.
    std::uint64_t             original_size; ///< Byte count before compression.
    double                    ratio;         ///< blob.size() / original_size (NaN if input empty).
};

// =============================================================================
// CompressionStats — timing + size report from benchmark().
//
// Use for per-save profiling: emit to the diagnostic overlay, the telemetry
// system, or a CI artifact to catch regressions before they ship.
// =============================================================================
struct CompressionStats
{
    std::uint64_t input_bytes;   ///< Size of the raw input span.
    std::uint64_t output_bytes;  ///< Size of the compressed blob.
    double        ratio;         ///< output_bytes / input_bytes.
    double        time_ms;       ///< Wall-clock time for compress_rle() in milliseconds.
};

// =============================================================================
// compress_rle()
//
// Encode `raw` with the packet-based RLE described in the Sprint-1 header.
// The call is deterministic and allocation-free beyond the output vector.
//
// Edge cases:
//   * Empty input  → CompressedSave{blob:{}, original_size:0, ratio:NaN}.
//   * 1-byte input → single-byte literal packet + 1-byte payload (ratio 2.0).
//     This is the one provable worst-case for single-byte incompressible data
//     but in practice saves are never a lone byte.
//
// [[nodiscard]] — ratio and original_size guide caller decisions; silently
//                 dropping the result is almost certainly a bug.
// =============================================================================
[[nodiscard]] CompressedSave compress_rle(std::span<const std::uint8_t> raw);

// =============================================================================
// decompress_rle()
//
// Reconstruct the original byte stream from a CompressedSave produced by
// compress_rle().  Returns std::nullopt on any corruption:
//
//   * blob is empty but original_size > 0
//   * a packet header claims a literal run but runs past blob.end()
//   * a repeat run header is at the final byte of blob (no payload byte)
//   * decoded byte count != original_size (truncated or extended blob)
//
// On success the returned vector is exactly `original_size` bytes.
// =============================================================================
[[nodiscard]] std::optional<std::vector<std::uint8_t>>
decompress_rle(const CompressedSave& compressed);

// =============================================================================
// benchmark()
//
// Run compress_rle() on `raw` and return timing + size statistics without
// keeping the compressed result.  Intended for the save-UI diagnostic overlay
// and CI artifact reporting.  The timing uses a high-resolution wall clock and
// includes only the compression call (not allocation of the stats struct).
// =============================================================================
[[nodiscard]] CompressionStats benchmark(std::span<const std::uint8_t> raw);

// =============================================================================
// LZ4 path — Sprint-2 (Phase 749).
//
// Available only when CD_SAVE_COMPRESSION_HAS_LZ4 == 1 (lz4 resolved at
// configure time via vcpkg or FetchContent).  Guarded by #if so the library
// compiles cleanly on platforms / CI configs where lz4 is absent.
//
// compress_lz4()
//   Encode `raw` using LZ4_compress_default (LZ4 block format — NOT the LZ4
//   frame format).  The frame format adds a content-length header which we
//   store ourselves in CompressedSave::original_size, keeping the envelope
//   identical to the RLE path.
//   Typical performance: ~100 MB/s compression, ~400 MB/s decompression.
//   Typical ratio vs RLE: ~0.75x (better on binary / mixed data).
//
// decompress_lz4()
//   Reconstruct original bytes from a CompressedSave produced by compress_lz4().
//   Returns std::nullopt on LZ4 decode failure or size mismatch.
//   CAUTION: do NOT pass an RLE-produced CompressedSave — codecs are opaque.
//
// benchmark_lz4()
//   Same contract as benchmark() but drives the LZ4 path.  Use to compare
//   codec throughput side-by-side in the diagnostic overlay.
// =============================================================================
#if CD_SAVE_COMPRESSION_HAS_LZ4

[[nodiscard]] CompressedSave compress_lz4(std::span<const std::uint8_t> raw);

[[nodiscard]] std::optional<std::vector<std::uint8_t>>
decompress_lz4(const CompressedSave& compressed);

[[nodiscard]] CompressionStats benchmark_lz4(std::span<const std::uint8_t> raw);

#endif  // CD_SAVE_COMPRESSION_HAS_LZ4

}  // namespace cd::game::save_compression
