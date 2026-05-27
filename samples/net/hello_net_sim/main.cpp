// =============================================================================
// CHROMODYNAMIC — samples/hello_net_sim
//
// Headless multiplayer-replication simulator. Two virtual endpoints
// ("server" and "client") exchange entity state through a lossy/jittery
// channel for 2 simulated seconds, wiring four marathon primitives:
//
//   cd::net::SnapshotBuffer   — client-side interpolation (Phase 64)
//   cd::net::DeltaWriter      — byte-level delta compression (Phase 52)
//   cd::net::LatencyStats     — EWMA RTT + jitter (Phase 84, RFC 6298)
//   cd::net::Throttle         — token-bucket outbound rate limiter (Phase 92)
//
// What the demo does:
//   - server simulates an entity orbiting around origin
//   - it ticks state at 60 Hz, attempts to send at most 30 packets/sec
//     (Throttle drops the rest deterministically)
//   - 10% of sent packets are "dropped" by the channel
//   - sent packets are delta-encoded against the previous baseline
//   - delivered packets travel with 30-90 ms variable latency
//   - client buffers snapshots, samples at render_time = t - 100 ms
//     (classic client-interpolation delay)
//   - per second, prints: pkts sent / received / dropped, total bytes
//     before vs after delta, RTT EWMA, jitter, interp lookup count
//
// Output: console-only summary table. No files written.
// =============================================================================
#include <cd/net/DeltaWriter.hpp>
#include <cd/net/LatencyStats.hpp>
#include <cd/net/SnapshotBuffer.hpp>
#include <cd/net/Throttle.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <span>
#include <vector>

namespace
{

// 32-byte entity state: pos.xyz, rot.xyzw, vel.xyz (10 floats + 8 B pad).
struct EntityState
{
    float pos[3];
    float rot[4];
    float vel[3];
    float pad[2];
};
static_assert(sizeof(EntityState) == 48, "EntityState must be 48 bytes");

[[nodiscard]] EntityState operator+(const EntityState& a, const EntityState& b) noexcept
{
    EntityState r {};
    for (int i = 0; i < 3; ++i) r.pos[i] = a.pos[i] + b.pos[i];
    for (int i = 0; i < 4; ++i) r.rot[i] = a.rot[i] + b.rot[i];
    for (int i = 0; i < 3; ++i) r.vel[i] = a.vel[i] + b.vel[i];
    return r;
}

[[nodiscard]] EntityState operator*(const EntityState& a, float s) noexcept
{
    EntityState r {};
    for (int i = 0; i < 3; ++i) r.pos[i] = a.pos[i] * s;
    for (int i = 0; i < 4; ++i) r.rot[i] = a.rot[i] * s;
    for (int i = 0; i < 3; ++i) r.vel[i] = a.vel[i] * s;
    return r;
}

[[nodiscard]] EntityState orbit_state(double t) noexcept
{
    EntityState s {};
    const auto ft = static_cast<float>(t);
    s.pos[0] = 5.0F * std::cos(ft * 1.2F);
    s.pos[1] = 1.5F;
    s.pos[2] = 5.0F * std::sin(ft * 1.2F);
    s.rot[3] = 1.0F;  // identity-ish
    s.vel[0] = -6.0F * std::sin(ft * 1.2F);
    s.vel[2] =  6.0F * std::cos(ft * 1.2F);
    return s;
}

// Deterministic xorshift32 PRNG so runs match across machines.
struct Rng
{
    std::uint32_t s { 0xC0FFEE42u };
    std::uint32_t next() noexcept
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s;
    }
    [[nodiscard]] float uniform() noexcept
    {
        return static_cast<float>(next()) / static_cast<float>(0xFFFFFFFFu);
    }
};

struct ChannelPacket
{
    double                 deliver_at;
    std::uint32_t          seq;
    std::vector<std::byte> bytes;
};

}  // namespace

int main()
{
    std::printf("=== hello_net_sim — SnapshotBuffer + DeltaWriter + LatencyStats + Throttle ===\n");

    // Wall-clock simulation, 2 seconds at 1 ms granularity.
    constexpr double kSimDuration = 2.0;
    constexpr double kDt          = 1.0 / 1000.0;
    constexpr double kTickRate    = 60.0;
    constexpr double kInterpDelay = 0.100;
    constexpr float  kSendRate    = 30.0F;          // pkts/sec ceiling
    constexpr float  kDropRate    = 0.10F;
    constexpr double kLatMin      = 0.030;
    constexpr double kLatMax      = 0.090;

    cd::net::Throttle               out_throttle { /*capacity=*/4.0F, /*rate=*/kSendRate };
    cd::net::SnapshotBuffer<EntityState> client_buf;
    cd::net::LatencyStats           rtt_stats;
    Rng                             rng;

    std::deque<ChannelPacket>       in_flight;
    EntityState                     server_baseline {};   // last acked state
    bool                            have_baseline { false };
    EntityState                     client_baseline {};
    double                          next_server_tick { 0.0 };

    std::uint32_t pkts_sent     = 0;
    std::uint32_t pkts_dropped  = 0;
    std::uint32_t pkts_received = 0;
    std::uint64_t raw_bytes     = 0;
    std::uint64_t wire_bytes    = 0;
    std::uint32_t sample_calls  = 0;
    std::uint32_t next_seq      = 0;

    std::printf("\n  t  | sent | recv | drop | raw B | wire B | RTT (ms) | jitter (ms) | interp\n");
    std::printf(" ----+------+------+------+-------+--------+----------+-------------+--------\n");

    double next_report = 1.0;
    std::uint32_t prev_sent = 0;
    std::uint32_t prev_recv = 0;
    std::uint32_t prev_drop = 0;
    std::uint64_t prev_raw  = 0;
    std::uint64_t prev_wire = 0;
    std::uint32_t prev_samp = 0;

    for (double t = 0.0; t <= kSimDuration; t += kDt)
    {
        out_throttle.update(static_cast<float>(kDt));

        // Server tick: produce a fresh state at 60 Hz, try to send.
        if (t >= next_server_tick)
        {
            next_server_tick += 1.0 / kTickRate;
            const EntityState current = orbit_state(t);

            if (out_throttle.try_consume(1.0F))
            {
                ++pkts_sent;

                std::vector<std::byte> payload;
                std::span<const std::byte> cur_span {
                    reinterpret_cast<const std::byte*>(&current), sizeof(EntityState) };
                raw_bytes += sizeof(EntityState);
                if (!have_baseline)
                {
                    payload.assign(cur_span.begin(), cur_span.end());
                    have_baseline = true;
                }
                else
                {
                    std::span<const std::byte> base_span {
                        reinterpret_cast<const std::byte*>(&server_baseline),
                        sizeof(EntityState) };
                    payload = cd::net::write_delta(base_span, cur_span);
                }
                server_baseline = current;
                wire_bytes += payload.size();

                // 10 % loss.
                if (rng.uniform() < kDropRate)
                {
                    ++pkts_dropped;
                }
                else
                {
                    const double lat = kLatMin
                        + (kLatMax - kLatMin) * static_cast<double>(rng.uniform());
                    ChannelPacket p { /*deliver_at=*/t + lat, /*seq=*/next_seq, std::move(payload) };
                    in_flight.push_back(std::move(p));
                    // Crude RTT proxy: assume ack returns symmetrically.
                    rtt_stats.record(static_cast<std::uint32_t>(lat * 2.0 * 1e6));
                }
                ++next_seq;
            }
        }

        // Channel delivery — anything whose deliver_at is in the past
        // arrives at the client now.
        while (!in_flight.empty() && in_flight.front().deliver_at <= t)
        {
            auto pkt = std::move(in_flight.front());
            in_flight.pop_front();
            ++pkts_received;

            EntityState received = client_baseline;
            std::span<std::byte> tgt {
                reinterpret_cast<std::byte*>(&received), sizeof(EntityState) };
            // First packet is full payload, not a delta.
            if (pkt.bytes.size() == sizeof(EntityState))
                std::memcpy(&received, pkt.bytes.data(), sizeof(EntityState));
            else
                (void)cd::net::apply_delta(tgt, std::span<const std::byte>(pkt.bytes));
            client_baseline = received;
            client_buf.push(t, received);
        }

        // Client interpolation tick at the same 1 kHz cadence as the
        // simulator. The actual render thread would do this once per
        // frame; we sample every ms so we exercise the API.
        const double render_t = t - kInterpDelay;
        if (render_t > 0.0)
        {
            auto s = client_buf.sample(render_t);
            if (s.has_value()) ++sample_calls;
        }

        // Per-second readout.
        if (t >= next_report - 1e-9)
        {
            const std::uint32_t sent_d = pkts_sent     - prev_sent;
            const std::uint32_t recv_d = pkts_received - prev_recv;
            const std::uint32_t drop_d = pkts_dropped  - prev_drop;
            const std::uint64_t raw_d  = raw_bytes     - prev_raw;
            const std::uint64_t wire_d = wire_bytes    - prev_wire;
            const std::uint32_t samp_d = sample_calls  - prev_samp;
            std::printf("  %1.0fs |  %3u |  %3u |  %3u | %5llu | %6llu | %8.2f | %11.2f | %6u\n",
                        next_report,
                        sent_d, recv_d, drop_d,
                        static_cast<unsigned long long>(raw_d),
                        static_cast<unsigned long long>(wire_d),
                        static_cast<double>(rtt_stats.current_rtt_us()) / 1000.0,
                        static_cast<double>(rtt_stats.jitter_us()) / 1000.0,
                        samp_d);
            prev_sent = pkts_sent;
            prev_recv = pkts_received;
            prev_drop = pkts_dropped;
            prev_raw  = raw_bytes;
            prev_wire = wire_bytes;
            prev_samp = sample_calls;
            next_report += 1.0;
        }

        client_buf.drop_older_than(t - 0.5);
    }

    const double ratio = (raw_bytes > 0)
        ? (100.0 * static_cast<double>(wire_bytes) / static_cast<double>(raw_bytes))
        : 0.0;

    std::printf("\nsummary:\n");
    std::printf("  sent     %u packets   (server tick %.0f Hz, throttle cap %.0f pkt/s)\n",
                pkts_sent, kTickRate, static_cast<double>(kSendRate));
    std::printf("  received %u packets   (%.1f%% delivery)\n",
                pkts_received,
                pkts_sent == 0 ? 0.0 : (100.0 * pkts_received / pkts_sent));
    std::printf("  dropped  %u packets   (channel loss %.0f%%)\n",
                pkts_dropped, static_cast<double>(kDropRate * 100.0F));
    std::printf("  raw   %llu B  ->  wire %llu B  (%.1f%% of raw via delta)\n",
                static_cast<unsigned long long>(raw_bytes),
                static_cast<unsigned long long>(wire_bytes),
                ratio);
    std::printf("  RTT      EWMA %.2f ms  jitter %.2f ms  (min %u us, max %u us, n=%llu)\n",
                static_cast<double>(rtt_stats.current_rtt_us()) / 1000.0,
                static_cast<double>(rtt_stats.jitter_us()) / 1000.0,
                rtt_stats.min_us(), rtt_stats.max_us(),
                static_cast<unsigned long long>(rtt_stats.sample_count()));
    std::printf("  interp   %u sample() calls hit a non-empty SnapshotBuffer\n",
                sample_calls);
    std::printf("[hello_net_sim] OK\n");
    return 0;
}
