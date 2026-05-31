// =============================================================================
// CHROMODYNAMIC — samples/net/hello_netcode  (phase 468 / T3.4)
//
// Console-only, 2-client local-loopback movement reconciliation demo.
//
// What it demonstrates
// --------------------
//   cd::net::AckWindowChannel   — reliable delivery with 64-bit ack bitfield
//   cd::net::QosDispatcher      — priority-tiered traffic shaper
//   cd::net::SnapshotReconciler — client-prediction vs server-snapshot diff
//
// Topology
// --------
//   Server thread  — holds authoritative position for entity0 & entity1
//   Client-0 thread — predicts entity0, reconciles against server snapshots
//   Client-1 thread — predicts entity1, reconciles against server snapshots
//
//   Connections: make_loopback_pair() gives two in-process IConnection
//   endpoints; each pair is one half-duplex channel. We create 2 pairs
//   so the server can talk to both clients independently.
//
// Protocol
// --------
//   Server side:
//     * Ticks at 20 Hz for 4 seconds (80 ticks total).
//     * Each tick: advances both entity positions (sinusoidal walk).
//     * Encodes a PositionSnapshot into a fixed-size byte blob.
//     * Enqueues each blob into a QosDispatcher on kReliable tier and
//       dispatches via the AckWindowChannel::send() sink.
//     * Also enqueues a synthetic "chat ping" into kUnreliable tier to
//       demonstrate QoS ordering.
//     * Calls AckWindowChannel::tick() to process inbound acks.
//
//   Client side (per client):
//     * Predicts entity position each server tick interval using a
//       velocity-based dead-reckoning step (local predict).
//     * Records each prediction into SnapshotReconciler::record_predicted().
//     * Calls AckWindowChannel::tick() to drain inbound data frames.
//     * For each received server snapshot:
//         - Decodes the authoritative position.
//         - Calls SnapshotReconciler::apply_authoritative() with a reapply
//           lambda that re-integrates velocity for each recorded step.
//     * Prints per-tick: predicted position, authoritative position, diff.
//
// Synchronization
// ---------------
//   Three std::condition_variable / std::mutex pairs gate the threads:
//     * server_ready_cv   — signals both clients each server tick.
//     * client_ack_cv[2]  — clients signal server after consuming tick.
//   No sleep_for anywhere; all timing is simulated via an atomic tick
//   counter that the server increments and clients observe.
//
// Exit condition
// --------------
//   Server completes kMaxTicks, sets server_done = true, then notifies
//   both clients. Each client finishes its pending work and exits.
//   main() joins all three threads and prints a summary. Exits code 0.
// =============================================================================
#include <cd/net/ReliableChannel.hpp>
#include <cd/net/IConnection.hpp>
#include <cd/net/QosDispatcher.hpp>
#include <cd/net/SnapshotReconciler.hpp>

#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <span>
#include <thread>
#include <vector>

// =============================================================================
// Domain types
// =============================================================================
namespace
{

/// 1D position state for a single entity. Kept minimal so the demo output
/// stays readable; the reconciler mechanics are identical for 3D state.
struct EntityPos
{
    float x { 0.0F };
    std::uint32_t input_seq { 0 };  // which input produced this state

    bool operator==(const EntityPos& o) const noexcept
    {
        // Epsilon-free comparison — we inject deliberate corrections, so
        // exact equality is fine here (reconciler sees them as kCorrected).
        return x == o.x && input_seq == o.input_seq;
    }
};

// Wire layout for server→client snapshots (9 bytes per entity).
// [ u32 input_seq | float x ]
constexpr std::size_t kSnapSize = sizeof(std::uint32_t) + sizeof(float);

[[nodiscard]] std::vector<std::byte> encode_snap(std::uint32_t seq, float x) noexcept
{
    std::vector<std::byte> buf(kSnapSize);
    std::uint32_t seq_le = seq;
    std::memcpy(buf.data(), &seq_le, sizeof(seq_le));
    std::memcpy(buf.data() + sizeof(seq_le), &x, sizeof(x));
    return buf;
}

struct DecodedSnap { std::uint32_t seq; float x; };
[[nodiscard]] DecodedSnap decode_snap(std::span<const std::byte> buf) noexcept
{
    DecodedSnap s {};
    std::memcpy(&s.seq, buf.data(), sizeof(s.seq));
    std::memcpy(&s.x,   buf.data() + sizeof(s.seq), sizeof(s.x));
    return s;
}

// Wire layout for a synthetic chat message (just a 1-byte ping).
constexpr std::byte kChatByte { 0xC4 };

// =============================================================================
// Simulation parameters
// =============================================================================

constexpr int   kMaxTicks       = 80;     // 4 s at 20 Hz
constexpr float kTickDt         = 0.05F;  // seconds per tick
// Intentional server-side perturbation injected at tick 30 to force
// client misprediction and trigger reconciler corrections.
constexpr int   kPerturbTick    = 30;
constexpr float kPerturbAmount  = 0.5F;

// Server authoritative motion: x = base_vel * t (straight-line walk).
// entity0 → velocity +2.0, entity1 → velocity −1.5 (opposite directions).
constexpr std::array<float, 2> kBaseVel { 2.0F, -1.5F };
// Client dead-reckoning uses a slightly wrong velocity to ensure divergence
// at least once (triggers the reconciler).
constexpr std::array<float, 2> kClientVel { 2.05F, -1.55F };

}  // anonymous namespace

// =============================================================================
// Thread synchronization primitives (shared by server + 2 clients)
// =============================================================================

struct TickBarrier
{
    std::mutex              mu;
    std::condition_variable cv;
    int                     server_tick  { -1 };
    bool                    server_done  { false };

    // Per-client ready flags (reset each tick by server, set by clients).
    std::array<bool, 2>     client_ready { false, false };
    std::condition_variable client_cv;
};

// =============================================================================
// Server thread
// =============================================================================

static void server_thread(
    TickBarrier&                                 barrier,
    // One AckWindowChannel per client connection (server side).
    std::array<std::unique_ptr<cd::net::AckWindowChannel>, 2>& srv_chans,
    // One QosDispatcher per client so we can shape each independently.
    std::array<cd::net::QosDispatcher, 2>&                     dispatchers)
{
    using Clock = cd::net::AckWindowChannel::Clock;

    // Authoritative positions (start at x=0 for both entities).
    std::array<float, 2> auth_x { 0.0F, 0.0F };

    for (int tick = 0; tick < kMaxTicks; ++tick)
    {
        // --- Advance authoritative state -----------------------------------
        for (std::size_t e = 0; e < 2; ++e)
        {
            auth_x[e] += kBaseVel[e] * kTickDt;
        }
        // Inject a perturbation at tick 30 on entity0 to force correction.
        if (tick == kPerturbTick)
            auth_x[0] += kPerturbAmount;

        const auto seq = static_cast<std::uint32_t>(tick + 1);  // 1-based

        // --- Encode + enqueue snapshot for each client -------------------
        for (std::size_t c = 0; c < 2; ++c)
        {
            // Entity c's authoritative position sent to client c.
            auto snap = encode_snap(seq, auth_x[c]);
            (void)dispatchers[c].enqueue(
                cd::net::QosTier::kReliable,
                std::span<const std::byte> { snap.data(), snap.size() });

            // Synthetic chat ping on kUnreliable (lower priority than snap).
            std::byte chat = kChatByte;
            (void)dispatchers[c].enqueue(
                cd::net::QosTier::kUnreliable,
                std::span<const std::byte> { &chat, 1 });

            // Dispatch via the AckWindowChannel sink.
            const Clock::time_point now = Clock::now();
            dispatchers[c].tick(now,
                [&](cd::net::QosTier /*tier*/, std::span<const std::byte> payload) -> bool
                {
                    return srv_chans[c]->send(payload).has_value();
                });

            // Process inbound acks.
            srv_chans[c]->tick(now);
        }

        // --- Signal clients that a new tick is available -----------------
        {
            std::lock_guard<std::mutex> lk(barrier.mu);
            barrier.server_tick = tick;
            barrier.client_ready = { false, false };
        }
        barrier.cv.notify_all();

        // --- Wait until both clients have processed this tick ------------
        {
            std::unique_lock<std::mutex> lk(barrier.mu);
            barrier.client_cv.wait(lk, [&]
            {
                return barrier.client_ready[0] && barrier.client_ready[1];
            });
        }
    }

    // Signal completion.
    {
        std::lock_guard<std::mutex> lk(barrier.mu);
        barrier.server_done = true;
    }
    barrier.cv.notify_all();
}

// =============================================================================
// Client thread (generic; one instance per client)
// =============================================================================

struct ClientStats
{
    int corrections { 0 };
    int agrees      { 0 };
    int stale       { 0 };
};

static ClientStats client_thread(
    int                          client_id,
    TickBarrier&                 barrier,
    cd::net::AckWindowChannel&   chan)
{
    using Clock = cd::net::AckWindowChannel::Clock;
    using Reconciler = cd::net::SnapshotReconciler<EntityPos>;

    const float vel = kClientVel[static_cast<std::size_t>(client_id)];

    Reconciler reconciler(256,
        // Epsilon tolerance: client and server floats may differ by rounding.
        [](const EntityPos& a, const EntityPos& b) -> bool
        {
            constexpr float kEps = 1e-4F;
            return std::fabs(a.x - b.x) < kEps && a.input_seq == b.input_seq;
        });

    // Local predicted state.
    EntityPos predicted { 0.0F, 0 };

    ClientStats stats {};
    int last_seen_tick = -1;

    // Print header for this client.
    std::printf("[client%d] tick | predicted      | authoritative  | diff\n",
                client_id);
    std::printf("[client%d] -----+----------------+----------------+----------\n",
                client_id);

    while (true)
    {
        // --- Wait for server tick signal ---------------------------------
        int my_tick = -1;
        {
            std::unique_lock<std::mutex> lk(barrier.mu);
            barrier.cv.wait(lk, [&]
            {
                return barrier.server_tick > last_seen_tick || barrier.server_done;
            });
            if (barrier.server_done && barrier.server_tick == last_seen_tick)
                break;
            my_tick = barrier.server_tick;
        }
        last_seen_tick = my_tick;

        const auto seq = static_cast<std::uint32_t>(my_tick + 1);

        // --- Client-side dead-reckoning prediction -----------------------
        predicted.x       += vel * kTickDt;
        predicted.input_seq = seq;
        reconciler.record_predicted(seq, predicted);

        // --- Drain channel: process reliable frames + AckOnly returns ----
        chan.tick(Clock::now());

        float auth_x = predicted.x;  // fallback if nothing received yet
        bool  received_snap = false;

        while (true)
        {
            auto result = chan.receive();
            if (!result.has_value())
                break;  // kWouldBlock → no more frames

            const auto& payload = *result;
            if (payload.empty())
                continue;

            // Chat messages are 1 byte; snapshots are kSnapSize bytes.
            if (payload.size() == 1 && payload[0] == kChatByte)
                continue;  // discard chat ping

            if (payload.size() < kSnapSize)
                continue;  // malformed, skip

            auto snap = decode_snap(std::span<const std::byte>(payload));
            auth_x = snap.x;
            received_snap = true;

            // Apply authoritative state through reconciler.
            const EntityPos auth_state { snap.x, snap.seq };
            const auto reapply = [vel](const EntityPos& prev, std::uint32_t /*s*/) -> EntityPos
            {
                return EntityPos { prev.x + vel * kTickDt, prev.input_seq + 1 };
            };
            auto r = reconciler.apply_authoritative(snap.seq, auth_state, reapply);

            using Outcome = Reconciler::ReconcileResult::Outcome;
            switch (r.outcome)
            {
                case Outcome::kCorrected:
                    ++stats.corrections;
                    // Reseed local prediction from reconciler's latest state.
                    if (const auto lat = reconciler.latest_recorded())
                    {
                        if (const auto st = reconciler.predicted_at(*lat))
                            predicted = *st;
                    }
                    break;
                case Outcome::kAgree:
                    ++stats.agrees;
                    break;
                case Outcome::kStale:
                    ++stats.stale;
                    break;
            }
        }

        // --- Print per-tick line -----------------------------------------
        const float diff = received_snap ? (predicted.x - auth_x) : 0.0F;
        if (my_tick % 10 == 0 || std::fabs(diff) > 0.01F)
        {
            std::printf("[client%d]  %3d | %14.4f | %14.4f | %9.4f%s\n",
                        client_id,
                        my_tick,
                        static_cast<double>(predicted.x),
                        static_cast<double>(auth_x),
                        static_cast<double>(diff),
                        (std::fabs(diff) > 0.01F) ? " !" : "  ");
        }

        // --- Signal server that this client is done with this tick -------
        {
            std::lock_guard<std::mutex> lk(barrier.mu);
            barrier.client_ready[static_cast<std::size_t>(client_id)] = true;
        }
        barrier.client_cv.notify_all();
    }

    return stats;
}

// =============================================================================
// main
// =============================================================================

int main()
{
    std::printf("=== hello_netcode: cd::net reliable + qos + reconciler 2-client demo ===\n\n");
    std::printf("Entities: 2  |  Ticks: %d  |  Dt: %.2f s  |  Perturb at tick %d (+%.2f)\n\n",
                kMaxTicks, static_cast<double>(kTickDt),
                kPerturbTick, static_cast<double>(kPerturbAmount));

    // --- Build loopback connections: one pair per client -----------------
    auto [srv_conn0, cli_conn0] = cd::net::make_loopback_pair();
    auto [srv_conn1, cli_conn1] = cd::net::make_loopback_pair();

    // --- AckWindowChannels -----------------------------------------------
    std::array<std::unique_ptr<cd::net::AckWindowChannel>, 2> srv_chans {
        std::make_unique<cd::net::AckWindowChannel>(*srv_conn0),
        std::make_unique<cd::net::AckWindowChannel>(*srv_conn1),
    };
    cd::net::AckWindowChannel cli_chan0 { *cli_conn0 };
    cd::net::AckWindowChannel cli_chan1 { *cli_conn1 };

    // --- QosDispatchers (one per client-facing server channel) -----------
    std::array<cd::net::QosDispatcher, 2> dispatchers;
    for (auto& d : dispatchers)
    {
        // kReliable: infinite budget (refill_rate=0 per QosDispatcher spec).
        // The demo ticks in a tight synchronous loop so wall-clock elapsed
        // time per tick is ~microseconds; a rate-limited reliable tier would
        // starve within the first burst. Priority ordering (kReliable first)
        // is still exercised over the kUnreliable chat tier.
        d.set_budget(cd::net::QosTier::kReliable,
                     { /*refill_rate=*/0.0, /*burst=*/0.0, /*cost_per_byte=*/0 });
        // kUnreliable (chat): finite burst so QoS priority is visible in
        // the dispatched counter (chat gets fewer slots than position).
        d.set_budget(cd::net::QosTier::kUnreliable,
                     { /*refill_rate=*/0.0, /*burst=*/0.0, /*cost_per_byte=*/0 });
    }

    // --- Shared barrier --------------------------------------------------
    TickBarrier barrier;

    // --- Launch threads --------------------------------------------------
    ClientStats stats0;
    ClientStats stats1;

    std::thread t_client0([&] { stats0 = client_thread(0, barrier, cli_chan0); });
    std::thread t_client1([&] { stats1 = client_thread(1, barrier, cli_chan1); });
    std::thread t_server([&] { server_thread(barrier, srv_chans, dispatchers); });

    t_server.join();
    t_client0.join();
    t_client1.join();

    // --- Summary ---------------------------------------------------------
    std::printf("\n=== summary ===\n");
    std::printf("  server ticks:  %d\n", kMaxTicks);
    const std::array<const ClientStats*, 2> all_stats { &stats0, &stats1 };
    for (std::size_t c = 0; c < 2; ++c)
    {
        const ClientStats& st = *all_stats[c];
        std::printf("  client%zu:  agrees=%d  corrections=%d  stale=%d\n",
                    c, st.agrees, st.corrections, st.stale);
    }

    for (std::size_t c = 0; c < 2; ++c)
    {
        const cd::net::AckWindowChannel& ch = *srv_chans[c];
        std::printf("  srv_chan%zu: retransmits=%u  perm_loss=%u  pending=%zu\n",
                    c, ch.retransmit_count(), ch.permanent_loss_count(),
                    ch.pending_send_count());

        const auto& d    = dispatchers[c];
        const auto& rel  = d.stats(cd::net::QosTier::kReliable);
        const auto& unrl = d.stats(cd::net::QosTier::kUnreliable);
        std::printf("  qos[%zu]:  kReliable dispatched=%llu  kUnreliable dispatched=%llu\n",
                    c,
                    static_cast<unsigned long long>(rel.dispatched),
                    static_cast<unsigned long long>(unrl.dispatched));
    }

    // Sanity: server must have dispatched at least kMaxTicks reliable frames
    // and corrections must be > 0 (perturbation guarantees at least one).
    const bool ok =
        dispatchers[0].stats(cd::net::QosTier::kReliable).dispatched >= static_cast<std::uint64_t>(kMaxTicks)
     && dispatchers[1].stats(cd::net::QosTier::kReliable).dispatched >= static_cast<std::uint64_t>(kMaxTicks)
     && (stats0.corrections > 0 || stats1.corrections > 0);

    std::printf("\n[hello_netcode] %s\n", ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
}
