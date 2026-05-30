// =============================================================================
// CHROMODYNAMIC — cd::net::SnapshotReconciler tests (Phase 468 / M0)
// =============================================================================
#include <cd/net/SnapshotReconciler.hpp>
#include <gtest/gtest.h>

#include <cstdint>

namespace cd::net
{
extern const char* snapshot_reconciler_translation_unit() noexcept;
}

namespace
{

struct Vec3
{
    float x { 0 }, y { 0 }, z { 0 };
    [[nodiscard]] bool operator==(const Vec3& o) const noexcept
    {
        return x == o.x && y == o.y && z == o.z;
    }
};

// Reapply step: forward integrate by (1, 0, 0) per input.
[[nodiscard]] Vec3 reapply_step(const Vec3& prev, std::uint32_t /*seq*/)
{
    return Vec3 { prev.x + 1.0F, prev.y, prev.z };
}

TEST(NetReconciler, AgreementCounts)
{
    cd::net::SnapshotReconciler<Vec3> r;
    r.record_predicted(1, Vec3 { 1, 0, 0 });
    r.record_predicted(2, Vec3 { 2, 0, 0 });
    r.record_predicted(3, Vec3 { 3, 0, 0 });

    auto res = r.apply_authoritative(2, Vec3 { 2, 0, 0 }, reapply_step);
    EXPECT_EQ(res.outcome, cd::net::SnapshotReconciler<Vec3>::ReconcileResult::Outcome::kAgree);
    EXPECT_EQ(res.replayed, 0U);
    EXPECT_EQ(r.agree_count(), 1U);
    EXPECT_EQ(r.correction_count(), 0U);
}

TEST(NetReconciler, MismatchTriggersReplay)
{
    cd::net::SnapshotReconciler<Vec3> r;
    r.record_predicted(1, Vec3 { 1, 0, 0 });
    r.record_predicted(2, Vec3 { 2, 0, 0 });
    r.record_predicted(3, Vec3 { 3, 0, 0 });
    r.record_predicted(4, Vec3 { 4, 0, 0 });

    // Server says: at input 2 your true X was 10 (mispredict by 8).
    auto res = r.apply_authoritative(2, Vec3 { 10, 0, 0 }, reapply_step);
    EXPECT_EQ(res.outcome,
              cd::net::SnapshotReconciler<Vec3>::ReconcileResult::Outcome::kCorrected);
    EXPECT_EQ(res.replayed, 2U);  // seqs 3 and 4 replayed
    EXPECT_EQ(r.correction_count(), 1U);
    EXPECT_EQ(r.last_correction_window(), 2U);

    // After replay, seq 4 should be (10 + 2 = 12, 0, 0).
    auto p4 = r.predicted_at(4);
    ASSERT_TRUE(p4.has_value());
    EXPECT_FLOAT_EQ(p4->x, 12.0F);

    // Original entry at 2 must now hold the authoritative state.
    auto p2 = r.predicted_at(2);
    ASSERT_TRUE(p2.has_value());
    EXPECT_FLOAT_EQ(p2->x, 10.0F);
}

TEST(NetReconciler, StaleSnapshotIsCounted)
{
    cd::net::SnapshotReconciler<Vec3> r { /*capacity=*/4 };
    // Fill the ring beyond capacity.
    for (std::uint32_t s = 1; s <= 10; ++s)
        r.record_predicted(s, Vec3 { static_cast<float>(s), 0, 0 });

    // Seq 1 should have been evicted (capacity=4 ring).
    auto res = r.apply_authoritative(1, Vec3 { 1, 0, 0 }, reapply_step);
    EXPECT_EQ(res.outcome,
              cd::net::SnapshotReconciler<Vec3>::ReconcileResult::Outcome::kStale);
    EXPECT_EQ(r.stale_snapshot_count(), 1U);
    EXPECT_EQ(r.correction_count(), 0U);
}

TEST(NetReconciler, CorrectionWithNoSubsequentPredictionsReplaysZero)
{
    cd::net::SnapshotReconciler<Vec3> r;
    r.record_predicted(7, Vec3 { 99, 0, 0 });

    auto res = r.apply_authoritative(7, Vec3 { 1, 2, 3 }, reapply_step);
    EXPECT_EQ(res.outcome,
              cd::net::SnapshotReconciler<Vec3>::ReconcileResult::Outcome::kCorrected);
    EXPECT_EQ(res.replayed, 0U);  // no seqs > 7 in the ring

    auto p = r.predicted_at(7);
    ASSERT_TRUE(p.has_value());
    EXPECT_FLOAT_EQ(p->x, 1.0F);
    EXPECT_FLOAT_EQ(p->y, 2.0F);
    EXPECT_FLOAT_EQ(p->z, 3.0F);
}

TEST(NetReconciler, CustomEqualToleratesEpsilon)
{
    auto fuzzy = [](const Vec3& a, const Vec3& b) {
        const float dx = a.x - b.x;
        return (dx > -0.01F && dx < 0.01F);
    };
    cd::net::SnapshotReconciler<Vec3> r { /*capacity=*/16,
                                          cd::net::SnapshotReconciler<Vec3>::EqualFn { fuzzy } };
    r.record_predicted(5, Vec3 { 5.0F, 0, 0 });
    auto res = r.apply_authoritative(5, Vec3 { 5.005F, 0, 0 }, reapply_step);
    EXPECT_EQ(res.outcome,
              cd::net::SnapshotReconciler<Vec3>::ReconcileResult::Outcome::kAgree);
    EXPECT_EQ(r.correction_count(), 0U);
}

TEST(NetReconciler, ClearResetsCountersAndRing)
{
    cd::net::SnapshotReconciler<Vec3> r;
    r.record_predicted(1, Vec3 { 1, 0, 0 });
    (void)r.apply_authoritative(1, Vec3 { 1, 0, 0 }, reapply_step);
    EXPECT_EQ(r.agree_count(), 1U);
    EXPECT_EQ(r.size(), 1U);

    r.clear();
    EXPECT_EQ(r.size(), 0U);
    EXPECT_FALSE(r.predicted_at(1).has_value());
    EXPECT_FALSE(r.latest_recorded().has_value());
}

TEST(NetReconciler, SymbolPresentInLibrary)
{
    EXPECT_NE(cd::net::snapshot_reconciler_translation_unit(), nullptr);
}

}  // namespace
