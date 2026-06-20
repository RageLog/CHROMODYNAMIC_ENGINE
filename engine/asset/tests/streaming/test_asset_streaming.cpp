#include <cd/asset/streaming/Streaming.hpp>

#include <gtest/gtest.h>

namespace
{

using cd::asset::streaming::AssetKind;
using cd::asset::streaming::Request;
using cd::asset::streaming::Scheduler;

TEST(AssetStreaming, EmptySchedulerYieldsNothing)
{
    Scheduler s(1024);
    Request out;
    EXPECT_FALSE(s.next(out));
}

TEST(AssetStreaming, HighestPriorityServedFirst)
{
    Scheduler s(10000);
    s.enqueue({ 1, AssetKind::kMesh, 0, 0.5F, 100 });
    s.enqueue({ 2, AssetKind::kMesh, 0, 0.9F, 100 });
    s.enqueue({ 3, AssetKind::kMesh, 0, 0.3F, 100 });
    Request out {};
    ASSERT_TRUE(s.next(out));
    EXPECT_EQ(out.asset_id, 2U);
}

TEST(AssetStreaming, BudgetSkipsOversizedRequests)
{
    Scheduler s(50);
    s.enqueue({ 1, AssetKind::kMesh, 0, 1.0F, 100 });   // too big
    s.enqueue({ 2, AssetKind::kMesh, 0, 0.1F,  30 });   // fits
    Request out {};
    ASSERT_TRUE(s.next(out));
    EXPECT_EQ(out.asset_id, 2U);
    // The oversized one stayed pending.
    EXPECT_EQ(s.pending_count(), 1U);
}

TEST(AssetStreaming, CompleteFreesBudget)
{
    Scheduler s(50);
    Request a { 1, AssetKind::kMesh, 0, 1.0F, 40 };
    s.enqueue(a);
    Request out {};
    ASSERT_TRUE(s.next(out));
    EXPECT_EQ(s.in_flight(), 40U);
    s.complete(out);
    EXPECT_EQ(s.in_flight(), 0U);
}

// =============================================================================
// Robustness / edge coverage (≥80→100 marathon, ADD-ONLY).
// =============================================================================

TEST(AssetStreamingEdge, OversizedRequestNeverServedButStaysPending)
{
    // A single request larger than the whole budget can never be served, yet
    // it must not be dropped — it stays pending for a future budget bump.
    Scheduler s(100);
    s.enqueue({ 1, AssetKind::kTexture, 0, 1.0F, 500 });
    Request out {};
    EXPECT_FALSE(s.next(out));
    EXPECT_EQ(s.pending_count(), 1U);
    EXPECT_EQ(s.in_flight(), 0U);
}

TEST(AssetStreamingEdge, SkippedRequestsAreRequeuedNotLost)
{
    // Highest priority is oversized; a smaller lower-priority one must still
    // be served, and the skipped oversized one stays in the queue.
    Scheduler s(50);
    s.enqueue({ 1, AssetKind::kMesh, 0, 0.9F, 100 });  // oversized, top priority
    s.enqueue({ 2, AssetKind::kMesh, 0, 0.1F, 20 });   // fits
    Request out {};
    ASSERT_TRUE(s.next(out));
    EXPECT_EQ(out.asset_id, 2U);
    EXPECT_EQ(s.pending_count(), 1U);  // oversized request still queued
}

TEST(AssetStreamingEdge, CompleteWithoutMatchingServeClampsAtZero)
{
    // Over-completion (completing more bytes than are in flight) must clamp
    // in_flight to 0 rather than underflow the unsigned counter.
    Scheduler s(1000);
    Request phantom { 9, AssetKind::kAudio, 0, 1.0F, 999 };
    s.complete(phantom);  // nothing was ever served
    EXPECT_EQ(s.in_flight(), 0U);
}

TEST(AssetStreamingEdge, ZeroBudgetServesOnlyZeroByteRequests)
{
    Scheduler s(0);
    s.enqueue({ 1, AssetKind::kMesh, 0, 1.0F, 0 });   // zero-byte fits
    s.enqueue({ 2, AssetKind::kMesh, 0, 2.0F, 1 });   // one byte does not
    Request out {};
    ASSERT_TRUE(s.next(out));
    EXPECT_EQ(out.asset_id, 1U);  // 2 is higher priority but oversized
    EXPECT_EQ(s.in_flight(), 0U);
}

TEST(AssetStreamingEdge, EqualPriorityBothEventuallyDrain)
{
    Scheduler s(1000);
    s.enqueue({ 1, AssetKind::kMesh, 0, 0.5F, 100 });
    s.enqueue({ 2, AssetKind::kMesh, 0, 0.5F, 100 });
    Request a {};
    Request b {};
    ASSERT_TRUE(s.next(a));
    ASSERT_TRUE(s.next(b));
    EXPECT_NE(a.asset_id, b.asset_id);
    EXPECT_EQ(s.pending_count(), 0U);
    EXPECT_EQ(s.in_flight(), 200U);
}

}  // namespace
