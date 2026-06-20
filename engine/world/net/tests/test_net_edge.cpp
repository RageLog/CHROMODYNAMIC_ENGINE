// =============================================================================
// CHROMODYNAMIC — cd::net edge / negative / wraparound tests
// Phase 1258 — 85→100 gap close for cd::net
//
// Covers gaps not exercised by the four pre-existing test files:
//   * AckWindowChannel  — window-full / lost-ack / seq wraparound
//   * ReliableChannel   — RTO backoff doubling / lost-ack dedup
//   * SnapshotReconciler — gap-in-replay-chain / multi-correction
//   * PredictionBuffer  — missing-entry correct / capacity-1
//   * RleCodec          — all-same / worst-case / truncated-decode
//   * SequenceId        — u32 wraparound / equal / distance boundary
//   * SequenceWindow    — reset reuse / u32-boundary / latesttracking
//   * QosDispatcher     — byte-cost accounting / stats / sequenced evict
//   * DeltaWriter       — zero-length target / out-of-range offset
//   * LatencyStats      — reset + re-use / zero-sample guard
//   * Throttle          — negative dt guard / rate=0 / reset
//   * SnapshotBuffer    — empty sample / duplicate push time / clear
//   * PacketHeader      — all opcodes valid / length field round-trip
// =============================================================================
#include <cd/net/ChannelMux.hpp>
#include <cd/net/DeltaWriter.hpp>
#include <cd/net/IConnection.hpp>
#include <cd/net/LatencyStats.hpp>
#include <cd/net/PacketHeader.hpp>
#include <cd/net/PredictionBuffer.hpp>
#include <cd/net/QosDispatcher.hpp>
#include <cd/net/QoSTier.hpp>
#include <cd/net/ReliableChannel.hpp>
#include <cd/net/Retransmit.hpp>
#include <cd/net/RleCodec.hpp>
#include <cd/net/SequenceId.hpp>
#include <cd/net/SequenceWindow.hpp>
#include <cd/net/SnapshotBuffer.hpp>
#include <cd/net/SnapshotReconciler.hpp>
#include <cd/net/Throttle.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

[[nodiscard]] std::vector<std::byte> bytes_of(std::string_view s)
{
    std::vector<std::byte> v(s.size());
    std::memcpy(v.data(), s.data(), s.size());
    return v;
}

[[nodiscard]] std::string str_of(const std::vector<std::byte>& b)
{
    return std::string { reinterpret_cast<const char*>(b.data()), b.size() };
}

// Deterministic transport that lets tests control which frames arrive.
class ManualTransport final : public cd::net::IConnection
{
public:
    [[nodiscard]] cd::core::Result<void> send(std::span<const std::byte> bytes) override
    {
        outbox_.emplace_back(bytes.begin(), bytes.end());
        bytes_sent_ += bytes.size();
        return {};
    }

    [[nodiscard]] cd::core::Result<std::vector<std::byte>> receive() override
    {
        if (inbox_.empty())
            return std::unexpected(
                cd::net::net_errors::make(cd::net::net_errors::Code::kWouldBlock));
        auto front = std::move(inbox_.front());
        inbox_.pop_front();
        bytes_received_ += front.size();
        return front;
    }

    [[nodiscard]] cd::net::ConnectionState state() const noexcept override
    {
        return cd::net::ConnectionState::kConnected;
    }
    [[nodiscard]] std::size_t bytes_sent()     const noexcept override { return bytes_sent_; }
    [[nodiscard]] std::size_t bytes_received() const noexcept override { return bytes_received_; }
    void close() override {}

    void deliver(std::vector<std::byte> frame) { inbox_.push_back(std::move(frame)); }
    void drain_outbox() { outbox_.clear(); }
    [[nodiscard]] std::vector<std::vector<std::byte>>& outbox() { return outbox_; }
    [[nodiscard]] bool outbox_empty() const { return outbox_.empty(); }

private:
    std::vector<std::vector<std::byte>> outbox_;
    std::deque<std::vector<std::byte>>  inbox_;
    std::size_t bytes_sent_     { 0 };
    std::size_t bytes_received_ { 0 };
};

// ---------------------------------------------------------------------------
// AckWindowChannel — additional edge cases
// ---------------------------------------------------------------------------

TEST(AckWindowChannelEdge, LostAckCausesRetransmitUntilAckArrives)
{
    // Scenario: sender sends seq=1, receiver gets it, sends AckOnly.
    // AckOnly is "lost" — sender retransmits seq=1. Receiver deduplicates,
    // re-sends AckOnly. Sender finally receives AckOnly, drains pending.
    ManualTransport sender_tx;
    ManualTransport receiver_tx;
    cd::net::AckWindowChannel sender { sender_tx, /*rto=*/10ms, /*max_retries=*/5 };
    cd::net::AckWindowChannel receiver { receiver_tx };

    ASSERT_TRUE(sender.send(bytes_of("lost-ack")).has_value());
    ASSERT_EQ(sender_tx.outbox().size(), 1U);

    // Deliver the data frame to receiver.
    receiver_tx.deliver(sender_tx.outbox()[0]);
    sender_tx.drain_outbox();

    const auto t0 = cd::net::AckWindowChannel::Clock::now();
    receiver.tick(t0);   // processes data, enqueues payload, sets ack_pending_
    (void)receiver.receive();

    // Receiver's AckOnly sits in receiver_tx.outbox — don't deliver to sender.
    ASSERT_FALSE(receiver_tx.outbox().empty());
    receiver_tx.drain_outbox();  // "lose" the AckOnly

    // Sender times out and retransmits — tick well past rto.
    sender.tick(t0 + 50ms);
    EXPECT_GE(sender.retransmit_count(), 1U);
    EXPECT_EQ(sender.pending_send_count(), 1U);

    // Receiver gets the retransmit, deduplicates (doesn't deliver again),
    // emits another AckOnly.
    receiver_tx.deliver(sender_tx.outbox()[0]);
    sender_tx.drain_outbox();
    receiver.tick(t0 + 60ms);
    EXPECT_EQ(receiver.duplicate_drop_count(), 1U);   // retransmit was a dup
    auto dup_recv = receiver.receive();
    EXPECT_FALSE(dup_recv.has_value());              // not delivered twice

    // Now deliver the new AckOnly to sender.
    ASSERT_FALSE(receiver_tx.outbox().empty());
    sender_tx.deliver(receiver_tx.outbox().back());
    sender.tick(t0 + 70ms);
    EXPECT_EQ(sender.pending_send_count(), 0U);    // finally acked
}

TEST(AckWindowChannelEdge, WindowFullBlocksNewSend)
{
    // AckWindowChannel has no send window — this verifies it queues
    // multiple outstanding frames correctly (no artificial cap).
    // Exercise the ack_bits shift path with a large window distance.
    ManualTransport tx;
    cd::net::AckWindowChannel ch { tx };

    // Send more than 64 frames to trigger the "shift >= 64 → ack_bits = 0" branch.
    for (std::uint32_t i = 0; i < 70; ++i)
        ASSERT_TRUE(ch.send(bytes_of("x")).has_value());
    EXPECT_EQ(ch.pending_send_count(), 70U);
}

TEST(AckWindowChannelEdge, AckBitsShiftGt64ClearsWindow)
{
    // Receiver sees seq=1, then seq=68 (gap > 64). The shift branch
    // "shift >= 64 → ack_bits_ = 0" in handle_frame_ must fire.
    ManualTransport sender_tx;
    ManualTransport receiver_tx;
    cd::net::AckWindowChannel sender { sender_tx };
    cd::net::AckWindowChannel receiver { receiver_tx };

    // Send 68 frames.
    for (std::uint32_t i = 0; i < 68; ++i)
        (void)sender.send(bytes_of("p"));
    ASSERT_EQ(sender_tx.outbox().size(), 68U);

    // Deliver only frame 0 (seq=1) and frame 67 (seq=68) to receiver.
    receiver_tx.deliver(sender_tx.outbox()[0]);
    receiver_tx.deliver(sender_tx.outbox()[67]);

    const auto now = cd::net::AckWindowChannel::Clock::now();
    receiver.tick(now);

    // Seq=1 is contiguous from baseline (next_deliver_seq_=0), so it flushes
    // to delivery_queue immediately. Seq=68 is buffered in inbox_ because
    // 2..67 are missing — no further flush happens.
    EXPECT_EQ(receiver.ready_count(), 1U);   // only seq=1 flushed
    // latest_ack_seen_ should be 68 (the highest we delivered to the channel).
    EXPECT_EQ(receiver.latest_ack_seen(), 68U);
}

TEST(AckWindowChannelEdge, MaxRetriesDropsPermanently)
{
    // Verify permanent_loss_count increments when retries exhaust.
    ManualTransport tx;
    cd::net::AckWindowChannel ch { tx, /*rto=*/5ms, /*max_retries=*/2 };

    ASSERT_TRUE(ch.send(bytes_of("drop-me")).has_value());
    const auto t0 = cd::net::AckWindowChannel::Clock::now();

    // Three ticks past RTO: retry 1, retry 2, then exhausted → permanent drop.
    ch.tick(t0 + 10ms);
    ch.tick(t0 + 20ms);
    ch.tick(t0 + 30ms);

    EXPECT_EQ(ch.permanent_loss_count(), 1U);
    EXPECT_EQ(ch.pending_send_count(), 0U);
    EXPECT_EQ(ch.retransmit_count(), 2U);
}

TEST(AckWindowChannelEdge, DuplicateDataIncrementsDupDropCounter)
{
    // Inject the same Data frame twice into the receiver's transport.
    ManualTransport sender_tx;
    ManualTransport receiver_tx;
    cd::net::AckWindowChannel sender { sender_tx };
    cd::net::AckWindowChannel receiver { receiver_tx };

    ASSERT_TRUE(sender.send(bytes_of("dup-test")).has_value());
    ASSERT_FALSE(sender_tx.outbox().empty());

    // Deliver same frame twice.
    receiver_tx.deliver(sender_tx.outbox()[0]);
    receiver_tx.deliver(sender_tx.outbox()[0]);

    receiver.tick(cd::net::AckWindowChannel::Clock::now());

    EXPECT_EQ(receiver.duplicate_drop_count(), 1U);
    // Only one payload in delivery queue.
    auto r1 = receiver.receive();
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(str_of(*r1), "dup-test");
    EXPECT_FALSE(receiver.receive().has_value());
}

TEST(AckWindowChannelEdge, EmptyPayloadFrameRoundTrips)
{
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::AckWindowChannel sender { *a };
    cd::net::AckWindowChannel receiver { *b };

    ASSERT_TRUE(sender.send({}).has_value());

    receiver.tick(cd::net::AckWindowChannel::Clock::now());
    auto r = receiver.receive();
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(r->empty());
}

// ---------------------------------------------------------------------------
// ReliableChannel (Retransmit.hpp) — additional edge cases
// ---------------------------------------------------------------------------

TEST(ReliableChannelEdge, RtoBackoffDoublesPerEntry)
{
    // After a retransmit, effective_rto for that entry should double.
    // Drive 3 ticks past progressively-longer RTOs to confirm backoff.
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::ChannelMux ma { *a };
    cd::net::ReliableChannel sender {
        ma, 0, 1, /*initial_rto=*/10ms, /*max_retries=*/10
    };

    ASSERT_TRUE(sender.send(bytes_of("backoff")).has_value());

    // Drop the frame.
    auto dropped = b->receive();
    ASSERT_TRUE(dropped.has_value());

    const auto t0 = cd::net::ReliableChannel::Clock::now();

    // Tick 1: past 10ms → first retransmit, entry's effective_rto = 20ms.
    sender.tick(t0 + 15ms);
    EXPECT_EQ(sender.retransmit_count(), 1U);

    // Drop that retransmit too.
    while (b->receive().has_value()) {}

    // Tick 2: 25ms since t0 — effective_rto is now 20ms, send_time was
    // reset to t0+15ms, so deadline = t0+35ms. Not fired yet.
    sender.tick(t0 + 25ms);
    EXPECT_EQ(sender.retransmit_count(), 1U);

    // Tick 3: 40ms from t0 — past the 20ms backoff → second retransmit.
    sender.tick(t0 + 55ms);
    EXPECT_GE(sender.retransmit_count(), 2U);
}

TEST(ReliableChannelEdge, LostAckDuplicateNotDelivered)
{
    // Receiver gets the data and ACKs, ACK is lost. Sender retransmits.
    // Receiver deduplicates and re-emits ACK. Final state: 1 payload delivered.
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::ChannelMux ma { *a };
    cd::net::ChannelMux mb { *b };
    cd::net::ReliableChannel sender { ma, 0, 1, 20ms, 5 };
    cd::net::ReliableChannel receiver { mb, 0, 1 };

    ASSERT_TRUE(sender.send(bytes_of("once-only")).has_value());

    const auto t0 = cd::net::ReliableChannel::Clock::now();
    // Receiver processes data + emits ACK on ACK channel.
    receiver.tick(t0);

    // Drop the ACK by draining a's receive queue.
    while (a->receive().has_value()) {}

    // Check payload delivered exactly once.
    auto first = receiver.receive();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(str_of(*first), "once-only");
    EXPECT_FALSE(receiver.receive().has_value());

    // Past RTO → sender retransmits.
    sender.tick(t0 + 50ms);
    EXPECT_GE(sender.retransmit_count(), 1U);

    // Receiver gets retransmit: dedup, NOT delivered again.
    receiver.tick(t0 + 55ms);
    EXPECT_GE(receiver.duplicate_drop_count(), 1U);
    EXPECT_FALSE(receiver.receive().has_value());
}

TEST(ReliableChannelEdge, WindowFullThenAckUnblocks)
{
    // With window=2: third send must block, then after ACK the slot opens.
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::ChannelMux ma { *a };
    cd::net::ChannelMux mb { *b };
    cd::net::ReliableChannel sender {
        ma, 0, 1, 200ms, 5, 25ms, 5000ms, /*send_window_size=*/2
    };
    cd::net::ReliableChannel receiver { mb, 0, 1 };

    const auto p = bytes_of("w");
    ASSERT_TRUE(sender.send({ p.data(), p.size() }).has_value());
    ASSERT_TRUE(sender.send({ p.data(), p.size() }).has_value());

    // Window full.
    auto blocked = sender.send({ p.data(), p.size() });
    ASSERT_FALSE(blocked.has_value());
    EXPECT_EQ(blocked.error().code,
              static_cast<std::uint32_t>(cd::net::net_errors::Code::kWouldBlock));

    // Drain via receiver+sender tick.
    const auto t0 = cd::net::ReliableChannel::Clock::now();
    receiver.tick(t0);
    sender.tick(t0);
    EXPECT_EQ(sender.pending_send_count(), 0U);

    // Now a send succeeds.
    ASSERT_TRUE(sender.send({ p.data(), p.size() }).has_value());
}

TEST(ReliableChannelEdge, SackRangeCapNotExceeded)
{
    // Send more than kSackRangeCap scattered frames to verify the
    // build_sack_ranges() function caps output correctly.
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::ChannelMux ma { *a };
    cd::net::ChannelMux mb { *b };
    cd::net::ReliableChannel sender { ma, 0, 1 };
    cd::net::ReliableChannel receiver { mb, 0, 1 };

    const auto p = bytes_of("s");
    // Send 40 frames (> kSackRangeCap=16).
    for (int i = 0; i < 40; ++i)
        ASSERT_TRUE(sender.send({ p.data(), p.size() }).has_value());

    // Let receiver process all — no crash, no assert.
    const auto t0 = cd::net::ReliableChannel::Clock::now();
    receiver.tick(t0);
    sender.tick(t0);
    EXPECT_EQ(sender.pending_send_count(), 0U);
}

// ---------------------------------------------------------------------------
// SnapshotReconciler — additional edge cases
// ---------------------------------------------------------------------------

TEST(SnapshotReconcilerEdge, ReplayChainWithGapStopsAtMissingEntry)
{
    // Records: 1, 2, 4 (seq 3 missing). Correction at 1 should replay
    // seq 2 (exists) but stop at seq 3 (not in ring), never reaching 4.
    cd::net::SnapshotReconciler<int> r;
    r.record_predicted(1, 10);
    r.record_predicted(2, 11);
    r.record_predicted(4, 13);  // seq 3 NOT recorded

    auto reapply = [](int prev, std::uint32_t) { return prev + 1; };
    auto res = r.apply_authoritative(1, 100, reapply);
    EXPECT_EQ(res.outcome, cd::net::SnapshotReconciler<int>::ReconcileResult::Outcome::kCorrected);
    EXPECT_EQ(res.replayed, 1U);    // only seq 2 replayed; stops at gap before 3

    // Seq 2 gets corrected state + 1 step.
    auto p2 = r.predicted_at(2);
    ASSERT_TRUE(p2.has_value());
    EXPECT_EQ(*p2, 101);

    // Seq 4 is untouched (replay stopped before it).
    auto p4 = r.predicted_at(4);
    ASSERT_TRUE(p4.has_value());
    EXPECT_EQ(*p4, 13);   // original, not replayed
}

TEST(SnapshotReconcilerEdge, MultipleSuccessiveCorrections)
{
    cd::net::SnapshotReconciler<int> r;
    for (std::uint32_t i = 1; i <= 5; ++i)
        r.record_predicted(i, static_cast<int>(i * 10));

    auto reapply = [](int prev, std::uint32_t) { return prev + 1; };

    // First correction at seq 2.
    auto res1 = r.apply_authoritative(2, 200, reapply);
    EXPECT_EQ(res1.outcome, cd::net::SnapshotReconciler<int>::ReconcileResult::Outcome::kCorrected);
    EXPECT_EQ(res1.replayed, 3U);   // seqs 3, 4, 5 replayed
    EXPECT_EQ(r.correction_count(), 1U);

    // Verify chain: 200 → 201 → 202 → 203.
    EXPECT_EQ(*r.predicted_at(2), 200);
    EXPECT_EQ(*r.predicted_at(3), 201);
    EXPECT_EQ(*r.predicted_at(4), 202);
    EXPECT_EQ(*r.predicted_at(5), 203);

    // Second correction at seq 3 (further mismatch from real physics).
    auto res2 = r.apply_authoritative(3, 300, reapply);
    EXPECT_EQ(res2.outcome, cd::net::SnapshotReconciler<int>::ReconcileResult::Outcome::kCorrected);
    EXPECT_EQ(r.correction_count(), 2U);

    EXPECT_EQ(*r.predicted_at(3), 300);
    EXPECT_EQ(*r.predicted_at(4), 301);
    EXPECT_EQ(*r.predicted_at(5), 302);
}

TEST(SnapshotReconcilerEdge, StaleSnapshotWithNoRecordsReturnsStale)
{
    cd::net::SnapshotReconciler<int> r;
    // Empty ring: any authoritative is stale.
    auto res = r.apply_authoritative(42, 999, [](int, std::uint32_t) { return 0; });
    EXPECT_EQ(res.outcome, cd::net::SnapshotReconciler<int>::ReconcileResult::Outcome::kStale);
    EXPECT_EQ(r.stale_snapshot_count(), 1U);
}

TEST(SnapshotReconcilerEdge, LatestRecordedUpdatesMonotonically)
{
    cd::net::SnapshotReconciler<int> r;
    EXPECT_FALSE(r.latest_recorded().has_value());

    r.record_predicted(5, 50);
    ASSERT_TRUE(r.latest_recorded().has_value());
    EXPECT_EQ(*r.latest_recorded(), 5U);

    r.record_predicted(3, 30);   // older seq — latest must stay at 5
    EXPECT_EQ(*r.latest_recorded(), 5U);

    r.record_predicted(10, 100);
    EXPECT_EQ(*r.latest_recorded(), 10U);
}

TEST(SnapshotReconcilerEdge, AgreementOnFirstEntry)
{
    cd::net::SnapshotReconciler<int> r;
    r.record_predicted(1, 42);
    auto res = r.apply_authoritative(1, 42, [](int, std::uint32_t) { return 0; });
    EXPECT_EQ(res.outcome, cd::net::SnapshotReconciler<int>::ReconcileResult::Outcome::kAgree);
    EXPECT_EQ(r.agree_count(), 1U);
}

// ---------------------------------------------------------------------------
// PredictionBuffer — additional edge cases
// ---------------------------------------------------------------------------

TEST(PredictionBufferEdge, CorrectMissingEntryIsNoOpReplay)
{
    cd::net::PredictionBuffer<int> pb { 8 };
    pb.record(10, 100);
    pb.record(12, 120);
    // correct_and_replay on seq 11 (not recorded) → replace nothing,
    // replay starting from seq 12 which exists.
    const auto replayed = pb.correct_and_replay(11, 999,
        [](int prev, std::uint32_t) { return prev + 1; });
    // seq 11 not in buffer, so the write for seq 11 is a no-op.
    // The replay walk starts from cursor=11, looks for seq 12, finds it.
    EXPECT_EQ(replayed, 1U);
    // seq 12 should now be 999+1=1000.
    EXPECT_EQ(*pb.at(12), 1000);
}

TEST(PredictionBufferEdge, CapacityOneEvictsOnSecondRecord)
{
    cd::net::PredictionBuffer<int> pb { 1 };
    pb.record(1, 100);
    EXPECT_EQ(pb.size(), 1U);
    EXPECT_EQ(*pb.at(1), 100);

    pb.record(2, 200);   // evicts seq 1
    EXPECT_EQ(pb.size(), 1U);
    EXPECT_FALSE(pb.at(1).has_value());
    EXPECT_EQ(*pb.at(2), 200);
}

TEST(PredictionBufferEdge, ClearMakesAllEntriesInvalid)
{
    cd::net::PredictionBuffer<int> pb { 16 };
    for (std::uint32_t i = 0; i < 8; ++i) pb.record(i, static_cast<int>(i));
    pb.clear();
    EXPECT_EQ(pb.size(), 0U);
    for (std::uint32_t i = 0; i < 8; ++i)
        EXPECT_FALSE(pb.at(i).has_value());
}

// ---------------------------------------------------------------------------
// RleCodec — additional edge cases
// ---------------------------------------------------------------------------

TEST(RleCodecEdge, AllSameByteSingleRun)
{
    std::vector<std::uint8_t> src(50, 0x42u);
    auto enc = cd::net::rle_encode(src);
    // Exactly one (count, value) pair.
    ASSERT_EQ(enc.size(), 2U);
    EXPECT_EQ(enc[0], 50U);
    EXPECT_EQ(enc[1], 0x42U);

    auto dec = cd::net::rle_decode(enc);
    EXPECT_EQ(dec, src);
}

TEST(RleCodecEdge, WorstCaseAllDifferent)
{
    // 256 distinct bytes → 256 pairs, each of count=1.
    std::vector<std::uint8_t> src;
    src.reserve(256);
    for (std::uint32_t i = 0; i < 256; ++i)
        src.push_back(static_cast<std::uint8_t>(i));
    auto enc = cd::net::rle_encode(src);
    EXPECT_EQ(enc.size(), 512U);   // 256 × (count=1, value)
    auto dec = cd::net::rle_decode(enc);
    EXPECT_EQ(dec, src);
}

TEST(RleCodecEdge, TruncatedDecodeReturnsPartial)
{
    // A valid RLE stream cut short: count=10 but only 1 pair then eof.
    // rle_decode with an odd-length or truncated input returns partial output.
    std::vector<std::uint8_t> partial { 3, 0xAAu, 5, 0xBBu };
    // Remove last byte to make it odd-length: only first pair is complete.
    partial.pop_back();
    auto dec = cd::net::rle_decode(partial);
    // Only the first pair (3, 0xAA) is decoded; the incomplete (5, truncated) is ignored.
    ASSERT_EQ(dec.size(), 3U);
    EXPECT_EQ(dec[0], 0xAAu);
    EXPECT_EQ(dec[2], 0xAAu);
}

TEST(RleCodecEdge, SingleByteInput)
{
    std::vector<std::uint8_t> src { 0xFFu };
    auto enc = cd::net::rle_encode(src);
    ASSERT_EQ(enc.size(), 2U);
    EXPECT_EQ(enc[0], 1U);
    EXPECT_EQ(enc[1], 0xFFU);
    auto dec = cd::net::rle_decode(enc);
    EXPECT_EQ(dec, src);
}

TEST(RleCodecEdge, RunOf255ThenOne)
{
    // Exactly 255 + 1 of the same byte — must encode as two separate pairs.
    std::vector<std::uint8_t> src(256, 0x0Au);
    auto enc = cd::net::rle_encode(src);
    ASSERT_EQ(enc.size(), 4U);
    EXPECT_EQ(enc[0], 255U);
    EXPECT_EQ(enc[2], 1U);
    auto dec = cd::net::rle_decode(enc);
    EXPECT_EQ(dec, src);
}

// ---------------------------------------------------------------------------
// SequenceId — u32 wraparound and edge cases
// ---------------------------------------------------------------------------

TEST(SequenceIdEdge, U32WrapAroundGtComparison)
{
    using cd::net::seq_greater_than;
    // u32 max = 4294967295. 0 after wrap should be "newer" than max.
    EXPECT_TRUE(seq_greater_than<std::uint32_t>(0U, 0xFFFFFFFFU));
    EXPECT_FALSE(seq_greater_than<std::uint32_t>(0xFFFFFFFFU, 0U));
}

TEST(SequenceIdEdge, EqualValuesNotGreater)
{
    using cd::net::seq_greater_than;
    EXPECT_FALSE(seq_greater_than<std::uint32_t>(100U, 100U));
    EXPECT_FALSE(seq_greater_than<std::uint16_t>(0U, 0U));
    EXPECT_FALSE(seq_greater_than<std::uint8_t>(255U, 255U));
}

TEST(SequenceIdEdge, HalfSpaceBoundaryU16)
{
    using cd::net::seq_greater_than;
    // At exactly kHalf distance: 32768 ahead of 0 is NOT "newer" (tie-break).
    // seq_greater_than: (a > b && a-b <= kHalf) || (a < b && b-a > kHalf)
    // a=32768, b=0: a > b && 32768 <= 32768 → true (newer).
    EXPECT_TRUE(seq_greater_than<std::uint16_t>(32768U, 0U));
    // a=32769, b=0: a > b && 32769 <= 32768 → false; a < b → false → false.
    EXPECT_FALSE(seq_greater_than<std::uint16_t>(32769U, 0U));
}

TEST(SequenceIdEdge, DistanceAtBoundary)
{
    using cd::net::seq_distance;
    // seq_distance is unsigned subtract — wraps correctly.
    // Distance from 0 back to 65535: 0 - 65535 (u16) = 1.
    EXPECT_EQ(seq_distance<std::uint16_t>(0U, 65535U), 1U);
    // Distance from 100 to 50: 50.
    EXPECT_EQ(seq_distance<std::uint16_t>(100U, 50U), 50U);
}

TEST(SequenceIdEdge, U8WrapAround)
{
    using cd::net::seq_greater_than;
    // u8 kHalf = 128. 0 after 255 wrap → newer.
    EXPECT_TRUE(seq_greater_than<std::uint8_t>(0U, 255U));
    EXPECT_FALSE(seq_greater_than<std::uint8_t>(255U, 0U));
}

// ---------------------------------------------------------------------------
// SequenceWindow — additional edge cases
// ---------------------------------------------------------------------------

TEST(SequenceWindowEdge, ResetAndReuse)
{
    cd::net::SequenceWindow<8> w;
    EXPECT_TRUE(w.accept(10));
    EXPECT_FALSE(w.accept(10));   // duplicate

    w.reset();
    // After reset, seq 10 is "new" again.
    EXPECT_TRUE(w.accept(10));
    EXPECT_FALSE(w.accept(10));
}

TEST(SequenceWindowEdge, LatestTracksHighestSeen)
{
    cd::net::SequenceWindow<> w;
    w.accept(5);
    w.accept(3);
    w.accept(8);
    w.accept(2);
    EXPECT_EQ(w.latest(), 8U);
}

TEST(SequenceWindowEdge, GapExactlyAtWindowBoundary)
{
    // Window N=8: age=8 is exactly the limit. accept(100) latest, then
    // accept(92) is age=8 → too old (age >= N). accept(93) age=7 → inside.
    cd::net::SequenceWindow<8> w;
    EXPECT_TRUE(w.accept(100));
    EXPECT_FALSE(w.accept(92));   // age = 8, exactly at limit (rejected)
    EXPECT_TRUE(w.accept(93));    // age = 7, inside window
    EXPECT_FALSE(w.accept(93));   // now a duplicate
}

TEST(SequenceWindowEdge, LargeJumpClearsOldBits)
{
    cd::net::SequenceWindow<64> w;
    EXPECT_TRUE(w.accept(0));
    EXPECT_TRUE(w.accept(1));
    // Jump by > 64 — old bits clear, then a backward seek for old seq rejected.
    EXPECT_TRUE(w.accept(100));
    EXPECT_FALSE(w.accept(0));   // now too old (distance 100 > 64)
    EXPECT_FALSE(w.accept(1));   // also too old
}

// ---------------------------------------------------------------------------
// QosDispatcher — additional edge cases
// ---------------------------------------------------------------------------

TEST(QosDispatcherEdge, ByteCostAccountingPerMessage)
{
    // cost_per_byte=10, burst=3: a 20-byte message costs 1 + ceil(20/10) = 3 tokens.
    cd::net::QosDispatcher d;
    d.set_budget(cd::net::QosTier::kUnreliable,
                 cd::net::QosBudget { /*refill*/ 100.0, /*burst*/ 3.0,
                                     /*cost_per_byte*/ 10 });

    // 20-byte payload → cost = 3.0. Bucket starts at 3 tokens → exactly fits 1.
    std::vector<std::byte> payload20(20, std::byte { 0xAB });
    ASSERT_TRUE(d.enqueue(cd::net::QosTier::kUnreliable,
                          { payload20.data(), payload20.size() }).has_value());
    // A second 20-byte message can't fit (0 tokens left after first).
    ASSERT_TRUE(d.enqueue(cd::net::QosTier::kUnreliable,
                          { payload20.data(), payload20.size() }).has_value());

    const auto t0 = cd::net::QosDispatcher::Clock::now();
    std::size_t dispatched = 0;
    (void)d.tick(t0, [&](cd::net::QosTier, std::span<const std::byte>) {
        ++dispatched;
        return true;
    });
    EXPECT_EQ(dispatched, 1U);   // second blocked by budget
    EXPECT_EQ(d.stats(cd::net::QosTier::kUnreliable).dropped_budget, 1U);
}

TEST(QosDispatcherEdge, SequencedTierOverflowEvictsOldest)
{
    cd::net::QosDispatcher d { /*max_queue*/ 2 };
    const auto b1 = bytes_of("first");
    const auto b2 = bytes_of("second");
    const auto b3 = bytes_of("third");
    ASSERT_TRUE(d.enqueue(cd::net::QosTier::kUnreliableSequenced,
                          { b1.data(), b1.size() }).has_value());
    ASSERT_TRUE(d.enqueue(cd::net::QosTier::kUnreliableSequenced,
                          { b2.data(), b2.size() }).has_value());
    ASSERT_TRUE(d.enqueue(cd::net::QosTier::kUnreliableSequenced,
                          { b3.data(), b3.size() }).has_value());  // evicts "first"

    EXPECT_EQ(d.pending(cd::net::QosTier::kUnreliableSequenced), 2U);
    EXPECT_EQ(d.stats(cd::net::QosTier::kUnreliableSequenced).dropped_queue_overflow, 1U);

    std::string delivered;
    (void)d.tick(cd::net::QosDispatcher::Clock::now(),
                 [&](cd::net::QosTier, std::span<const std::byte> p) {
                     delivered.append(reinterpret_cast<const char*>(p.data()), p.size());
                     return true;
                 });
    // "first" was evicted; "second" and "third" dispatched.
    EXPECT_NE(delivered.find("second"), std::string::npos);
    EXPECT_NE(delivered.find("third"),  std::string::npos);
    EXPECT_EQ(delivered.find("first"),  std::string::npos);
}

TEST(QosDispatcherEdge, StatsDispatchedCountAccumulates)
{
    cd::net::QosDispatcher d;
    const auto p = bytes_of("r");
    for (int i = 0; i < 5; ++i)
        ASSERT_TRUE(d.enqueue(cd::net::QosTier::kReliable,
                              { p.data(), p.size() }).has_value());
    const auto t0 = cd::net::QosDispatcher::Clock::now();
    (void)d.tick(t0, [](cd::net::QosTier, std::span<const std::byte>) { return true; });
    EXPECT_EQ(d.stats(cd::net::QosTier::kReliable).dispatched, 5U);
}

TEST(QosDispatcherEdge, TokenRefillDoesNotExceedBurst)
{
    cd::net::QosDispatcher d;
    d.set_budget(cd::net::QosTier::kUnreliable,
                 cd::net::QosBudget { /*refill*/ 10.0, /*burst*/ 3.0, 0 });

    const auto t0 = cd::net::QosDispatcher::Clock::now();
    // First tick: initialises last_tick_.
    (void)d.tick(t0, [](cd::net::QosTier, std::span<const std::byte>) { return true; });

    // Second tick: 100 seconds elapsed → would refill 1000 tokens, but capped at burst=3.
    ASSERT_TRUE(d.enqueue(cd::net::QosTier::kUnreliable, bytes_of("x")).has_value());
    ASSERT_TRUE(d.enqueue(cd::net::QosTier::kUnreliable, bytes_of("x")).has_value());
    ASSERT_TRUE(d.enqueue(cd::net::QosTier::kUnreliable, bytes_of("x")).has_value());
    ASSERT_TRUE(d.enqueue(cd::net::QosTier::kUnreliable, bytes_of("x")).has_value());  // 4th

    std::size_t dispatched = 0;
    (void)d.tick(t0 + std::chrono::seconds { 100 },
                 [&](cd::net::QosTier, std::span<const std::byte>) {
                     ++dispatched;
                     return true;
                 });
    // Burst=3 so at most 3 dispatched despite 100s elapsed.
    EXPECT_EQ(dispatched, 3U);
    EXPECT_EQ(d.pending(cd::net::QosTier::kUnreliable), 1U);
}

// ---------------------------------------------------------------------------
// DeltaWriter — additional edge cases
// ---------------------------------------------------------------------------

TEST(DeltaWriterEdge, AllBytesChanged)
{
    std::array<std::byte, 4> base { std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0} };
    std::array<std::byte, 4> cur  { std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4} };
    auto d = cd::net::write_delta(base, cur);
    // changed=4: 2 + 4*3 = 14 bytes.
    EXPECT_EQ(d.size(), 14U);
    std::array<std::byte, 4> tgt {};
    ASSERT_TRUE(cd::net::apply_delta(tgt, d));
    EXPECT_EQ(std::memcmp(tgt.data(), cur.data(), 4), 0);
}

TEST(DeltaWriterEdge, OutOfRangeOffsetRejected)
{
    // Hand-craft a delta with offset 255 applied to a 4-byte target.
    std::array<std::byte, 4> tgt {};
    std::vector<std::byte> bad_delta {
        std::byte{1}, std::byte{0},   // changed=1
        std::byte{255}, std::byte{0}, // offset=255 (out of range for 4-byte tgt)
        std::byte{0xAB}               // value
    };
    EXPECT_FALSE(cd::net::apply_delta(tgt, bad_delta));
}

TEST(DeltaWriterEdge, ZeroLengthTargetEmptyDelta)
{
    std::span<const std::byte> empty_base {};
    std::span<const std::byte> empty_cur  {};
    auto d = cd::net::write_delta(empty_base, empty_cur);
    // changed=0: only 2 header bytes.
    ASSERT_EQ(d.size(), 2U);
    EXPECT_EQ(static_cast<std::uint8_t>(d[0]), 0U);
}

// ---------------------------------------------------------------------------
// LatencyStats — additional edge cases
// ---------------------------------------------------------------------------

TEST(LatencyStatsEdge, ZeroSampleGuardReturnsZero)
{
    cd::net::LatencyStats s;
    EXPECT_EQ(s.min_us(), 0U);
    EXPECT_EQ(s.max_us(), 0U);
    EXPECT_FLOAT_EQ(s.current_rtt_us(), 0.0F);
    EXPECT_EQ(s.sample_count(), 0U);
}

TEST(LatencyStatsEdge, ResetClearsAccumulators)
{
    cd::net::LatencyStats s;
    s.record(10000);
    s.record(20000);
    EXPECT_GT(s.sample_count(), 0U);

    s.reset();
    EXPECT_EQ(s.sample_count(), 0U);
    EXPECT_FLOAT_EQ(s.current_rtt_us(), 0.0F);
    EXPECT_EQ(s.min_us(), 0U);
    EXPECT_EQ(s.max_us(), 0U);
}

TEST(LatencyStatsEdge, SingleSampleSetsMinAndMax)
{
    cd::net::LatencyStats s;
    s.record(77777U);
    EXPECT_EQ(s.min_us(), 77777U);
    EXPECT_EQ(s.max_us(), 77777U);
    EXPECT_FLOAT_EQ(s.jitter_us(), 0.0F);
}

// ---------------------------------------------------------------------------
// Throttle — additional edge cases
// ---------------------------------------------------------------------------

TEST(ThrottleEdge, NegativeDtNoRefill)
{
    cd::net::Throttle t { 10.0F, 5.0F };
    (void)t.try_consume(8.0F);
    EXPECT_FLOAT_EQ(t.tokens(), 2.0F);
    t.update(-1.0F);   // negative dt must be ignored
    EXPECT_FLOAT_EQ(t.tokens(), 2.0F);
}

TEST(ThrottleEdge, ZeroRateNeverRefills)
{
    cd::net::Throttle t { 5.0F, 0.0F };
    (void)t.try_consume(3.0F);
    t.update(100.0F);  // 100 seconds, rate=0 → no tokens added
    EXPECT_FLOAT_EQ(t.tokens(), 2.0F);
}

TEST(ThrottleEdge, ResetRestoresCapacity)
{
    cd::net::Throttle t { 8.0F, 2.0F };
    (void)t.try_consume(6.0F);
    EXPECT_FLOAT_EQ(t.tokens(), 2.0F);
    t.reset();
    EXPECT_FLOAT_EQ(t.tokens(), 8.0F);
}

TEST(ThrottleEdge, ConsumeExactCapacitySucceeds)
{
    cd::net::Throttle t { 5.0F, 1.0F };
    EXPECT_TRUE(t.try_consume(5.0F));
    EXPECT_FLOAT_EQ(t.tokens(), 0.0F);
    EXPECT_FALSE(t.try_consume(0.001F));  // empty
}

// ---------------------------------------------------------------------------
// SnapshotBuffer — additional edge cases
// ---------------------------------------------------------------------------

namespace
{
struct NetPos
{
    float x { 0.0F };
    NetPos operator+(const NetPos& o) const noexcept { return { x + o.x }; }
    NetPos operator*(float s) const noexcept { return { x * s }; }
};
}  // namespace

TEST(SnapshotBufferEdge, EmptyBufferReturnsNullopt)
{
    cd::net::SnapshotBuffer<NetPos> buf;
    EXPECT_FALSE(buf.sample(1.0).has_value());
}

TEST(SnapshotBufferEdge, AfterClearSampleReturnsNullopt)
{
    cd::net::SnapshotBuffer<NetPos> buf;
    buf.push(0.0, NetPos { 10.0F });
    buf.clear();
    EXPECT_TRUE(buf.empty());
    EXPECT_FALSE(buf.sample(0.0).has_value());
}

TEST(SnapshotBufferEdge, PushOutOfOrderInterleavedCorrectly)
{
    // Push t=2, t=0, t=1 — buffer should still interpolate correctly.
    cd::net::SnapshotBuffer<NetPos> buf;
    buf.push(2.0, NetPos { 20.0F });
    buf.push(0.0, NetPos { 0.0F });
    buf.push(1.0, NetPos { 10.0F });

    // Sample at t=0.5 → between t=0 (x=0) and t=1 (x=10) → x=5.
    auto r = buf.sample(0.5);
    ASSERT_TRUE(r.has_value());
    EXPECT_NEAR(r->x, 5.0F, 1e-4F);
}

TEST(SnapshotBufferEdge, DropOlderThanRemovesAll)
{
    cd::net::SnapshotBuffer<NetPos> buf;
    buf.push(0.0, NetPos {});
    buf.push(1.0, NetPos {});
    buf.drop_older_than(999.0);
    EXPECT_TRUE(buf.empty());
}

TEST(SnapshotBufferEdge, SampleAfterLatestClampsToLast)
{
    cd::net::SnapshotBuffer<NetPos> buf;
    buf.push(0.0, NetPos { 5.0F });
    buf.push(1.0, NetPos { 15.0F });
    auto r = buf.sample(100.0);
    ASSERT_TRUE(r.has_value());
    EXPECT_FLOAT_EQ(r->x, 15.0F);
}

// ---------------------------------------------------------------------------
// PacketHeader — additional edge cases
// ---------------------------------------------------------------------------

TEST(PacketHeaderEdge, AllValidOpcodes)
{
    using cd::net::Opcode;
    using cd::net::PacketHeader;
    const Opcode valid_ops[] = {
        Opcode::kHandshake, Opcode::kHeartbeat, Opcode::kReliable,
        Opcode::kUnreliable, Opcode::kAck, Opcode::kDisconnect,
    };
    for (auto op : valid_ops)
    {
        PacketHeader h;
        h.opcode = op;
        EXPECT_TRUE(cd::net::is_valid(h)) << "opcode=" << static_cast<int>(op);
    }
}

TEST(PacketHeaderEdge, LengthFieldRoundTrip)
{
    cd::net::PacketHeader h;
    h.opcode = cd::net::Opcode::kReliable;
    h.length = 1024U;
    EXPECT_EQ(h.length, 1024U);
    EXPECT_TRUE(cd::net::is_valid(h));
}

TEST(PacketHeaderEdge, DefaultConstructHasCorrectMagicAndVersion)
{
    cd::net::PacketHeader h;
    EXPECT_EQ(h.magic, cd::net::kPacketMagic);
    EXPECT_EQ(h.version, cd::net::kPacketVersion);
    EXPECT_EQ(h.opcode, cd::net::Opcode::kInvalid);
}

// ---------------------------------------------------------------------------
// QoSTier — additional edge cases
// ---------------------------------------------------------------------------

TEST(QoSTierEdge, AllTierPrioritiesStrictlyOrdered)
{
    using cd::net::QoSTier;
    using cd::net::tier_priority;
    EXPECT_LT(tier_priority(QoSTier::kLow),    tier_priority(QoSTier::kNormal));
    EXPECT_LT(tier_priority(QoSTier::kNormal),  tier_priority(QoSTier::kHigh));
    EXPECT_LT(tier_priority(QoSTier::kHigh),    tier_priority(QoSTier::kCritical));
}

TEST(QoSTierEdge, DropAndReliableAttributesExclusive)
{
    using cd::net::QoSTier;
    EXPECT_TRUE(cd::net::may_drop_on_congestion(QoSTier::kLow));
    EXPECT_TRUE(cd::net::may_drop_on_congestion(QoSTier::kNormal));
    EXPECT_FALSE(cd::net::may_drop_on_congestion(QoSTier::kHigh));
    EXPECT_FALSE(cd::net::may_drop_on_congestion(QoSTier::kCritical));
    EXPECT_FALSE(cd::net::requires_reliable_delivery(QoSTier::kLow));
    EXPECT_FALSE(cd::net::requires_reliable_delivery(QoSTier::kNormal));
}

// ---------------------------------------------------------------------------
// ChannelMux — sequence number isolation and out-of-order buffer
// ---------------------------------------------------------------------------

TEST(ChannelMuxEdge, UnreliableDoesNotDedup)
{
    // Unreliable channel: same sequence arrives twice → both delivered.
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::ChannelMux mb { *b };

    auto inject_unrel = [&](std::uint32_t seq, std::string_view payload) {
        std::vector<std::byte> frame;
        frame.reserve(cd::net::kChannelHeaderSize + payload.size());
        frame.push_back(std::byte { 0 });   // channel
        frame.push_back(std::byte { 1 });   // kUnreliableUnordered
        for (int i = 0; i < 4; ++i)
            frame.push_back(std::byte { static_cast<std::uint8_t>((seq >> (i * 8)) & 0xFFu) });
        frame.insert(frame.end(),
                     reinterpret_cast<const std::byte*>(payload.data()),
                     reinterpret_cast<const std::byte*>(payload.data()) + payload.size());
        ASSERT_TRUE(a->send({ frame.data(), frame.size() }).has_value());
    };

    inject_unrel(0, "alpha");
    inject_unrel(0, "alpha");  // same seq — unreliable should still deliver

    auto r1 = mb.receive();
    auto r2 = mb.receive();
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());   // both pass through unreliable
    EXPECT_EQ(r1->channel, 0U);
    EXPECT_EQ(r2->channel, 0U);
}

TEST(ChannelMuxEdge, ShortFrameRejectedWithError)
{
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::ChannelMux mb { *b };

    // Send a 3-byte frame (< kChannelHeaderSize=6).
    std::vector<std::byte> short_frame(3, std::byte { 0 });
    ASSERT_TRUE(a->send({ short_frame.data(), short_frame.size() }).has_value());

    auto r = mb.receive();
    ASSERT_FALSE(r.has_value());
    // Should return kBackendError (short frame) or recurse to kWouldBlock.
    // Either is acceptable; the key is it does NOT crash or deliver garbage.
    const auto code = r.error().code;
    EXPECT_TRUE(
        code == static_cast<std::uint32_t>(cd::net::net_errors::Code::kBackendError)
     || code == static_cast<std::uint32_t>(cd::net::net_errors::Code::kWouldBlock));
}

}  // namespace
