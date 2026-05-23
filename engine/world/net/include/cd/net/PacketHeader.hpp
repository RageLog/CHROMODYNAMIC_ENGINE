// =============================================================================
// CHROMODYNAMIC — cd/net/PacketHeader.hpp
// Phase 28.C / Wave 197 — versioned UDP packet preamble.
//
// Every datagram sent by the CHROMODYNAMIC net stack carries an 8-byte
// preamble that lets the receiver reject foreign traffic, detect
// protocol drift across client/server versions, and dispatch payload by
// opcode without parsing further:
//
//   ┌─────────────────────────────────────────┐
//   │ u32 magic   — 'CDNT' (0x434E4454)        │
//   │ u8  version — wire-protocol version       │
//   │ u8  opcode  — payload kind (see Opcode)   │
//   │ u16 length  — payload length, little-end. │
//   └─────────────────────────────────────────┘
//
// Wire format is fixed little-endian to keep parity with ChannelMux.
// Header is C-style POD so it can be memcpy'd in/out of network buffers
// without endian conversion on LE platforms (Intel/ARM, our entire
// target matrix); BE platforms would need byte-swap shims which we
// haven't shipped because no BE target is on the roadmap.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>

namespace cd::net
{

inline constexpr std::uint32_t kPacketMagic   = 0x434E4454u;  // 'CDNT'
inline constexpr std::uint8_t  kPacketVersion = 1u;

enum class Opcode : std::uint8_t
{
    kInvalid     = 0,
    kHandshake   = 1,
    kHeartbeat   = 2,
    kReliable    = 3,
    kUnreliable  = 4,
    kAck         = 5,
    kDisconnect  = 6,
};

struct PacketHeader
{
    std::uint32_t magic { kPacketMagic };
    std::uint8_t  version { kPacketVersion };
    Opcode        opcode { Opcode::kInvalid };
    std::uint16_t length { 0 };  ///< payload length in bytes (LE on wire)
};
static_assert(sizeof(PacketHeader) == 8, "PacketHeader must stay 8 bytes");

[[nodiscard]] constexpr bool is_valid(const PacketHeader& h) noexcept
{
    return h.magic == kPacketMagic
        && h.version == kPacketVersion
        && h.opcode != Opcode::kInvalid;
}

}  // namespace cd::net
