// =============================================================================
// CHROMODYNAMIC — cd::net::session_replay tests
// Phase 600 / Sprint-2 close-out
//
// Tests: 28
//   --- Round-trip / basic API ---
//    1. RecordSaveLoadRoundTrip            — 2-packet record/save/load, meta exact
//    2. RecordingStoppedRejectsPacket      — record_packet ignored when not recording
//    3. ReplayReturnsNulloptAtEnd          — next_packet returns nullopt when exhausted
//    4. ResetRewindsPlayback               — reset() restores cursor to start
//    5. MalformedFileRejected              — bad magic → false
//    6. ReplayTimeGatedDelivery            — next_packet respects current_ms
//    7. EmptyRecordingSaveAndLoad          — zero-packet file round-trips cleanly
//    8. VersionMismatchRejected            — unsupported version → false
//    9. TruncatedRecordBodyRejected        — count > body bytes → false
//   10. MissingFileRejected               — non-existent path → false
//   --- Edge / negative ---
//   11. SinglePacketRoundTrip             — 1-packet session; bit-identical payload
//   12. LargeSessionRoundTrip             — 1000-packet stress; all payloads verified
//   13. EmptyPayloadPacketRoundTrip       — payload_size=0 in a multi-packet file
//   14. IsRecordingStateTransitions       — is_recording() tracks start/stop
//   15. AllOnFreshReplayer                — all() returns empty span before any load
//   16. PacketCountReplayer               — packet_count() matches loaded record count
//   17. FinishedFlagBehaviour             — finished() false until cursor exhausted
//   18. MultipleResetAndReplay            — two rewind-and-drain cycles identical
//   19. ChannelIdAndDirectionRoundTrip    — both incoming + outgoing across channels
//   --- Truncation at each SRPK field boundary ---
//   20. TruncatedAfterTimestamp           — body cut after 8 bytes of record 0
//   21. TruncatedAfterChannelId           — body cut after 8+4=12 bytes of record 0
//   22. TruncatedAfterIncomingByte        — body cut after 8+4+1=13 bytes of record 0
//   23. TruncatedAfterPayloadSize         — body cut after 8+4+1+4=17 bytes of record 0
//   24. TruncatedMidPayload               — payload_size=4, only 2 payload bytes written
//   --- Feature tests ---
//   25. SeekToSkipsPastPackets            — seek_to(t) lands cursor at >=t
//   26. SeekToPastEndGivesFinished        — seek_to past last ts → finished()
//   27. SeekToThenReset                   — seek then reset restores full replay
//   28. OutOfOrderTimestampsRejected      — load rejects non-monotonic timestamps
// =============================================================================
#include <cd/net/session_replay/SessionReplay.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{

using cd::net::session_replay::Recorder;
using cd::net::session_replay::Replayer;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

[[nodiscard]] std::filesystem::path make_temp_path(std::string_view suffix)
{
    return std::filesystem::temp_directory_path() /
           (std::string("cd_sr_test_") + std::string(suffix) + ".srpk");
}

[[nodiscard]] std::vector<std::uint8_t> make_payload(std::initializer_list<std::uint8_t> bytes)
{
    return {bytes};
}

// ---------------------------------------------------------------------------
// Synthetic file writer — exact mirror of SessionReplay.cpp wire format:
//   magic[4] | version(u32 LE) | count(u32 LE) |
//   for each: timestamp_ms(double LE) | channel_id(u32 LE) | incoming(u8) |
//             payload_size(u32 LE) | payload[payload_size]
// ---------------------------------------------------------------------------

struct SyntheticPacket
{
    double                    timestamp_ms;
    std::uint32_t             channel_id;
    bool                      incoming;
    std::vector<std::uint8_t> payload;
};

template<typename T>
void write_le(std::ofstream& out, const T& v)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    out.write(reinterpret_cast<const char*>(&v), static_cast<std::streamsize>(sizeof(T)));
}

[[nodiscard]] bool write_synthetic_file(const std::filesystem::path&        path,
                                        const std::vector<SyntheticPacket>& pkts)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        return false;

    const std::array<std::uint8_t, 4> magic { 0x53, 0x52, 0x50, 0x4B };
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    out.write(reinterpret_cast<const char*>(magic.data()),
              static_cast<std::streamsize>(magic.size()));

    write_le(out, std::uint32_t{1});  // version
    write_le(out, static_cast<std::uint32_t>(pkts.size()));

    for (const SyntheticPacket& p : pkts)
    {
        write_le(out, p.timestamp_ms);
        write_le(out, p.channel_id);
        const auto incoming_byte = p.incoming ? std::uint8_t{1} : std::uint8_t{0};
        write_le(out, incoming_byte);
        write_le(out, static_cast<std::uint32_t>(p.payload.size()));
        if (!p.payload.empty())
        {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            out.write(reinterpret_cast<const char*>(p.payload.data()),
                      static_cast<std::streamsize>(p.payload.size()));
        }
    }
    return out.good();
}

// Write just the SRPK header (magic + version + count) with no record bodies.
void write_header_only(const std::filesystem::path& path,
                       std::uint32_t                version,
                       std::uint32_t                count)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    const std::array<std::uint8_t, 4> magic { 0x53, 0x52, 0x50, 0x4B };
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    out.write(reinterpret_cast<const char*>(magic.data()),
              static_cast<std::streamsize>(magic.size()));
    write_le(out, version);
    write_le(out, count);
}

// Write a partial record body stopping after `stop_after_bytes` bytes of the
// first record (used by truncation tests 20–23).
void write_header_plus_partial_record(const std::filesystem::path& path,
                                      std::size_t                  stop_after_bytes)
{
    // Full first-record body: timestamp(8) | channel(4) | incoming(1) | size(4) | payload(N)
    // We write a zero-payload record so the field layout is:
    //   offset 0..7  : timestamp_ms = 1.0
    //   offset 8..11 : channel_id   = 0
    //   offset 12    : incoming      = 1
    //   offset 13..16: payload_size = 0
    // Total without payload: 17 bytes.
    constexpr std::size_t kRecordHeaderBytes { 17 };
    const std::array<std::uint8_t, kRecordHeaderBytes> full_record_hdr = []()
    {
        std::array<std::uint8_t, kRecordHeaderBytes> buf {};
        double ts_ms { 1.0 };
        std::uint32_t ch_id { 0 };
        std::uint8_t  inc   { 1 };
        std::uint32_t psz   { 0 };
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        std::memcpy(buf.data(),      &ts_ms,  sizeof(ts_ms));
        std::memcpy(buf.data() + 8,  &ch_id,  sizeof(ch_id));
        std::memcpy(buf.data() + 12, &inc,    sizeof(inc));
        std::memcpy(buf.data() + 13, &psz,    sizeof(psz));
        return buf;
    }();

    const std::size_t bytes_to_write = std::min(stop_after_bytes, kRecordHeaderBytes);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    const std::array<std::uint8_t, 4> magic { 0x53, 0x52, 0x50, 0x4B };
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    out.write(reinterpret_cast<const char*>(magic.data()),
              static_cast<std::streamsize>(magic.size()));
    write_le(out, std::uint32_t{1});  // version
    write_le(out, std::uint32_t{1});  // count = 1
    out.write(reinterpret_cast<const char*>(full_record_hdr.data()),  // NOLINT
              static_cast<std::streamsize>(bytes_to_write));
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. Record + save + load round-trip
// ---------------------------------------------------------------------------
TEST(SessionReplay, RecordSaveLoadRoundTrip)
{
    const auto path = make_temp_path("roundtrip");

    const std::vector<std::uint8_t> p1 = make_payload({ 0xDE, 0xAD });
    const std::vector<std::uint8_t> p2 = make_payload({ 0xBE, 0xEF, 0xFF });

    Recorder rec;
    rec.start_recording();
    rec.record_packet(p1, /*channel_id=*/1, /*incoming=*/true);
    rec.record_packet(p2, /*channel_id=*/2, /*incoming=*/false);
    rec.stop_recording();

    ASSERT_EQ(rec.packet_count(), 2U);
    ASSERT_TRUE(rec.save_to_file(path));

    Replayer rep;
    ASSERT_TRUE(rep.load_from_file(path));

    const auto all = rep.all();
    ASSERT_EQ(all.size(), 2U);

    EXPECT_EQ(all[0].channel_id, 1U);
    EXPECT_TRUE(all[0].incoming);
    EXPECT_EQ(all[0].payload, p1);

    EXPECT_EQ(all[1].channel_id, 2U);
    EXPECT_FALSE(all[1].incoming);
    EXPECT_EQ(all[1].payload, p2);

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
    const std::vector<std::uint8_t> data = make_payload({ 0x01, 0x02, 0x03 });
    rec.record_packet(data, 0, true);
    EXPECT_EQ(rec.packet_count(), 0U);

    rec.start_recording();
    rec.record_packet(data, 0, true);
    EXPECT_EQ(rec.packet_count(), 1U);
    rec.stop_recording();
    rec.record_packet(data, 0, true);
    EXPECT_EQ(rec.packet_count(), 1U);
}

// ---------------------------------------------------------------------------
// 3. next_packet returns nullopt after all packets are consumed
// ---------------------------------------------------------------------------
TEST(SessionReplay, ReplayReturnsNulloptAtEnd)
{
    const auto path = make_temp_path("nullopt");
    ASSERT_TRUE(write_synthetic_file(path, {
        SyntheticPacket { 1.0, 0, true, make_payload({ 0xAA }) }
    }));

    Replayer rep;
    ASSERT_TRUE(rep.load_from_file(path));

    EXPECT_TRUE(rep.next_packet(100.0).has_value());
    EXPECT_FALSE(rep.next_packet(100.0).has_value());

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// 4. reset() rewinds the playback cursor
// ---------------------------------------------------------------------------
TEST(SessionReplay, ResetRewindsPlayback)
{
    const auto path = make_temp_path("reset");
    ASSERT_TRUE(write_synthetic_file(path, {
        SyntheticPacket { 1.0, 0, true,  make_payload({ 0x10 }) },
        SyntheticPacket { 2.0, 0, false, make_payload({ 0x20 }) }
    }));

    Replayer rep;
    ASSERT_TRUE(rep.load_from_file(path));

    EXPECT_TRUE(rep.next_packet(999.0).has_value());
    EXPECT_TRUE(rep.next_packet(999.0).has_value());
    EXPECT_FALSE(rep.next_packet(999.0).has_value());

    rep.reset();
    const auto after_reset = rep.next_packet(999.0);
    ASSERT_TRUE(after_reset.has_value());
    EXPECT_EQ(after_reset->payload, std::vector<std::uint8_t>({ 0x10 }));

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// 5. Bad magic bytes are rejected
// ---------------------------------------------------------------------------
TEST(SessionReplay, MalformedFileRejected)
{
    const auto path = make_temp_path("malformed");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.is_open());
        const std::array<std::uint8_t, 12> garbage {
            0xFF, 0xFF, 0xFF, 0xFF,
            0x01, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00
        };
        out.write(reinterpret_cast<const char*>(garbage.data()),  // NOLINT
                  static_cast<std::streamsize>(garbage.size()));
    }

    Replayer rep;
    EXPECT_FALSE(rep.load_from_file(path));
    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// 6. next_packet respects the current_ms threshold
// ---------------------------------------------------------------------------
TEST(SessionReplay, ReplayTimeGatedDelivery)
{
    const auto path = make_temp_path("timegated");
    ASSERT_TRUE(write_synthetic_file(path, {
        SyntheticPacket {  5.0, 0, true, make_payload({ 0x05 }) },
        SyntheticPacket { 15.0, 0, true, make_payload({ 0x15 }) },
        SyntheticPacket { 25.0, 0, true, make_payload({ 0x25 }) }
    }));

    Replayer rep;
    ASSERT_TRUE(rep.load_from_file(path));

    const auto at10 = rep.next_packet(10.0);
    ASSERT_TRUE(at10.has_value());
    EXPECT_EQ(at10->payload, std::vector<std::uint8_t>({ 0x05 }));

    EXPECT_FALSE(rep.next_packet(10.0).has_value());

    const auto at20 = rep.next_packet(20.0);
    ASSERT_TRUE(at20.has_value());
    EXPECT_EQ(at20->payload, std::vector<std::uint8_t>({ 0x15 }));

    EXPECT_FALSE(rep.next_packet(20.0).has_value());

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
    const auto path = make_temp_path("empty");

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

// ---------------------------------------------------------------------------
// 8. Unsupported version is rejected
// ---------------------------------------------------------------------------
TEST(SessionReplay, VersionMismatchRejected)
{
    const auto path = make_temp_path("version");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.is_open());
        const std::array<std::uint8_t, 12> bytes {
            0x53, 0x52, 0x50, 0x4B,
            0x02, 0x00, 0x00, 0x00,  // version = 2
            0x00, 0x00, 0x00, 0x00   // count = 0
        };
        out.write(reinterpret_cast<const char*>(bytes.data()),  // NOLINT
                  static_cast<std::streamsize>(bytes.size()));
    }

    Replayer rep;
    EXPECT_FALSE(rep.load_from_file(path))
        << "load_from_file must reject an unsupported format version";
    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// 9. Header claims N records but body is empty
// ---------------------------------------------------------------------------
TEST(SessionReplay, TruncatedRecordBodyRejected)
{
    const auto path = make_temp_path("truncated");
    write_header_only(path, /*version=*/1, /*count=*/3);

    Replayer rep;
    EXPECT_FALSE(rep.load_from_file(path))
        << "load_from_file must reject a truncated record body";
    EXPECT_EQ(rep.all().size(), 0U);
    EXPECT_FALSE(rep.next_packet(9999.0).has_value());

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// 10. Non-existent path returns false
// ---------------------------------------------------------------------------
TEST(SessionReplay, MissingFileRejected)
{
    const auto path = make_temp_path("does_not_exist_4f2c9a");
    std::filesystem::remove(path);

    Replayer rep;
    EXPECT_FALSE(rep.load_from_file(path));
}

// ---------------------------------------------------------------------------
// 11. Single-packet session — bit-identical payload round-trip
// ---------------------------------------------------------------------------
TEST(SessionReplay, SinglePacketRoundTrip)
{
    const auto path = make_temp_path("single");
    const std::vector<std::uint8_t> expected = { 0xCA, 0xFE, 0xBA, 0xBE };

    Recorder rec;
    rec.start_recording();
    rec.record_packet(expected, /*channel_id=*/7, /*incoming=*/false);
    rec.stop_recording();
    ASSERT_TRUE(rec.save_to_file(path));

    Replayer rep;
    ASSERT_TRUE(rep.load_from_file(path));
    ASSERT_EQ(rep.packet_count(), 1U);

    const auto pkt = rep.next_packet(1e9);
    ASSERT_TRUE(pkt.has_value());
    EXPECT_EQ(pkt->payload,    expected);
    EXPECT_EQ(pkt->channel_id, 7U);
    EXPECT_FALSE(pkt->incoming);

    EXPECT_FALSE(rep.next_packet(1e9).has_value());

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// 12. Large session (1000 packets) — all payloads verified after round-trip
// ---------------------------------------------------------------------------
TEST(SessionReplay, LargeSessionRoundTrip)
{
    const auto path = make_temp_path("large");

    constexpr std::size_t kCount { 1000 };
    std::vector<SyntheticPacket> pkts;
    pkts.reserve(kCount);
    for (std::size_t i = 0; i < kCount; ++i)
    {
        const auto ts = static_cast<double>(i) * 0.5;  // 0.0, 0.5, 1.0, ...
        const auto ch = static_cast<std::uint32_t>(i % 16);
        const auto payload_byte = static_cast<std::uint8_t>(i & 0xFFU);
        pkts.push_back(SyntheticPacket { ts, ch, (i % 2) == 0, { payload_byte } });
    }
    ASSERT_TRUE(write_synthetic_file(path, pkts));

    Replayer rep;
    ASSERT_TRUE(rep.load_from_file(path));
    ASSERT_EQ(rep.packet_count(), kCount);

    const auto all = rep.all();
    for (std::size_t i = 0; i < kCount; ++i)
    {
        const auto expected_byte = static_cast<std::uint8_t>(i & 0xFFU);
        ASSERT_EQ(all[i].payload.size(), 1U);
        ASSERT_EQ(all[i].payload[0],     expected_byte) << "mismatch at index " << i;
        ASSERT_EQ(all[i].channel_id,
                  static_cast<std::uint32_t>(i % 16));
    }

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// 13. Empty-payload packet within a multi-packet file round-trips cleanly
// ---------------------------------------------------------------------------
TEST(SessionReplay, EmptyPayloadPacketRoundTrip)
{
    const auto path = make_temp_path("empty_payload");
    ASSERT_TRUE(write_synthetic_file(path, {
        SyntheticPacket { 1.0, 0, true,  {} },                       // empty payload
        SyntheticPacket { 2.0, 1, false, make_payload({ 0xAB }) },   // non-empty
        SyntheticPacket { 3.0, 2, true,  {} }                        // empty again
    }));

    Replayer rep;
    ASSERT_TRUE(rep.load_from_file(path));
    ASSERT_EQ(rep.packet_count(), 3U);

    const auto all = rep.all();
    EXPECT_TRUE(all[0].payload.empty());
    EXPECT_EQ(all[1].payload, std::vector<std::uint8_t>({ 0xAB }));
    EXPECT_TRUE(all[2].payload.empty());

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// 14. is_recording() tracks start/stop state transitions
// ---------------------------------------------------------------------------
TEST(SessionReplay, IsRecordingStateTransitions)
{
    Recorder rec;
    EXPECT_FALSE(rec.is_recording());

    rec.start_recording();
    EXPECT_TRUE(rec.is_recording());

    rec.stop_recording();
    EXPECT_FALSE(rec.is_recording());

    // Second start clears and re-arms.
    rec.start_recording();
    EXPECT_TRUE(rec.is_recording());
    rec.stop_recording();
    EXPECT_FALSE(rec.is_recording());
}

// ---------------------------------------------------------------------------
// 15. all() on a fresh Replayer returns an empty span
// ---------------------------------------------------------------------------
TEST(SessionReplay, AllOnFreshReplayer)
{
    Replayer rep;
    EXPECT_EQ(rep.all().size(), 0U);
    EXPECT_TRUE(rep.all().empty());
}

// ---------------------------------------------------------------------------
// 16. packet_count() on Replayer matches the loaded record count
// ---------------------------------------------------------------------------
TEST(SessionReplay, PacketCountReplayer)
{
    const auto path = make_temp_path("pcount");
    ASSERT_TRUE(write_synthetic_file(path, {
        SyntheticPacket { 1.0, 0, true,  make_payload({ 0x01 }) },
        SyntheticPacket { 2.0, 0, false, make_payload({ 0x02 }) },
        SyntheticPacket { 3.0, 0, true,  make_payload({ 0x03 }) }
    }));

    Replayer rep;
    ASSERT_TRUE(rep.load_from_file(path));
    EXPECT_EQ(rep.packet_count(), 3U);

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// 17. finished() is false until all packets are consumed, then true
// ---------------------------------------------------------------------------
TEST(SessionReplay, FinishedFlagBehaviour)
{
    const auto path = make_temp_path("finished");
    ASSERT_TRUE(write_synthetic_file(path, {
        SyntheticPacket { 1.0, 0, true, make_payload({ 0x10 }) },
        SyntheticPacket { 2.0, 0, true, make_payload({ 0x20 }) }
    }));

    Replayer rep;
    ASSERT_TRUE(rep.load_from_file(path));

    EXPECT_FALSE(rep.finished());
    static_cast<void>(rep.next_packet(999.0));
    EXPECT_FALSE(rep.finished());
    static_cast<void>(rep.next_packet(999.0));
    EXPECT_TRUE(rep.finished());

    rep.reset();
    EXPECT_FALSE(rep.finished());

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// 18. Two rewind-and-drain cycles yield identical packet sequences
// ---------------------------------------------------------------------------
TEST(SessionReplay, MultipleResetAndReplay)
{
    const auto path = make_temp_path("multireset");
    ASSERT_TRUE(write_synthetic_file(path, {
        SyntheticPacket { 1.0, 3, true,  make_payload({ 0xAA }) },
        SyntheticPacket { 2.0, 7, false, make_payload({ 0xBB }) }
    }));

    Replayer rep;
    ASSERT_TRUE(rep.load_from_file(path));

    auto drain = [&rep]() -> std::vector<std::uint8_t>
    {
        std::vector<std::uint8_t> collected;
        while (true)
        {
            const auto pkt = rep.next_packet(9999.0);
            if (!pkt.has_value())
                break;
            collected.push_back(pkt->payload[0]);
        }
        return collected;
    };

    const auto run1 = drain();
    rep.reset();
    const auto run2 = drain();

    EXPECT_EQ(run1, run2);
    EXPECT_EQ(run1, (std::vector<std::uint8_t>{ 0xAA, 0xBB }));

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// 19. channel_id and incoming direction survive round-trip for both values
// ---------------------------------------------------------------------------
TEST(SessionReplay, ChannelIdAndDirectionRoundTrip)
{
    const auto path = make_temp_path("chdir");
    ASSERT_TRUE(write_synthetic_file(path, {
        SyntheticPacket { 0.0,  0xDEAD'BEEF, true,  make_payload({ 0x01 }) },
        SyntheticPacket { 1.0,  0x0000'0001, false, make_payload({ 0x02 }) }
    }));

    Replayer rep;
    ASSERT_TRUE(rep.load_from_file(path));
    const auto all = rep.all();
    ASSERT_EQ(all.size(), 2U);

    EXPECT_EQ(all[0].channel_id, 0xDEAD'BEEF);
    EXPECT_TRUE(all[0].incoming);

    EXPECT_EQ(all[1].channel_id, 0x0000'0001U);
    EXPECT_FALSE(all[1].incoming);

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// 20. Truncated after timestamp field (8 bytes into record body)
// ---------------------------------------------------------------------------
TEST(SessionReplay, TruncatedAfterTimestamp)
{
    const auto path = make_temp_path("trunc_ts");
    // Write 8 bytes of the first record (just the double timestamp).
    write_header_plus_partial_record(path, /*stop_after_bytes=*/8);

    Replayer rep;
    EXPECT_FALSE(rep.load_from_file(path))
        << "must reject stream truncated after timestamp field";
    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// 21. Truncated after channel_id field (8+4 = 12 bytes into record body)
// ---------------------------------------------------------------------------
TEST(SessionReplay, TruncatedAfterChannelId)
{
    const auto path = make_temp_path("trunc_ch");
    write_header_plus_partial_record(path, /*stop_after_bytes=*/12);

    Replayer rep;
    EXPECT_FALSE(rep.load_from_file(path))
        << "must reject stream truncated after channel_id field";
    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// 22. Truncated after incoming byte (8+4+1 = 13 bytes into record body)
// ---------------------------------------------------------------------------
TEST(SessionReplay, TruncatedAfterIncomingByte)
{
    const auto path = make_temp_path("trunc_inc");
    write_header_plus_partial_record(path, /*stop_after_bytes=*/13);

    Replayer rep;
    EXPECT_FALSE(rep.load_from_file(path))
        << "must reject stream truncated after incoming byte";
    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// 23. Truncated after payload_size field (8+4+1+4 = 17 bytes into record body)
//
// Note: for a payload_size=0 record the 17-byte record IS the complete record,
// so we write a record with payload_size=4 but no payload bytes.
// ---------------------------------------------------------------------------
TEST(SessionReplay, TruncatedAfterPayloadSize)
{
    const auto path = make_temp_path("trunc_psz");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        const std::array<std::uint8_t, 4> magic { 0x53, 0x52, 0x50, 0x4B };
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        out.write(reinterpret_cast<const char*>(magic.data()),
                  static_cast<std::streamsize>(magic.size()));
        write_le(out, std::uint32_t{1});  // version
        write_le(out, std::uint32_t{1});  // count = 1

        // One record: timestamp(8) + channel(4) + incoming(1) + payload_size=4(4)
        // but NO payload bytes follow → payload read will fail.
        double        ts  { 1.0 };
        std::uint32_t ch  { 0 };
        std::uint8_t  inc { 1 };
        std::uint32_t psz { 4 };  // claims 4 bytes but we write none
        write_le(out, ts);
        write_le(out, ch);
        write_le(out, inc);
        write_le(out, psz);
        // Intentionally write NO payload bytes.
    }

    Replayer rep;
    EXPECT_FALSE(rep.load_from_file(path))
        << "must reject stream where payload is absent despite payload_size > 0";
    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// 24. Partial payload — payload_size=4 but only 2 bytes written
// ---------------------------------------------------------------------------
TEST(SessionReplay, TruncatedMidPayload)
{
    const auto path = make_temp_path("trunc_mid");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        const std::array<std::uint8_t, 4> magic { 0x53, 0x52, 0x50, 0x4B };
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        out.write(reinterpret_cast<const char*>(magic.data()),
                  static_cast<std::streamsize>(magic.size()));
        write_le(out, std::uint32_t{1});  // version
        write_le(out, std::uint32_t{1});  // count = 1

        double        ts  { 1.0 };
        std::uint32_t ch  { 0 };
        std::uint8_t  inc { 1 };
        std::uint32_t psz { 4 };  // claims 4 bytes
        write_le(out, ts);
        write_le(out, ch);
        write_le(out, inc);
        write_le(out, psz);
        // Write only 2 of the 4 claimed payload bytes.
        const std::array<std::uint8_t, 2> partial { 0xAA, 0xBB };
        out.write(reinterpret_cast<const char*>(partial.data()),  // NOLINT
                  static_cast<std::streamsize>(partial.size()));
    }

    Replayer rep;
    EXPECT_FALSE(rep.load_from_file(path))
        << "must reject stream with fewer payload bytes than payload_size";
    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// 25. seek_to() skips packets before target_ms and resumes from >= target
// ---------------------------------------------------------------------------
TEST(SessionReplay, SeekToSkipsPastPackets)
{
    const auto path = make_temp_path("seek");
    ASSERT_TRUE(write_synthetic_file(path, {
        SyntheticPacket {  5.0, 0, true, make_payload({ 0x05 }) },
        SyntheticPacket { 10.0, 0, true, make_payload({ 0x10 }) },
        SyntheticPacket { 15.0, 0, true, make_payload({ 0x15 }) },
        SyntheticPacket { 20.0, 0, true, make_payload({ 0x20 }) }
    }));

    Replayer rep;
    ASSERT_TRUE(rep.load_from_file(path));

    // Seek to 12 ms — should land on the packet at ts=15 (first >= 12).
    rep.seek_to(12.0);
    EXPECT_FALSE(rep.finished());

    const auto pkt = rep.next_packet(9999.0);
    ASSERT_TRUE(pkt.has_value());
    EXPECT_EQ(pkt->payload, std::vector<std::uint8_t>({ 0x15 }));

    // Next call should return ts=20.
    const auto pkt2 = rep.next_packet(9999.0);
    ASSERT_TRUE(pkt2.has_value());
    EXPECT_EQ(pkt2->payload, std::vector<std::uint8_t>({ 0x20 }));

    EXPECT_FALSE(rep.next_packet(9999.0).has_value());

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// 26. seek_to() past the last timestamp sets finished()
// ---------------------------------------------------------------------------
TEST(SessionReplay, SeekToPastEndGivesFinished)
{
    const auto path = make_temp_path("seek_past");
    ASSERT_TRUE(write_synthetic_file(path, {
        SyntheticPacket { 1.0, 0, true, make_payload({ 0x01 }) },
        SyntheticPacket { 2.0, 0, true, make_payload({ 0x02 }) }
    }));

    Replayer rep;
    ASSERT_TRUE(rep.load_from_file(path));

    rep.seek_to(9999.0);
    EXPECT_TRUE(rep.finished());
    EXPECT_FALSE(rep.next_packet(9999.0).has_value());

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// 27. seek_to then reset restores full playback from the beginning
// ---------------------------------------------------------------------------
TEST(SessionReplay, SeekToThenReset)
{
    const auto path = make_temp_path("seek_reset");
    ASSERT_TRUE(write_synthetic_file(path, {
        SyntheticPacket { 1.0, 0, true, make_payload({ 0x01 }) },
        SyntheticPacket { 2.0, 0, true, make_payload({ 0x02 }) },
        SyntheticPacket { 3.0, 0, true, make_payload({ 0x03 }) }
    }));

    Replayer rep;
    ASSERT_TRUE(rep.load_from_file(path));

    // Seek past all packets.
    rep.seek_to(9999.0);
    EXPECT_TRUE(rep.finished());

    // Reset must bring us back to the very first packet.
    rep.reset();
    EXPECT_FALSE(rep.finished());
    const auto first = rep.next_packet(9999.0);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->payload, std::vector<std::uint8_t>({ 0x01 }));

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// 28. Out-of-order timestamps are rejected at load time
// ---------------------------------------------------------------------------
TEST(SessionReplay, OutOfOrderTimestampsRejected)
{
    const auto path = make_temp_path("ooo");
    // Third packet has timestamp lower than the second — violates monotonicity.
    ASSERT_TRUE(write_synthetic_file(path, {
        SyntheticPacket {  1.0, 0, true, make_payload({ 0x01 }) },
        SyntheticPacket { 10.0, 0, true, make_payload({ 0x0A }) },
        SyntheticPacket {  5.0, 0, true, make_payload({ 0x05 }) }  // out of order
    }));

    Replayer rep;
    EXPECT_FALSE(rep.load_from_file(path))
        << "load_from_file must reject non-monotonic timestamps";
    EXPECT_EQ(rep.all().size(), 0U);
    EXPECT_FALSE(rep.next_packet(9999.0).has_value());

    std::filesystem::remove(path);
}
