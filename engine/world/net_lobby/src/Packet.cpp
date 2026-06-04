// =============================================================================
// CHROMODYNAMIC — cd/network/lobby/Packet.cpp
// Phase 723 / Sprint W5B — encode_packet / decode_packet implementation
// =============================================================================
#include <cd/network/lobby/Packet.hpp>

#include <array>
#include <cstring>

namespace cd::network::lobby
{

// ---------------------------------------------------------------------------
// Internal helpers — little-endian serialisation
// ---------------------------------------------------------------------------

namespace
{

void write_u8(std::vector<std::uint8_t>& buf, std::uint8_t v)
{
    buf.push_back(v);
}

void write_u32_le(std::vector<std::uint8_t>& buf, std::uint32_t v)
{
    buf.push_back(static_cast<std::uint8_t>( v        & 0xFFU));
    buf.push_back(static_cast<std::uint8_t>((v >>  8U) & 0xFFU));
    buf.push_back(static_cast<std::uint8_t>((v >> 16U) & 0xFFU));
    buf.push_back(static_cast<std::uint8_t>((v >> 24U) & 0xFFU));
}

void write_u64_le(std::vector<std::uint8_t>& buf, std::uint64_t v)
{
    buf.push_back(static_cast<std::uint8_t>( v        & 0xFFU));
    buf.push_back(static_cast<std::uint8_t>((v >>  8U) & 0xFFU));
    buf.push_back(static_cast<std::uint8_t>((v >> 16U) & 0xFFU));
    buf.push_back(static_cast<std::uint8_t>((v >> 24U) & 0xFFU));
    buf.push_back(static_cast<std::uint8_t>((v >> 32U) & 0xFFU));
    buf.push_back(static_cast<std::uint8_t>((v >> 40U) & 0xFFU));
    buf.push_back(static_cast<std::uint8_t>((v >> 48U) & 0xFFU));
    buf.push_back(static_cast<std::uint8_t>((v >> 56U) & 0xFFU));
}

[[nodiscard]] std::uint32_t read_u32_le(std::span<const std::uint8_t> bytes,
                                         std::size_t offset) noexcept
{
    return  static_cast<std::uint32_t>(bytes[offset + 0U])
         | (static_cast<std::uint32_t>(bytes[offset + 1U]) <<  8U)
         | (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U)
         | (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] std::uint64_t read_u64_le(std::span<const std::uint8_t> bytes,
                                         std::size_t offset) noexcept
{
    return  static_cast<std::uint64_t>(bytes[offset + 0U])
         | (static_cast<std::uint64_t>(bytes[offset + 1U]) <<  8U)
         | (static_cast<std::uint64_t>(bytes[offset + 2U]) << 16U)
         | (static_cast<std::uint64_t>(bytes[offset + 3U]) << 24U)
         | (static_cast<std::uint64_t>(bytes[offset + 4U]) << 32U)
         | (static_cast<std::uint64_t>(bytes[offset + 5U]) << 40U)
         | (static_cast<std::uint64_t>(bytes[offset + 6U]) << 48U)
         | (static_cast<std::uint64_t>(bytes[offset + 7U]) << 56U);
}

}  // namespace

// ---------------------------------------------------------------------------
// encode_packet
// ---------------------------------------------------------------------------

std::vector<std::uint8_t> encode_packet(const Packet& pkt)
{
    std::vector<std::uint8_t> buf;
    buf.reserve(kMinPacketSize + pkt.payload.size());

    // kind : uint8_t
    write_u8(buf, static_cast<std::uint8_t>(pkt.kind));

    // room_id : uint64_t LE
    write_u64_le(buf, pkt.room_id);

    // player_id : uint64_t LE
    write_u64_le(buf, pkt.player_id);

    // payload_len : uint32_t LE
    write_u32_le(buf, static_cast<std::uint32_t>(pkt.payload.size()));

    // payload bytes
    buf.insert(buf.end(), pkt.payload.begin(), pkt.payload.end());

    return buf;
}

// ---------------------------------------------------------------------------
// decode_packet
// ---------------------------------------------------------------------------

std::optional<Packet> decode_packet(std::span<const std::uint8_t> bytes)
{
    // Minimum header check.
    if (bytes.size() < kMinPacketSize)
        return std::nullopt;

    // Decode header fields.
    const std::uint8_t  raw_kind   = bytes[0];
    const std::uint64_t room_id    = read_u64_le(bytes, 1U);
    const std::uint64_t player_id  = read_u64_le(bytes, 9U);
    const std::uint32_t payload_len = read_u32_le(bytes, 17U);

    // Kind range check.
    if (raw_kind >= static_cast<std::uint8_t>(PacketKind::kCount_))
        return std::nullopt;

    // Overrun guard: ensure the buffer actually contains payload_len bytes.
    const std::size_t total_needed = kMinPacketSize + static_cast<std::size_t>(payload_len);
    if (bytes.size() < total_needed)
        return std::nullopt;

    // Build result.
    Packet pkt;
    pkt.kind      = static_cast<PacketKind>(raw_kind);
    pkt.room_id   = room_id;
    pkt.player_id = player_id;
    pkt.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(kMinPacketSize),
                       bytes.begin() + static_cast<std::ptrdiff_t>(total_needed));

    return pkt;
}

}  // namespace cd::network::lobby
