// =============================================================================
// CHROMODYNAMIC -- cd::ui::animation Easing + Tweener + Timeline tests
//
// Phase 3.2 of ADR-20260530-ui-widget-library. Validates the Penner easing
// reference values, the Tweener<T> playhead semantics, and the multi-
// channel Timeline scheduler.
//
// Reference values pinned against easings.net (which derives directly from
// Penner's original 2001 code) so the curves are cross-engine compatible
// with web tooling, gsap, and Unity Mathf.
// =============================================================================
#include <cd/ui/animation/Animation.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <tuple>

namespace ani = cd::ui::animation;

namespace
{
constexpr float kEps = 1e-4F;
}  // namespace

// ---- Linear ----------------------------------------------------------------

TEST(Easing, LinearMidpointIsHalf)
{
    EXPECT_NEAR(ani::ease(0.0F, ani::Easing::kLinear), 0.0F, kEps);
    EXPECT_NEAR(ani::ease(0.5F, ani::Easing::kLinear), 0.5F, kEps);
    EXPECT_NEAR(ani::ease(1.0F, ani::Easing::kLinear), 1.0F, kEps);
}

// ---- EaseInQuad ------------------------------------------------------------

TEST(Easing, EaseInQuadHalfIsQuarter)
{
    // t^2 at t=0.5 -> 0.25
    EXPECT_NEAR(ani::ease(0.5F, ani::Easing::kEaseInQuad), 0.25F, kEps);
    EXPECT_NEAR(ani::ease(0.0F, ani::Easing::kEaseInQuad), 0.0F, kEps);
    EXPECT_NEAR(ani::ease(1.0F, ani::Easing::kEaseInQuad), 1.0F, kEps);
}

// ---- EaseOutQuad / EaseInOutQuad / Cubic family endpoints ------------------

TEST(Easing, MonotonicFamilyEndpointsAreClean)
{
    const ani::Easing curves[] = {
        ani::Easing::kEaseOutQuad,
        ani::Easing::kEaseInOutQuad,
        ani::Easing::kEaseInCubic,
        ani::Easing::kEaseOutCubic,
        ani::Easing::kEaseInOutCubic,
    };
    for (auto c : curves)
    {
        EXPECT_NEAR(ani::ease(0.0F, c), 0.0F, kEps) << static_cast<int>(c);
        EXPECT_NEAR(ani::ease(1.0F, c), 1.0F, kEps) << static_cast<int>(c);
    }
    // Specific samples
    // EaseOutQuad at t=0.5 -> 1 - (0.5)^2 = 0.75
    EXPECT_NEAR(ani::ease(0.5F, ani::Easing::kEaseOutQuad),  0.75F, kEps);
    // EaseInCubic  at t=0.5 -> 0.125
    EXPECT_NEAR(ani::ease(0.5F, ani::Easing::kEaseInCubic),  0.125F, kEps);
    // EaseOutCubic at t=0.5 -> 1 - 0.125 = 0.875
    EXPECT_NEAR(ani::ease(0.5F, ani::Easing::kEaseOutCubic), 0.875F, kEps);
    // In/Out variants are symmetric around 0.5 -> 0.5
    EXPECT_NEAR(ani::ease(0.5F, ani::Easing::kEaseInOutQuad),  0.5F, kEps);
    EXPECT_NEAR(ani::ease(0.5F, ani::Easing::kEaseInOutCubic), 0.5F, kEps);
}

// ---- Monotonicity (linear / quad / cubic must be non-decreasing) -----------

TEST(Easing, MonotonicFamilyIsNonDecreasing)
{
    const ani::Easing curves[] = {
        ani::Easing::kLinear,
        ani::Easing::kEaseInQuad,
        ani::Easing::kEaseOutQuad,
        ani::Easing::kEaseInOutQuad,
        ani::Easing::kEaseInCubic,
        ani::Easing::kEaseOutCubic,
        ani::Easing::kEaseInOutCubic,
    };
    for (auto c : curves)
    {
        float prev = ani::ease(0.0F, c);
        for (int i = 1; i <= 100; ++i)
        {
            const float t   = static_cast<float>(i) / 100.0F;
            const float cur = ani::ease(t, c);
            EXPECT_GE(cur, prev - kEps)
                << "curve=" << static_cast<int>(c) << " t=" << t;
            prev = cur;
        }
    }
}

// ---- EaseOutBack overshoots above 1.0 then returns to 1.0 ------------------

TEST(Easing, EaseOutBackOvershootsThenSettles)
{
    // Endpoints exact
    EXPECT_NEAR(ani::ease(0.0F, ani::Easing::kEaseOutBack), 0.0F, kEps);
    EXPECT_NEAR(ani::ease(1.0F, ani::Easing::kEaseOutBack), 1.0F, kEps);

    // At some t in (0,1) the curve goes strictly above 1.0 (overshoot).
    // With c1 = 1.70158, the peak is near t ~ 0.74-ish and is roughly
    // 1.0998. We just check there exists a sample > 1.0.
    bool saw_overshoot = false;
    for (int i = 1; i < 100; ++i)
    {
        const float t = static_cast<float>(i) / 100.0F;
        if (ani::ease(t, ani::Easing::kEaseOutBack) > 1.0F + 1e-3F)
        {
            saw_overshoot = true;
            break;
        }
    }
    EXPECT_TRUE(saw_overshoot);
}

// ---- EaseOutBounce produces multiple peaks (4-segment piecewise) -----------

TEST(Easing, EaseOutBounceHasMultiplePeaks)
{
    // Sample densely; count direction changes (down -> up transitions).
    // A monotonic curve has 0 direction changes; outBounce should have >= 3.
    int dir_changes = 0;
    float prev = ani::ease(0.0F, ani::Easing::kEaseOutBounce);
    float prev_delta = 0.0F;
    for (int i = 1; i <= 400; ++i)
    {
        const float t   = static_cast<float>(i) / 400.0F;
        const float cur = ani::ease(t, ani::Easing::kEaseOutBounce);
        const float dlt = cur - prev;
        if (prev_delta < 0.0F && dlt > 0.0F)
        {
            ++dir_changes;
        }
        prev_delta = dlt;
        prev       = cur;
    }
    EXPECT_GE(dir_changes, 3);
    // Endpoints still clean
    EXPECT_NEAR(ani::ease(0.0F, ani::Easing::kEaseOutBounce), 0.0F, kEps);
    EXPECT_NEAR(ani::ease(1.0F, ani::Easing::kEaseOutBounce), 1.0F, kEps);
}

// ---- EaseOutElastic endpoints ---------------------------------------------

TEST(Easing, EaseOutElasticEndpointsAreClean)
{
    EXPECT_NEAR(ani::ease(0.0F, ani::Easing::kEaseOutElastic), 0.0F, kEps);
    EXPECT_NEAR(ani::ease(1.0F, ani::Easing::kEaseOutElastic), 1.0F, kEps);
}

// ---- Tweener<float> linear 0->100 over 1s ----------------------------------

TEST(Tweener, LinearFloatHalfwayIsFifty)
{
    ani::Tweener<float> tw;
    ani::Animation<float> a {};
    a.from       = 0.0F;
    a.to         = 100.0F;
    a.duration_s = 1.0F;
    a.easing     = ani::Easing::kLinear;
    tw.start(a);

    tw.tick(0.5F);
    EXPECT_NEAR(tw.value(), 50.0F, 1e-3F);
    EXPECT_FALSE(tw.done());
}

// ---- Tweener::done() flips at duration -------------------------------------

TEST(Tweener, DoneTrueAtDuration)
{
    ani::Tweener<float> tw;
    ani::Animation<float> a {};
    a.from       = 10.0F;
    a.to         = 20.0F;
    a.duration_s = 1.0F;
    a.easing     = ani::Easing::kLinear;
    tw.start(a);

    EXPECT_FALSE(tw.done());
    tw.tick(0.999F);
    EXPECT_FALSE(tw.done());
    tw.tick(0.002F);  // total elapsed >= 1.0
    EXPECT_TRUE(tw.done());
    EXPECT_NEAR(tw.value(), 20.0F, kEps);
}

// ---- Tweener restart resets playhead ---------------------------------------

TEST(Tweener, RestartResetsPlayhead)
{
    ani::Tweener<float> tw;
    ani::Animation<float> a {};
    a.from       = 0.0F;
    a.to         = 100.0F;
    a.duration_s = 1.0F;
    tw.start(a);
    tw.tick(2.0F);
    EXPECT_TRUE(tw.done());
    EXPECT_NEAR(tw.value(), 100.0F, kEps);

    tw.start(a);
    EXPECT_FALSE(tw.done());
    EXPECT_NEAR(tw.value(), 0.0F, kEps);
    tw.tick(0.25F);
    EXPECT_NEAR(tw.value(), 25.0F, 1e-3F);
}

// ---- Timeline sequence (anim1 [0..1s], anim2 [1..2s]) ----------------------

TEST(Timeline, SequencePlaysSequentially)
{
    ani::Timeline tl;
    ani::Animation<float> a1 {};
    a1.from       = 0.0F;
    a1.to         = 10.0F;
    a1.duration_s = 1.0F;
    a1.easing     = ani::Easing::kLinear;

    ani::Animation<float> a2 {};
    a2.from       = 100.0F;
    a2.to         = 200.0F;
    a2.duration_s = 1.0F;
    a2.easing     = ani::Easing::kLinear;

    const auto ch1 = tl.add(0.0F, a1);
    const auto ch2 = tl.add(1.0F, a2);

    // Before any ticking
    EXPECT_NEAR(tl.value_for(ch1),   0.0F, kEps);
    EXPECT_NEAR(tl.value_for(ch2), 100.0F, kEps);

    // After 0.5s -> ch1 mid, ch2 still pinned at from
    tl.tick(0.5F);
    EXPECT_NEAR(tl.value_for(ch1),   5.0F, 1e-3F);
    EXPECT_NEAR(tl.value_for(ch2), 100.0F, kEps);
    EXPECT_FALSE(tl.done());

    // After +0.5s (total 1.0) -> ch1 reached `to`, ch2 just starting
    tl.tick(0.5F);
    EXPECT_NEAR(tl.value_for(ch1),  10.0F, kEps);
    EXPECT_NEAR(tl.value_for(ch2), 100.0F, kEps);

    // After +0.5s (total 1.5) -> ch1 still `to`, ch2 mid (150)
    tl.tick(0.5F);
    EXPECT_NEAR(tl.value_for(ch1),  10.0F, kEps);
    EXPECT_NEAR(tl.value_for(ch2), 150.0F, 1e-3F);
    EXPECT_FALSE(tl.done());

    // After +0.6s (total 2.1) -> both done; pinned at `to`
    tl.tick(0.6F);
    EXPECT_NEAR(tl.value_for(ch1),  10.0F, kEps);
    EXPECT_NEAR(tl.value_for(ch2), 200.0F, kEps);
    EXPECT_TRUE(tl.done());
}

// ---- Timeline reset --------------------------------------------------------

TEST(Timeline, ResetReturnsToOrigin)
{
    ani::Timeline tl;
    ani::Animation<float> a {};
    a.from       = 0.0F;
    a.to         = 1.0F;
    a.duration_s = 1.0F;
    const auto ch = tl.add(0.0F, a);

    tl.tick(2.0F);
    EXPECT_TRUE(tl.done());
    EXPECT_NEAR(tl.value_for(ch), 1.0F, kEps);

    tl.reset();
    EXPECT_NEAR(tl.now_s(), 0.0F, kEps);
    EXPECT_NEAR(tl.value_for(ch), 0.0F, kEps);
    EXPECT_FALSE(tl.done());
}

// ---- Timeline ignores negative ticks ---------------------------------------

TEST(Timeline, NegativeTickIgnored)
{
    ani::Timeline tl;
    ani::Animation<float> a {};
    a.from       = 0.0F;
    a.to         = 1.0F;
    a.duration_s = 1.0F;
    const auto ch = tl.add(0.0F, a);

    tl.tick(0.5F);
    const float v_mid = tl.value_for(ch);
    tl.tick(-1.0F);
    EXPECT_NEAR(tl.value_for(ch), v_mid, kEps);
    EXPECT_NEAR(tl.now_s(), 0.5F, kEps);
}

// ---- Tweener with zero duration short-circuits to `to` ---------------------

TEST(Tweener, ZeroDurationReturnsTo)
{
    ani::Tweener<float> tw;
    ani::Animation<float> a {};
    a.from       = 5.0F;
    a.to         = 42.0F;
    a.duration_s = 0.0F;
    tw.start(a);
    EXPECT_NEAR(tw.value(), 42.0F, kEps);
}

// ---- Tweener: negative dt treated as zero (elapsed unchanged) --------------

TEST(Tweener, NegativeDtTreatedAsZero)
{
    ani::Tweener<float> tw;
    ani::Animation<float> a {};
    a.from       = 0.0F;
    a.to         = 10.0F;
    a.duration_s = 1.0F;
    a.easing     = ani::Easing::kLinear;
    tw.start(a);
    tw.tick(0.4F);
    const float v_before = tw.value();
    tw.tick(-0.5F);
    EXPECT_NEAR(tw.value(), v_before, kEps);
    EXPECT_NEAR(tw.elapsed_s(), 0.4F, kEps);
    EXPECT_FALSE(tw.done());
}

// ---- Tweener: tick after done() has no further effect ----------------------

TEST(Tweener, TickAfterDoneIsNoOp)
{
    ani::Tweener<float> tw;
    ani::Animation<float> a {};
    a.from       = 0.0F;
    a.to         = 1.0F;
    a.duration_s = 0.5F;
    a.easing     = ani::Easing::kLinear;
    tw.start(a);
    tw.tick(1.0F);
    EXPECT_TRUE(tw.done());
    EXPECT_NEAR(tw.value(), 1.0F, kEps);
    EXPECT_NEAR(tw.elapsed_s(), 0.5F, kEps);

    // Further ticks must not change elapsed or value.
    tw.tick(2.0F);
    EXPECT_NEAR(tw.value(), 1.0F, kEps);
    EXPECT_NEAR(tw.elapsed_s(), 0.5F, kEps);
}

// ---- Tweener: default-constructed is immediately done() --------------------

TEST(Tweener, DefaultConstructedIsDone)
{
    const ani::Tweener<float> tw;
    // active_ starts false -> done() is true; value() returns default-T (0).
    EXPECT_TRUE(tw.done());
    EXPECT_NEAR(tw.value(), 0.0F, kEps);
}

// ---- Easing: every curve maps t=0 to 0 and t=1 to 1 -----------------------

TEST(Easing, AllCurvesHaveCleanEndpoints)
{
    // Includes overshoot curves: elastic/back boundaries are exact by design.
    const ani::Easing all_curves[] = {
        ani::Easing::kLinear,
        ani::Easing::kEaseInQuad,
        ani::Easing::kEaseOutQuad,
        ani::Easing::kEaseInOutQuad,
        ani::Easing::kEaseInCubic,
        ani::Easing::kEaseOutCubic,
        ani::Easing::kEaseInOutCubic,
        ani::Easing::kEaseOutBack,
        ani::Easing::kEaseOutElastic,
        ani::Easing::kEaseOutBounce,
    };
    for (auto c : all_curves)
    {
        EXPECT_NEAR(ani::ease(0.0F, c), 0.0F, kEps) << "curve=" << static_cast<int>(c) << " at t=0";
        EXPECT_NEAR(ani::ease(1.0F, c), 1.0F, kEps) << "curve=" << static_cast<int>(c) << " at t=1";
    }
}

// ---- Easing: back overshoot peak is bounded above ~1.2 ---------------------

TEST(Easing, EaseOutBackOvershootBoundedBelow1p15)
{
    // c1=1.70158 -> theoretical peak ≈ 1.0998. We assert it never exceeds 1.2
    // (sanity bound) and never dips below 0 (t=0 → 0; curve is monotone first).
    for (int i = 0; i <= 100; ++i)
    {
        const float t = static_cast<float>(i) / 100.0F;
        const float v = ani::ease(t, ani::Easing::kEaseOutBack);
        EXPECT_LE(v, 1.15F) << "kEaseOutBack peak above 1.15 at t=" << t;
        EXPECT_GE(v, -0.1F) << "kEaseOutBack below -0.1 at t=" << t;
    }
}

// ---- Easing: elastic oscillates above 1 at some interior t ----------------

TEST(Easing, EaseOutElasticOvershoots)
{
    // The damped sine produces values > 1.0 somewhere before settling at 1.
    bool saw_above = false;
    for (int i = 1; i < 99; ++i)
    {
        const float t = static_cast<float>(i) / 100.0F;
        if (ani::ease(t, ani::Easing::kEaseOutElastic) > 1.0F + 1e-3F)
        {
            saw_above = true;
            break;
        }
    }
    EXPECT_TRUE(saw_above);
}

// ---- Easing: bounce stays within [0..1] everywhere ------------------------

TEST(Easing, EaseOutBounceStaysInUnitInterval)
{
    for (int i = 0; i <= 200; ++i)
    {
        const float t = static_cast<float>(i) / 200.0F;
        const float v = ani::ease(t, ani::Easing::kEaseOutBounce);
        EXPECT_GE(v, -kEps) << "kEaseOutBounce below 0 at t=" << t;
        EXPECT_LE(v, 1.0F + kEps) << "kEaseOutBounce above 1 at t=" << t;
    }
}

// ---- Easing: EaseInOutQuad is symmetric (f(1-t) == 1-f(t)) ----------------

TEST(Easing, EaseInOutQuadIsSymmetric)
{
    for (int i = 0; i <= 50; ++i)
    {
        const float t    = static_cast<float>(i) / 100.0F;
        const float ft   = ani::ease(t,        ani::Easing::kEaseInOutQuad);
        const float fmt  = ani::ease(1.0F - t, ani::Easing::kEaseInOutQuad);
        EXPECT_NEAR(ft + fmt, 1.0F, kEps) << "symmetry broken at t=" << t;
    }
}

// ---- Easing: EaseInOutCubic is symmetric ----------------------------------

TEST(Easing, EaseInOutCubicIsSymmetric)
{
    for (int i = 0; i <= 50; ++i)
    {
        const float t    = static_cast<float>(i) / 100.0F;
        const float ft   = ani::ease(t,        ani::Easing::kEaseInOutCubic);
        const float fmt  = ani::ease(1.0F - t, ani::Easing::kEaseInOutCubic);
        EXPECT_NEAR(ft + fmt, 1.0F, kEps) << "symmetry broken at t=" << t;
    }
}

// ---- Easing: out-of-range t is clamped ------------------------------------

TEST(Easing, OutOfRangeTIsClamped)
{
    // t < 0 clamps to 0; t > 1 clamps to 1 (monotonic family).
    const ani::Easing mono[] = {
        ani::Easing::kLinear,
        ani::Easing::kEaseInQuad,
        ani::Easing::kEaseOutQuad,
        ani::Easing::kEaseInCubic,
        ani::Easing::kEaseOutCubic,
    };
    for (auto c : mono)
    {
        EXPECT_NEAR(ani::ease(-1.0F, c), 0.0F, kEps) << "curve=" << static_cast<int>(c);
        EXPECT_NEAR(ani::ease( 2.0F, c), 1.0F, kEps) << "curve=" << static_cast<int>(c);
    }
}

// ---- Timeline: empty timeline is immediately done() -----------------------

TEST(Timeline, EmptyTimelineIsDone)
{
    const ani::Timeline tl;
    EXPECT_TRUE(tl.done());
    EXPECT_EQ(tl.channel_count(), 0U);
    EXPECT_NEAR(tl.now_s(), 0.0F, kEps);
}

// ---- Timeline: invalid channel returns 0.0F -------------------------------

TEST(Timeline, InvalidChannelReturnsZero)
{
    ani::Timeline tl;
    ani::Animation<float> a {};
    a.from       = 5.0F;
    a.to         = 10.0F;
    a.duration_s = 1.0F;
    std::ignore = tl.add(0.0F, a);
    tl.tick(0.5F);

    EXPECT_NEAR(tl.value_for(ani::kInvalidChannel), 0.0F, kEps);
    // Out-of-range index (well past valid channels) is also guarded.
    const ani::ChannelId oob { 999U };
    EXPECT_NEAR(tl.value_for(oob), 0.0F, kEps);
}

// ---- Timeline: negative at_time_s clamped to 0 ----------------------------

TEST(Timeline, NegativeStartTimeClamped)
{
    ani::Timeline tl;
    ani::Animation<float> a {};
    a.from       = 0.0F;
    a.to         = 1.0F;
    a.duration_s = 1.0F;
    a.easing     = ani::Easing::kLinear;
    // Schedule with a negative start time; should behave as if start_s == 0.
    const auto ch = tl.add(-5.0F, a);
    // At now=0, the channel has start_s=0 so it is at its start, value = from.
    EXPECT_NEAR(tl.value_for(ch), 0.0F, kEps);
    tl.tick(0.5F);
    EXPECT_NEAR(tl.value_for(ch), 0.5F, kEps);
    tl.tick(0.5F);
    EXPECT_NEAR(tl.value_for(ch), 1.0F, kEps);
    EXPECT_TRUE(tl.done());
}

// ---- Timeline: seek via reset + bulk tick to target time ------------------

TEST(Timeline, SeekViaResetAndBulkTick)
{
    ani::Timeline tl;
    ani::Animation<float> a {};
    a.from       = 0.0F;
    a.to         = 10.0F;
    a.duration_s = 2.0F;
    a.easing     = ani::Easing::kLinear;
    const auto ch = tl.add(0.0F, a);

    // Advance to 1.0s (mid-point).
    tl.tick(1.0F);
    EXPECT_NEAR(tl.value_for(ch), 5.0F, kEps);

    // "Seek" back to t=0.5s: reset then tick to target.
    tl.reset();
    tl.tick(0.5F);
    EXPECT_NEAR(tl.value_for(ch), 2.5F, kEps);
    EXPECT_NEAR(tl.now_s(), 0.5F, kEps);
}

// ---- Timeline: overlapping channels sum independently ----------------------

TEST(Timeline, OverlappingChannelsAreIndependent)
{
    // Two channels start at the same time and run independently.
    ani::Timeline tl;

    ani::Animation<float> a1 {};
    a1.from       = 0.0F;
    a1.to         = 100.0F;
    a1.duration_s = 1.0F;
    a1.easing     = ani::Easing::kLinear;

    ani::Animation<float> a2 {};
    a2.from       = 200.0F;
    a2.to         = 0.0F;
    a2.duration_s = 1.0F;
    a2.easing     = ani::Easing::kLinear;

    const auto ch1 = tl.add(0.0F, a1);
    const auto ch2 = tl.add(0.0F, a2);

    tl.tick(0.25F);
    EXPECT_NEAR(tl.value_for(ch1),  25.0F, 1e-3F);
    EXPECT_NEAR(tl.value_for(ch2), 150.0F, 1e-3F);

    tl.tick(0.75F);  // total 1.0 -> both at `to`
    EXPECT_NEAR(tl.value_for(ch1), 100.0F, kEps);
    EXPECT_NEAR(tl.value_for(ch2),   0.0F, kEps);
    EXPECT_TRUE(tl.done());
}
