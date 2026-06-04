// =============================================================================
// CHROMODYNAMIC — cd::net::session_replay tests
// Phase 600
//
// Tests: 7
//   1. RecordSaveLoadRoundTrip         — record + save + load verifies payload/meta
//   2. RecordingStoppedRejectsPacket   — record_packet ignored when not recording
//   3. ReplayReturnsNulloptAtEnd       — next_packet returns nullopt after all consumed
//   4. ResetRewindsPlayback            — reset rewinds cursor to start
//   5. MalformedFileRejected           — load_from_file returns false on bad magic
//   6. ReplayTimeGatedDelivery         — next_packet respects current_ms threshold
//   7. EmptyRecordingSaveAndLoad       — zero-packet file round-trips cleanly
// =============================================================================
#include <cd/net/session_replay/SessionReplay.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{

using cd::net::session_replay::PacketRecord;
using cd::net::session_replay::Recorder;
using cd::net::session_replay::Replayer;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Returns a temp file path unique to this test.
[[nodiscard]] std::filesystem::path make_temp_path(std::string_view suffix)
{
    return std::filesystem::temp_directory_path() /
           (std::string("cd_sr_test_") + std::string(suffix) + ".srpk");
}

/// Build a small payload from an initialiser list.
[[nodiscard]] std::vector<std::uint8_t> make_payload(std::initializer_list<std::uint8_t> bytes)
{
    return {bytes};
}

// ---------------------------------------------------------------------------
// Write a synthetic binary file that Replayer can load, giving explicit
// control over per-packet timestamps (needed for time-gated tests).
//
// Format mirrors SessionReplay.cpp:
//   magic[4] | version(u32) | count(u32) |
//   for each: timestamp_ms(double) | channel_id(u32) | incoming(u8) |
//             payload_size(u32) | payload[payload_size]
// ---------------------------------------------------------------------------

struct SyntheticPacket
{
    double                    timestamp_ms;
    std::uint32_t             channel_id;
    bool                      incoming;
    std::vector<std::uint8_t> payload;
};

[[nodiscard]] bool write_synthetic_file(const std::filesystem::path&          path,
                                        const std::vector<SyntheticPacket>&   pkts)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        return false;

    // Magic "SRPK"
    const std::array<std::uint8_t, 4> magic { 0x53, 0x52, 0x50, 0x4B };
    out.write(reinterpret_cast<const char*>(magic.data()),   // NOLINT
              static_cast<std::streamsize>(magic.size()));

    const std::uint32_t version { 1 };
    out.write(reinterpret_cast<const char*>(&version),       // NOLINT
              static_cast<std::streamsize>(sizeof(version)));

    const auto count = static_cast<std::uint32_t>(pkts.size());
    out.write(reinterpret_cast<const char*>(&count),         // NOLINT
              static_cast<std::streamsize>(sizeof(count)));

    for (const SyntheticPacket& p : pkts)
    {
        out.write(reinterpret_cast<const char*>(&p.timestamp_ms),   // NOLINT
                  static_cast<std::streamsize>(sizeof(p.timestamp_ms)));
        out.write(reinterpret_cast<const char*>(&p.channel_id),     // NOLINT
                  static_cast<std::streamsize>(sizeof(p.channel_id)));
        const std::uint8_t incoming_byte = p.incoming ? std::uint8_t{1} : std::uint8_t{0};
        out.write(reinterpret_cast<const char*>(&incoming_byte),    // NOLINT
                  static_cast<std::streamsize>(sizeof(incoming_byte)));
        const auto sz = static_cast<std::uint32_t>(p.payload.size());
        out.write(reinterpret_cast<const char*>(&sz),               // NOLINT
                  static_cast<std::streamsize>(sizeof(sz)));
        if (sz > 0)
            out.write(reinterpret_cast<const char*>(p.payload.data()),  // NOLINT
                      static_cast<std::streamsize>(sz));
    }
    return out.good();
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. Record + save + load round-trip
// ---------------------------------------------------------------------------
TEST(SessionReplay, RecordSaveLoadRoundTrip)
{
    const std::filesystem::path path = make_temp_path("roundtrip");

    const std::vector<std::uint8_t> p1 = make_payload({ 0xDE, 0xAD });
    const std::vector<std::uint8_t> p2 = make_payload({ 0xBE, 0xEF, 0xFF });

    Recorder rec;
    rec.start_recording();
    rec.record_packet(p1, /*channel=*/1, /*incoming=*/true);
    rec.record_packet(p2, /*channel=*/2, /*incoming=*/false);
    rec.stop_recording();

    ASSERT_EQ(rec.packet_count(), 2U);
    ASSERT_TRUE(rec.save_to_file(path));

    Replayer rep;
    ASSERT_TRUE(rep.load_from_file(path));

    const auto all = rep.all();
    ASSERT_EQ(all.size(), 2U);

    // Payload and metadata round-trip correctly.
    EXPECT_EQ(all[0].channel_id, 1U);
    EXPECT_TRUE(all[0].incoming);
    EXPECT_EQ(all[0].payload, p1);

    EXPECT_EQ(all[1].channel_id, 2U);
    EXPECT_FALSE(all[1].incoming);
    EXPECT_EQ(all[1].payload, p2);

    // Timestamps are monotonically non-decreasing (auto-captured from clock).
    EXPECT_GE(all[0].timestamp_ms, 0.0);
    EXPECT_GE(all[1].timestamp_ms, all[0].timestamp_ms);

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// 2. record_packet is ignored when not recording
// ---------------------------------------------------------------------------
TEST(SessionReplay, RecordingStoppedRejectsPacket)
{
    Recorder rec;
    // Never called start_recording() — packets must be ignored.
    const std::vector<std::uint8_t> data = make_payload({ 0x01, 0x02, 0x03 });
    rec.record_packet(data, 0, true);
    EXPECT_EQ(rec.packet_count(), 0U);

    // Start, record one, stop, then try to record again.
    rec.start_recording();
    rec.record_packet(data, 0, true);
    EXPECT_EQ(rec.packet_count(), 1U);
    rec.stop_recording();
    rec.record_packet(data, 0, true);
    EXPECT_EQ(rec.packet_count(), 1U);  // still 1 — not incremented after stop
}

// ---------------------------------------------------------------------------
// 3. next_packet returns nullopt after all packets are consumed
// ---------------------------------------------------------------------------
TEST(SessionReplay, ReplayReturnsNulloptAtEnd)
{
    const std::filesystem::path path = make_temp_path("nullopt");
    ASSERT_TRUE(write_synthetic_file(path, {
        SyntheticPacket { 1.0, 0, true, make_payload({ 0xAA }) }
    }));

    Replayer rep;
    ASSERT_TRUE(rep.load_from_file(path));

    // First call at t=100 — qualifies.
    const auto first = rep.next_packet(100.0);
    EXPECT_TRUE(first.has_value());

    // Second call — cursor is past the end.
    const auto second = rep.next_packet(100.0);
    EXPECT_FALSE(second.has_value());

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// 4. reset() rewinds the playback cursor
// ---------------------------------------------------------------------------
TEST(SessionReplay, ResetRewindsPlayback)
{
    const std::filesystem::path path = make_temp_path("reset");
    ASSERT_TRUE(write_synthetic_file(path, {
        SyntheticPacket { 1.0, 0, true,  make_payload({ 0x10 }) },
        SyntheticPacket { 2.0, 0, false, make_payload({ 0x20 }) }
    }));

    Replayer rep;
    ASSERT_TRUE(rep.load_from_file(path));

    // Consume all packets.
    EXPECT_TRUE(rep.next_packet(999.0).has_value());
    EXPECT_TRUE(rep.next_packet(999.0).has_value());
    EXPECT_FALSE(rep.next_packet(999.0).has_value());

    // Reset and verify playback restarts from the first packet.
    rep.reset();
    const auto after_reset = rep.next_packet(999.0);
    ASSERT_TRUE(after_reset.has_value());
    EXPECT_EQ(after_reset->payload, std::vector<std::uint8_t>({ 0x10 }));

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// 5. Malformed file (bad magic bytes) is rejected
// ---------------------------------------------------------------------------
TEST(SessionReplay, MalformedFileRejected)
{
    const std::filesystem::path path = make_temp_path("malformed");

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.is_open());
        // Write garbage magic followed by plausible header fields.
        const std::array<std::uint8_t, 12> garbage {
            0xFF, 0xFF, 0xFF, 0xFF,  // wrong magic
            0x01, 0x00, 0x00, 0x00,  // version = 1
            0x00, 0x00, 0x00, 0x00   // count = 0
        };
        out.write(reinterpret_cast<const char*>(garbage.data()),  // NOLINT
                  static_cast<std::streamsize>(garbage.size()));
    }

    Replayer rep;
    EXPECT_FALSE(rep.load_from_file(path));

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// 6. next_packet respects the current_ms threshold (time-gated delivery)
// ---------------------------------------------------------------------------
TEST(SessionReplay, ReplayTimeGatedDelivery)
{
    const std::filesystem::path path = make_temp_path("timegated");
    // Three packets at t=5, 15, 25 ms.
    ASSERT_TRUE(write_synthetic_file(path, {
        SyntheticPacket {  5.0, 0, true, make_payload({ 0x05 }) },
        SyntheticPacket { 15.0, 0, true, make_payload({ 0x15 }) },
        SyntheticPacket { 25.0, 0, true, make_payload({ 0x25 }) }
    }));

    Replayer rep;
    ASSERT_TRUE(rep.load_from_file(path));

    // At t=10 only the first packet (ts=5) qualifies.
    const auto at10 = rep.next_packet(10.0);
    ASSERT_TRUE(at10.has_value());
    EXPECT_EQ(at10->payload, std::vector<std::uint8_t>({ 0x05 }));

    // At t=10 again — next packet has ts=15 > 10, so nullopt.
    EXPECT_FALSE(rep.next_packet(10.0).has_value());

    // At t=20 the second packet (ts=15) qualifies.
    const auto at20 = rep.next_packet(20.0);
    ASSERT_TRUE(at20.has_value());
    EXPECT_EQ(at20->payload, std::vector<std::uint8_t>({ 0x15 }));

    // At t=20, third packet (ts=25) does not qualify.
    EXPECT_FALSE(rep.next_packet(20.0).has_value());

    // At t=30, third packet (ts=25) qualifies.
    const auto at30 = rep.next_packet(30.0);
    ASSERT_TRUE(at30.has_value());
    EXPECT_EQ(at30->payload, std::vector<std::uint8_t>({ 0x25 }));

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// 7. Zero-packet recording saves and loads cleanly
// ---------------------------------------------------------------------------
TEST(SessionReplay, EmptyRecordingSaveAndLoad)
{
    const std::filesystem::path path = make_temp_path("empty");

    Recorder rec;
    rec.start_recording();
    rec.stop_recording();
    EXPECT_EQ(rec.packet_count(), 0U);
    ASSERT_TRUE(rec.save_to_file(path));

    Replayer rep;
    ASSERT_TRUE(rep.load_from_file(path));
    EXPECT_EQ(rep.all().size(), 0U);
    EXPECT_FALSE(rep.next_packet(9999.0).has_value());

    std::filesystem::remove(path);
}
