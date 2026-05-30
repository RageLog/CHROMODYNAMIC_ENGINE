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
