#include <cd/asset_streaming/Streaming.hpp>

#include <gtest/gtest.h>

namespace
{

using cd::asset_streaming::AssetKind;
using cd::asset_streaming::Request;
using cd::asset_streaming::Scheduler;

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

}  // namespace
