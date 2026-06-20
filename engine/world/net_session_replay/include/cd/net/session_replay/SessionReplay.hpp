// =============================================================================
// CHROMODYNAMIC — cd/net/session_replay/SessionReplay.hpp
// Phase 600 — cd::net::session_replay (Sprint-2 complete)
//
// Record and replay network packets for debugging multiplayer issues.
//
// Public types:
//   PacketRecord  — timestamped packet snapshot (payload + channel + direction).
//   Recorder      — captures packets during a live session; serialises to disk.
//   Replayer      — loads a saved file and drives playback by wall-clock query.
//
// Binary file format (little-endian):
//   [0..3]  magic        : 0x53 0x52 0x50 0x4B  ("SRPK")
//   [4..7]  version      : uint32_t = 1
//   [8..11] count        : uint32_t — number of PacketRecord entries
//   For each record:
//     double    timestamp_ms  (8 bytes)
//     uint32_t  channel_id   (4 bytes)
//     uint8_t   incoming      (1 byte, 0 or 1)
//     uint32_t  payload_size  (4 bytes)
//     uint8_t[] payload       (payload_size bytes)
//
// Monotonicity contract:
//   Records MUST be written in non-decreasing timestamp_ms order.
//   load_from_file() rejects a file that violates this invariant so that
//   Replayer::next_packet() can trust the cursor model without scanning ahead.
//
// Thread-safety: Recorder and Replayer are NOT thread-safe; external
// synchronisation is the caller's responsibility.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace cd::net::session_replay
{

// ---------------------------------------------------------------------------
// PacketRecord
// ---------------------------------------------------------------------------

/// A single captured network packet.
struct PacketRecord
{
    double                      timestamp_ms { 0.0 };
    std::vector<std::uint8_t>   payload;
    std::uint32_t               channel_id   { 0 };
    bool                        incoming     { true };
};

// ---------------------------------------------------------------------------
// Recorder
// ---------------------------------------------------------------------------

/// Captures packets during a live session.
///
/// Usage:
///   Recorder rec;
///   rec.start_recording();
///   rec.record_packet(data, channel, incoming);
///   rec.stop_recording();
///   rec.save_to_file("session.srpk");
class Recorder
{
public:
    Recorder() noexcept = default;
    ~Recorder() noexcept = default;
    Recorder(const Recorder&)            = delete;
    Recorder& operator=(const Recorder&) = delete;
    Recorder(Recorder&&) noexcept        = default;
    Recorder& operator=(Recorder&&) noexcept = default;

    /// Begin recording. Clears any previously recorded data.
    void start_recording();

    /// Append a packet snapshot. The timestamp is captured from
    /// std::chrono::steady_clock relative to the start_recording() call, in
    /// milliseconds.  Silently ignored when not recording.
    void record_packet(std::span<const std::uint8_t> payload,
                       std::uint32_t                  channel_id,
                       bool                           incoming);

    /// Stop accepting new packets.
    void stop_recording() noexcept;

    /// Serialise recorded packets to `out_path` in the binary format described
    /// in the file header.  Returns false on I/O error.
    [[nodiscard]] bool save_to_file(const std::filesystem::path& out_path) const;

    /// Number of recorded packets.
    [[nodiscard]] std::size_t packet_count() const noexcept;

    /// True while start_recording() is active and stop_recording() has not
    /// been called.
    [[nodiscard]] bool is_recording() const noexcept;

private:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    std::vector<PacketRecord> records_;
    TimePoint                 start_time_;
    bool                      recording_ { false };
};

// ---------------------------------------------------------------------------
// Replayer
// ---------------------------------------------------------------------------

/// Loads a saved session file and drives time-ordered playback.
///
/// Usage:
///   Replayer rep;
///   if (!rep.load_from_file("session.srpk")) { /* error */ }
///   while (auto pkt = rep.next_packet(current_ms)) { handle(*pkt); }
class Replayer
{
public:
    Replayer() noexcept = default;
    ~Replayer() noexcept = default;
    Replayer(const Replayer&)            = delete;
    Replayer& operator=(const Replayer&) = delete;
    Replayer(Replayer&&) noexcept        = default;
    Replayer& operator=(Replayer&&) noexcept = default;

    /// Load packets from `in_path`.
    /// Returns false if the file cannot be opened, the magic bytes are wrong,
    /// the format version is unsupported, the stream is truncated, or the
    /// packet timestamps are not monotonically non-decreasing.
    [[nodiscard]] bool load_from_file(const std::filesystem::path& in_path);

    /// Span over ALL loaded packets (unfiltered).
    [[nodiscard]] std::span<const PacketRecord> all() const noexcept;

    /// Return the next packet whose timestamp_ms <= current_ms and advance the
    /// internal cursor.  Returns std::nullopt when no more packets qualify or
    /// all packets have been consumed.
    [[nodiscard]] std::optional<PacketRecord> next_packet(double current_ms);

    /// Rewind playback cursor to the first packet.
    void reset() noexcept;

    /// Advance the cursor to the first packet whose timestamp_ms >= target_ms.
    /// Packets before that position are skipped (as if consumed).
    /// Useful for scrubbing / jumping mid-session.
    void seek_to(double target_ms) noexcept;

    /// True when the cursor is past the last packet (all packets delivered).
    [[nodiscard]] bool finished() const noexcept;

    /// Number of packets loaded.
    [[nodiscard]] std::size_t packet_count() const noexcept;

private:
    std::vector<PacketRecord> records_;
    std::size_t               cursor_ { 0 };
};

}  // namespace cd::net::session_replay
