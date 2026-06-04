// =============================================================================
// CHROMODYNAMIC — cd/network/lobby/Packet.hpp
// Phase 723 / Sprint W5B — cd::network::lobby Sprint-2 packet codec
//
// Wire-format types and codec helpers for lobby synchronisation.
//
// Public types:
//   PacketKind  — discriminant enum (kCreateRoom … kSyncRoomState).
//   Packet      — value-type carrying kind + room_id + player_id + payload.
//
// Public functions:
//   encode_packet(const Packet&)                    → std::vector<uint8_t>
//   decode_packet(std::span<const uint8_t>)         → std::optional<Packet>
//
// Wire format (little-endian):
//   [0]      kind         : uint8_t
//   [1..8]   room_id      : uint64_t  (LE)
//   [9..16]  player_id    : uint64_t  (LE)
//   [17..20] payload_len  : uint32_t  (LE)
//   [21..]   payload      : payload_len bytes
//
// Total minimum packet size: 21 bytes (kMinPacketSize).
//
// Reject policy: decode_packet returns std::nullopt when:
//   - span is shorter than kMinPacketSize
//   - span is shorter than kMinPacketSize + payload_len (overrun guard)
//   - kind byte encodes a value outside the valid PacketKind range
//
// SOTA note:
//   ENet and Valve GNS use similar length-prefix framing for reliable-UDP
//   channels. This format keeps the header fixed-width (21 bytes) so a
//   future ring-buffer or zero-copy implementation can pre-allocate with
//   certainty.
// =============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace cd::network::lobby
{

// ---------------------------------------------------------------------------
// PacketKind
// ---------------------------------------------------------------------------

/// Discriminant for lobby wire messages.
enum class PacketKind : std::uint8_t
{
    kCreateRoom    = 0,
    kJoinRoom      = 1,
    kLeaveRoom     = 2,
    kSetReady      = 3,
    kStartGame     = 4,
    kSyncRoomState = 5,

    kCount_  ///< Sentinel — not a valid kind; used for range checks.
};

// ---------------------------------------------------------------------------
// Packet
// ---------------------------------------------------------------------------

/// Value-type representing a single lobby wire message.
///
/// `payload` semantics depend on `kind`:
///   - kSyncRoomState : serialised player array (variable length).
///   - kSetReady      : single byte, 0 = not-ready / 1 = ready.
///   - others         : empty or caller-defined.
struct Packet
{
    PacketKind              kind      { PacketKind::kCreateRoom };
    std::uint64_t           room_id   { 0 };
    std::uint64_t           player_id { 0 };
    std::vector<std::uint8_t> payload;
};

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/// Minimum encoded size: kind(1) + room_id(8) + player_id(8) + payload_len(4).
inline constexpr std::size_t kMinPacketSize { 21U };

// ---------------------------------------------------------------------------
// encode_packet
// ---------------------------------------------------------------------------

/// Serialise `pkt` to the length-prefixed wire format.
/// Never throws; result size is always >= kMinPacketSize.
[[nodiscard]] std::vector<std::uint8_t> encode_packet(const Packet& pkt);

// ---------------------------------------------------------------------------
// decode_packet
// ---------------------------------------------------------------------------

/// Deserialise a packet from `bytes`.
/// Returns std::nullopt on any error (too short, overrun, unknown kind).
/// On success the returned Packet's payload is a copy of the embedded bytes.
[[nodiscard]] std::optional<Packet>
decode_packet(std::span<const std::uint8_t> bytes);

}  // namespace cd::network::lobby
