// =============================================================================
// CHROMODYNAMIC — samples/net/hello_session_replay
// Phase 613 — console proof that cd::net::session_replay is consumable as a
//             standalone library from an external sample.
//
// What the demo does:
//   Recorder phase:
//     - Constructs a Recorder, calls start_recording().
//     - Injects 5 fake packets with explicit binary payloads on channels 0-4.
//     - Calls stop_recording() and save_to_file() to a temp SRPK file.
//     - Verifies packet_count() == 5.
//   Replayer phase:
//     - Loads the SRPK file with load_from_file().
//     - Walks current_ms from 0 to 500 in 100 ms steps.
//     - At each step calls next_packet() until nullopt; prints every returned
//       packet (timestamp_ms, channel_id, incoming, payload hex).
//     - Verifies that all 5 packets were delivered.
//   Exits 0 on success.
//
// Console-only. No windows, no GPU. No hello_engine dependency.
// =============================================================================
#include <cd/net/session_replay/SessionReplay.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace
{

using cd::net::session_replay::PacketRecord;
using cd::net::session_replay::Recorder;
using cd::net::session_replay::Replayer;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Print a packet's metadata and up to 8 payload bytes as hex.
void print_packet(const PacketRecord& pkt) noexcept
{
    std::printf("    ts=%.3f ms  ch=%u  %s  payload[%zu]:",
                pkt.timestamp_ms,
                pkt.channel_id,
                pkt.incoming ? " in" : "out",
                pkt.payload.size());
    const std::size_t show = (pkt.payload.size() < 8U) ? pkt.payload.size() : 8U;
    for (std::size_t i = 0; i < show; ++i)
        std::printf(" %02X", static_cast<unsigned>(pkt.payload[i]));
    if (pkt.payload.size() > 8U)
        std::printf(" ...");
    std::printf("\n");
}

// Five distinct fake payloads – one per packet.
struct FakePacket
{
    std::uint32_t                channel_id;
    bool                         incoming;
    std::vector<std::uint8_t>    payload;
};

[[nodiscard]] std::vector<FakePacket> make_fake_packets()
{
    return {
        { 0, true,  { 0x01, 0x02, 0x03 }                               },
        { 1, false, { 0xDE, 0xAD, 0xBE, 0xEF }                         },
        { 2, true,  { 0xCA, 0xFE, 0xBA, 0xBE, 0x00, 0x01 }             },
        { 3, false, { 0xFF, 0xA0 }                                      },
        { 4, true,  { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80 } },
    };
}

}  // namespace

int main()
{
    std::printf("=== hello_session_replay — cd::net::session_replay consumable sample ===\n\n");

    const std::filesystem::path srp_path =
        std::filesystem::temp_directory_path() / "hello_session_replay_demo.srpk";

    // -------------------------------------------------------------------------
    // 1. Record 5 fake packets and save to disk.
    // -------------------------------------------------------------------------
    std::printf("--- Recorder phase ---\n");

    const auto fake_pkts = make_fake_packets();

    Recorder rec;
    rec.start_recording();

    for (const FakePacket& fp : fake_pkts)
    {
        rec.record_packet(fp.payload, fp.channel_id, fp.incoming);
        std::printf("  recorded ch=%u %s  payload[%zu]\n",
                    fp.channel_id,
                    fp.incoming ? " in" : "out",
                    fp.payload.size());
    }

    rec.stop_recording();

    const std::size_t recorded = rec.packet_count();
    std::printf("\n  packet_count() = %zu\n", recorded);

    if (recorded != fake_pkts.size())
    {
        std::fprintf(stderr, "ERROR: expected %zu packets, recorder has %zu.\n",
                     fake_pkts.size(), recorded);
        return 1;
    }

    if (!rec.save_to_file(srp_path))
    {
        std::fprintf(stderr, "ERROR: save_to_file(%s) failed.\n",
                     srp_path.string().c_str());
        return 1;
    }

    std::printf("  saved -> %s\n\n", srp_path.string().c_str());

    // -------------------------------------------------------------------------
    // 2. Load and replay with current_ms advancing by 100 ms steps.
    //    Because the Recorder stamps packets from steady_clock relative to
    //    start_recording(), all 5 packets will have very small timestamps
    //    (< 1 ms on any modern machine).  A walk starting at current_ms = 0
    //    and stepping by 100 ms therefore delivers all packets at the first
    //    qualifying step — demonstrating the time-gated next_packet() API.
    // -------------------------------------------------------------------------
    std::printf("--- Replayer phase ---\n");

    Replayer rep;
    if (!rep.load_from_file(srp_path))
    {
        std::fprintf(stderr, "ERROR: load_from_file(%s) failed.\n",
                     srp_path.string().c_str());
        return 1;
    }

    std::printf("  loaded %zu packets total.\n\n", rep.all().size());

    std::uint32_t delivered = 0;

    for (double current_ms = 0.0; current_ms <= 500.0; current_ms += 100.0)
    {
        // Drain all packets whose timestamp_ms <= current_ms.
        bool any = false;
        while (true)
        {
            auto pkt = rep.next_packet(current_ms);
            if (!pkt.has_value())
                break;
            if (!any)
            {
                std::printf("  current_ms = %.1f\n", current_ms);
                any = true;
            }
            print_packet(*pkt);
            ++delivered;
        }
    }

    std::printf("\n  delivered = %u / %zu\n", delivered, recorded);

    if (delivered != static_cast<std::uint32_t>(recorded))
    {
        std::fprintf(stderr, "ERROR: expected %zu delivered packets, got %u.\n",
                     recorded, delivered);
        return 1;
    }

    // -------------------------------------------------------------------------
    // 3. Cleanup temp file.
    // -------------------------------------------------------------------------
    std::filesystem::remove(srp_path);

    std::printf("\n[hello_session_replay] OK\n");
    return 0;
}
