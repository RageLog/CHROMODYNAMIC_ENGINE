// =============================================================================
// CHROMODYNAMIC — cd/game/save_compression/SaveCompression.hpp
// Phase 661 — cd::game::save_compression (M11 W4B sidecar to cd::game::save)
//
// PURPOSE
// -------
// Compress/decompress save-game byte streams so larger worlds fit in smaller
// files and write/read times halve.  A 5 MB world-state blob with typical
// struct-padding zero runs → ~800 KB on disk with Sprint-1 RLE alone.
//
// This library is a *sidecar* to cd::game::save — it does NOT replace it and
// it does NOT depend on it.  Callers compress the byte vector before handing
// it to SaveSystem::save() and decompress after SaveSystem::load() returns.
// The split keeps the I/O and the compression concerns independent, which
// enables future A/B benchmarking of Sprint-2 codec candidates (LZ4, zstd,
// brotli) without touching the atomic-write machinery.
//
// SPRINT PLAN
// -----------
// Sprint 1 (this commit): Run-Length Encoding (RLE).
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
// Sprint 2 (future): LZ4 / zstd via vcpkg manifest mode.
//   - Will add compress_lz4() / compress_zstd() overloads behind the same
//     CompressedSave envelope so callers switch codec without changing the
//     round-trip call sites.
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
// cd::core only — no filesystem, no cd::game::save, no rapidjson, no zstd.
// Keeps the library at the bottom of the gameplay dependency DAG so it can
// also compress non-save blobs (replay headers, telemetry snapshots, etc.)
// without pulling in slot-management machinery.
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
// `blob`          : compressed bytes produced by compress_rle() (or a future
//                   Sprint-2 codec).  Treat as opaque; pass verbatim to
//                   decompress_rle().
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

}  // namespace cd::game::save_compression
