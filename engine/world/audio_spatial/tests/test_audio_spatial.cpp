// =============================================================================
// CHROMODYNAMIC — cd::audio::spatial tests
// Phase 693/gap-closure — ILD, distance attenuation (3 models), Doppler,
// edge cases, negative tests, and math verification.
//
// Original test matrix (1-10):
//  1. Source directly to the RIGHT  → right_gain > left_gain.
//  2. Source directly in FRONT       → left_gain ≈ right_gain (balanced).
//  3. Source BEHIND listener          → mostly behind (L/R split vs. azimuth).
//  4. Distance > outer radius         → near-zero attenuation.
//  5. Source velocity toward listener → Doppler pitch > 1.0 (higher pitch).
//  6. Multiple sources tracked independently.
//  7. remove_source() reduces count + returns default mix.
//  8. Source inside inner radius      → attenuation = 1.0.
//  9. update_source_position() changes computed mix.
// 10. Doppler: source moving AWAY → pitch < 1.0.
//
// Gap-closure tests (11-37):
// 11. Attenuation kLinear math: atten = inner/d at midpoint.
// 12. Attenuation kInverseSquare math: atten = (inner/d)^2 at midpoint.
// 13. Attenuation kExponential math: atten = exp(-rf*(d-inner)/inner) at midpoint.
// 14. All models: d == inner → atten = 1.0.
// 15. All models: d == outer → atten = 0.0.
// 16. Listener and source at SAME position → no NaN, returns valid mix.
// 17. Source BEHIND listener → panning is balanced (azimuth dot ≈ 0).
// 18. Source above listener (Y axis) → balanced panning.
// 19. Doppler: both listener and source stationary → pitch ≈ 1.0.
// 20. Doppler: zero source velocity → pitch = (c + v_listener) / c.
// 21. Doppler: both moving same direction same speed → pitch ≈ 1.0.
// 22. Doppler: listener approaching source → pitch > 1.0.
// 23. Doppler: source moving at near-sound-speed toward listener → clamped.
// 24. Source gain=2.0 → left_gain + right_gain scaled by gain.
// 25. Source gain=0.0 → both channels silent.
// 26. compute_mix on unknown id → returns default unity mix.
// 27. update_source_position on unknown id → no crash (no-op).
// 28. add_source with duplicate id → overwrites previous.
// 29. remove_source on unknown id → no crash (no-op).
// 30. Source far to right (azimuth_dot ≈ +1) → right_gain ≈ src.gain * atten.
// 31. Source far to left  (azimuth_dot ≈ -1) → left_gain  ≈ src.gain * atten.
// 32. Listener faces +X (90° yaw) → source at +X is "in front" (balanced).
// 33. kExponential: rolloff_factor=0 treated as 1.0 (no divide-by-zero).
// 34. kExponential: large rolloff_factor → steeply decays, atten < kLinear at same d.
// 35. kInverseSquare: decays faster than kLinear at same distance.
// 36. Attenuation outer < inner config → outer clamped to inner+epsilon, no crash.
// 37. Multiple attenuations models on different sources in same mixer.
// =============================================================================

#include <cd/audio/spatial/AudioSpatial.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <numbers>

namespace
{

// Convenience: build a listener at origin, facing forward (-Z), identity quat.
cd::audio::spatial::Listener make_listener_at_origin()
{
    cd::audio::spatial::Listener l{};
    l.position         = { 0.0F, 0.0F, 0.0F };
    l.orientation_quat = { 0.0F, 0.0F, 0.0F, 1.0F };  // identity
    l.velocity         = { 0.0F, 0.0F, 0.0F };
    return l;
}

// Convenience: minimal source with id and position.
cd::audio::spatial::SoundSource make_source(
    uint64_t id,
    std::array<float, 3> pos,
    float inner_radius = 1.0F,
    float outer_radius = 50.0F)
{
    cd::audio::spatial::SoundSource s{};
    s.id           = id;
    s.position     = pos;
    s.gain         = 1.0F;
    s.pitch        = 1.0F;
    s.radius_inner = inner_radius;
    s.radius_outer = outer_radius;
    s.loop         = false;
    return s;
}

// ---------------------------------------------------------------------------
// TEST 1 — Source to the RIGHT of listener → right_gain > left_gain
//
// Identity quat: listener faces -Z, right ear at +X.
// Source at {5, 0, 0} is directly to the right → right channel louder.
// ---------------------------------------------------------------------------
TEST(AudioSpatial_ILD, SourceToRightPansRight)
{
    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());

    const auto src = make_source(1u, { 5.0F, 0.0F, 0.0F });
    mixer.add_source(src);

    const auto mix = mixer.compute_mix(1u);

    EXPECT_GT(mix.right_gain, mix.left_gain)
        << "Source at +X should pan right (right_gain=" << mix.right_gain
        << " left_gain=" << mix.left_gain << ")";
}

// ---------------------------------------------------------------------------
// TEST 2 — Source directly IN FRONT → left_gain ≈ right_gain (balanced)
//
// Listener faces -Z (identity quat); source at {0, 0, -5}.
// Azimuth dot product with right_world = {1,0,0} is 0 → equal panning.
// ---------------------------------------------------------------------------
TEST(AudioSpatial_ILD, SourceInFrontIsBalanced)
{
    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());

    const auto src = make_source(2u, { 0.0F, 0.0F, -5.0F });
    mixer.add_source(src);

    const auto mix = mixer.compute_mix(2u);

    EXPECT_NEAR(mix.left_gain, mix.right_gain, 0.05F)
        << "Source in front should produce balanced L/R panning"
        << " (left=" << mix.left_gain << " right=" << mix.right_gain << ")";
}

// ---------------------------------------------------------------------------
// TEST 3 — Source to the LEFT → left_gain > right_gain
//
// Source at {-5, 0, 0} → left of listener.
// ---------------------------------------------------------------------------
TEST(AudioSpatial_ILD, SourceToLeftPansLeft)
{
    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());

    const auto src = make_source(3u, { -5.0F, 0.0F, 0.0F });
    mixer.add_source(src);

    const auto mix = mixer.compute_mix(3u);

    EXPECT_GT(mix.left_gain, mix.right_gain)
        << "Source at -X should pan left (left_gain=" << mix.left_gain
        << " right_gain=" << mix.right_gain << ")";
}

// ---------------------------------------------------------------------------
// TEST 4 — Distance BEYOND outer radius → attenuation near zero
//
// Source outer_radius=10 m, place source at 100 m.  Attenuation must be 0.
// ---------------------------------------------------------------------------
TEST(AudioSpatial_Distance, BeyondOuterRadiusIsZero)
{
    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());

    auto src = make_source(4u, { 100.0F, 0.0F, 0.0F }, /*inner_radius=*/1.0F, /*outer_radius=*/10.0F);
    mixer.add_source(src);

    const auto mix = mixer.compute_mix(4u);

    EXPECT_NEAR(mix.distance_attenuation, 0.0F, 1e-6F)
        << "Attenuation must be 0 beyond outer radius";
    EXPECT_NEAR(mix.left_gain,  0.0F, 1e-5F);
    EXPECT_NEAR(mix.right_gain, 0.0F, 1e-5F);
}

// ---------------------------------------------------------------------------
// TEST 5 — Source inside inner radius → attenuation == 1.0
//
// Source inner_radius=5 m, source at 2 m.
// ---------------------------------------------------------------------------
TEST(AudioSpatial_Distance, InsideInnerRadiusIsUnity)
{
    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());

    auto src = make_source(5u, { 2.0F, 0.0F, 0.0F }, /*inner_radius=*/5.0F, /*outer_radius=*/50.0F);
    mixer.add_source(src);

    const auto mix = mixer.compute_mix(5u);

    EXPECT_NEAR(mix.distance_attenuation, 1.0F, 1e-5F)
        << "Attenuation must be 1.0 inside inner radius (got "
        << mix.distance_attenuation << ")";
}

// ---------------------------------------------------------------------------
// TEST 6 — Doppler: source moving TOWARD listener → pitch > 1.0
//
// Listener at origin, source at {10, 0, 0} moving toward listener (velocity -X).
// The source is closing in → higher pitch.
// ---------------------------------------------------------------------------
TEST(AudioSpatial_Doppler, SourceMovingTowardListenerRaisesPitch)
{
    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());

    auto src = make_source(6u, { 10.0F, 0.0F, 0.0F });
    src.velocity = { -30.0F, 0.0F, 0.0F };  // moving toward listener at 30 m/s
    mixer.add_source(src);

    const auto mix = mixer.compute_mix(6u);

    EXPECT_GT(mix.doppler_pitch, 1.0F)
        << "Source approaching listener should raise pitch (got "
        << mix.doppler_pitch << ")";
}

// ---------------------------------------------------------------------------
// TEST 7 — Doppler: source moving AWAY from listener → pitch < 1.0
//
// Source at {10, 0, 0} moving away (velocity +X).
// ---------------------------------------------------------------------------
TEST(AudioSpatial_Doppler, SourceMovingAwayLowersPitch)
{
    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());

    auto src = make_source(7u, { 10.0F, 0.0F, 0.0F });
    src.velocity = { 30.0F, 0.0F, 0.0F };  // moving away at 30 m/s
    mixer.add_source(src);

    const auto mix = mixer.compute_mix(7u);

    EXPECT_LT(mix.doppler_pitch, 1.0F)
        << "Source receding from listener should lower pitch (got "
        << mix.doppler_pitch << ")";
}

// ---------------------------------------------------------------------------
// TEST 8 — Multiple sources tracked independently
//
// Three sources at different positions; each must produce a distinct mix
// that reflects its own spatial position.
// ---------------------------------------------------------------------------
TEST(AudioSpatial_MultiSource, SourcesTrackedIndependently)
{
    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());

    // Source A: far right
    auto a = make_source(10u, { 5.0F, 0.0F, 0.0F });
    // Source B: far left
    auto b = make_source(11u, { -5.0F, 0.0F, 0.0F });
    // Source C: very far away (beyond outer)
    auto c = make_source(12u, { 200.0F, 0.0F, 0.0F }, 1.0F, 10.0F);

    mixer.add_source(a);
    mixer.add_source(b);
    mixer.add_source(c);

    EXPECT_EQ(mixer.source_count(), 3u);

    const auto mix_a = mixer.compute_mix(10u);
    const auto mix_b = mixer.compute_mix(11u);
    const auto mix_c = mixer.compute_mix(12u);

    // A pans right
    EXPECT_GT(mix_a.right_gain, mix_a.left_gain) << "Source A should pan right";
    // B pans left
    EXPECT_GT(mix_b.left_gain, mix_b.right_gain) << "Source B should pan left";
    // C is silent
    EXPECT_NEAR(mix_c.distance_attenuation, 0.0F, 1e-6F) << "Source C should be silent";
}

// ---------------------------------------------------------------------------
// TEST 9 — remove_source() reduces count and returns default mix
// ---------------------------------------------------------------------------
TEST(AudioSpatial_Management, RemoveSourceReducesCount)
{
    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());

    mixer.add_source(make_source(20u, { 1.0F, 0.0F, 0.0F }));
    mixer.add_source(make_source(21u, { 2.0F, 0.0F, 0.0F }));

    EXPECT_EQ(mixer.source_count(), 2u);

    mixer.remove_source(20u);
    EXPECT_EQ(mixer.source_count(), 1u);

    // compute_mix on removed source must return default (no crash, unit gains)
    const auto mix = mixer.compute_mix(20u);
    EXPECT_NEAR(mix.left_gain,            1.0F, 1e-5F);
    EXPECT_NEAR(mix.right_gain,           1.0F, 1e-5F);
    EXPECT_NEAR(mix.doppler_pitch,        1.0F, 1e-5F);
    EXPECT_NEAR(mix.distance_attenuation, 1.0F, 1e-5F);
}

// ---------------------------------------------------------------------------
// TEST 10 — update_source_position() changes the panning result
//
// Source starts at right (+X), then moves to left (-X).
// The mix should flip from right-dominant to left-dominant.
// ---------------------------------------------------------------------------
TEST(AudioSpatial_Management, UpdatePositionChangesMix)
{
    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());

    auto src = make_source(30u, { 5.0F, 0.0F, 0.0F });
    mixer.add_source(src);

    const auto mix_before = mixer.compute_mix(30u);
    EXPECT_GT(mix_before.right_gain, mix_before.left_gain) << "Should pan right initially";

    // Move source to the left
    mixer.update_source_position(30u, { -5.0F, 0.0F, 0.0F });

    const auto mix_after = mixer.compute_mix(30u);
    EXPECT_GT(mix_after.left_gain, mix_after.right_gain) << "Should pan left after move";
}

// =============================================================================
// GAP-CLOSURE TESTS (11-37)
// =============================================================================

// ---------------------------------------------------------------------------
// TEST 11 — kLinear attenuation math at midpoint
//
// inner=2m, outer=10m, d=4m → atten = inner/d = 2/4 = 0.5
// ---------------------------------------------------------------------------
TEST(AudioSpatial_Attenuation, LinearMathAtMidpoint)
{
    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());

    cd::audio::spatial::SoundSource src = make_source(101u, { 4.0F, 0.0F, 0.0F },
                                                       /*inner_radius=*/2.0F, /*outer_radius=*/10.0F);
    src.attenuation_model = cd::audio::spatial::AttenuationModel::kLinear;
    mixer.add_source(src);

    const auto mix = mixer.compute_mix(101u);
    EXPECT_NEAR(mix.distance_attenuation, 2.0F / 4.0F, 1e-5F)
        << "kLinear atten at d=4 (inner=2) should be 0.5";
}

// ---------------------------------------------------------------------------
// TEST 12 — kInverseSquare attenuation math at midpoint
//
// inner=2m, outer=10m, d=4m → atten = (inner/d)^2 = (0.5)^2 = 0.25
// ---------------------------------------------------------------------------
TEST(AudioSpatial_Attenuation, InverseSquareMathAtMidpoint)
{
    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());

    cd::audio::spatial::SoundSource src = make_source(102u, { 4.0F, 0.0F, 0.0F },
                                                       /*inner_radius=*/2.0F, /*outer_radius=*/10.0F);
    src.attenuation_model = cd::audio::spatial::AttenuationModel::kInverseSquare;
    mixer.add_source(src);

    const auto mix = mixer.compute_mix(102u);
    EXPECT_NEAR(mix.distance_attenuation, (2.0F / 4.0F) * (2.0F / 4.0F), 1e-5F)
        << "kInverseSquare atten at d=4 (inner=2) should be 0.25";
}

// ---------------------------------------------------------------------------
// TEST 13 — kExponential attenuation math at midpoint
//
// inner=2m, outer=10m, d=4m, rolloff=1.0
// → atten = exp(-1.0 * (4-2) / 2) = exp(-1) ≈ 0.36788
// ---------------------------------------------------------------------------
TEST(AudioSpatial_Attenuation, ExponentialMathAtMidpoint)
{
    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());

    cd::audio::spatial::SoundSource src = make_source(103u, { 4.0F, 0.0F, 0.0F },
                                                       /*inner_radius=*/2.0F, /*outer_radius=*/10.0F);
    src.attenuation_model  = cd::audio::spatial::AttenuationModel::kExponential;
    src.rolloff_factor     = 1.0F;
    mixer.add_source(src);

    const float expected = std::exp(-1.0F * (4.0F - 2.0F) / 2.0F);  // exp(-1)
    const auto mix = mixer.compute_mix(103u);
    EXPECT_NEAR(mix.distance_attenuation, expected, 1e-5F)
        << "kExponential atten at d=4 (inner=2, rolloff=1) should be exp(-1)";
}

// ---------------------------------------------------------------------------
// TEST 14 — All models: d == inner → attenuation == 1.0
// ---------------------------------------------------------------------------
TEST(AudioSpatial_Attenuation, AtInnerRadiusIsUnityAllModels)
{
    for (const auto model : {
             cd::audio::spatial::AttenuationModel::kLinear,
             cd::audio::spatial::AttenuationModel::kInverseSquare,
             cd::audio::spatial::AttenuationModel::kExponential,
         })
    {
        cd::audio::spatial::SpatialMixer mixer;
        mixer.set_listener(make_listener_at_origin());

        // Place source exactly at inner radius (5 m on X axis)
        cd::audio::spatial::SoundSource src = make_source(110u, { 5.0F, 0.0F, 0.0F },
                                                           /*inner_radius=*/5.0F, /*outer_radius=*/50.0F);
        src.attenuation_model = model;
        mixer.add_source(src);

        const auto mix = mixer.compute_mix(110u);
        EXPECT_NEAR(mix.distance_attenuation, 1.0F, 1e-5F)
            << "Attenuation at d==inner must be 1.0 for model="
            << static_cast<int>(model);
    }
}

// ---------------------------------------------------------------------------
// TEST 15 — All models: d >= outer → attenuation == 0.0
// ---------------------------------------------------------------------------
TEST(AudioSpatial_Attenuation, AtOrBeyondOuterRadiusIsZeroAllModels)
{
    for (const auto model : {
             cd::audio::spatial::AttenuationModel::kLinear,
             cd::audio::spatial::AttenuationModel::kInverseSquare,
             cd::audio::spatial::AttenuationModel::kExponential,
         })
    {
        cd::audio::spatial::SpatialMixer mixer;
        mixer.set_listener(make_listener_at_origin());

        // Source at exactly outer radius
        cd::audio::spatial::SoundSource src = make_source(111u, { 20.0F, 0.0F, 0.0F },
                                                           /*inner_radius=*/1.0F, /*outer_radius=*/20.0F);
        src.attenuation_model = model;
        mixer.add_source(src);

        const auto mix = mixer.compute_mix(111u);
        EXPECT_NEAR(mix.distance_attenuation, 0.0F, 1e-5F)
            << "Attenuation at d==outer must be 0.0 for model="
            << static_cast<int>(model);

        // Source well beyond outer radius
        mixer.update_source_position(111u, { 1000.0F, 0.0F, 0.0F });
        const auto mix2 = mixer.compute_mix(111u);
        EXPECT_NEAR(mix2.distance_attenuation, 0.0F, 1e-5F)
            << "Attenuation beyond outer must be 0.0 for model="
            << static_cast<int>(model);
    }
}

// ---------------------------------------------------------------------------
// TEST 16 — Listener and source at SAME position → no NaN, valid mix
//
// Same position → dist=0 → default direction {0,0,-1} used, no division.
// ---------------------------------------------------------------------------
TEST(AudioSpatial_Edge, SamePositionNoNaN)
{
    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());

    const auto src = make_source(120u, { 0.0F, 0.0F, 0.0F },
                                 /*inner_radius=*/1.0F, /*outer_radius=*/20.0F);
    mixer.add_source(src);

    const auto mix = mixer.compute_mix(120u);

    EXPECT_FALSE(std::isnan(mix.left_gain))            << "left_gain must not be NaN";
    EXPECT_FALSE(std::isnan(mix.right_gain))           << "right_gain must not be NaN";
    EXPECT_FALSE(std::isnan(mix.doppler_pitch))        << "doppler_pitch must not be NaN";
    EXPECT_FALSE(std::isnan(mix.distance_attenuation)) << "distance_attenuation must not be NaN";

    // dist=0 < inner=1 → attenuation must be 1.0
    EXPECT_NEAR(mix.distance_attenuation, 1.0F, 1e-5F);
}

// ---------------------------------------------------------------------------
// TEST 17 — Source directly BEHIND listener → balanced panning
//
// Identity quat: listener faces -Z; right = +X.
// Source at {0, 0, +5} is directly behind → direction = {0,0,+1}.
// dot with right={1,0,0} = 0 → balanced L/R.
// ---------------------------------------------------------------------------
TEST(AudioSpatial_ILD, SourceBehindListenerIsBalanced)
{
    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());

    const auto src = make_source(121u, { 0.0F, 0.0F, 5.0F });
    mixer.add_source(src);

    const auto mix = mixer.compute_mix(121u);
    EXPECT_NEAR(mix.left_gain, mix.right_gain, 0.05F)
        << "Source directly behind should be balanced L/R"
        << " (left=" << mix.left_gain << " right=" << mix.right_gain << ")";
}

// ---------------------------------------------------------------------------
// TEST 18 — Source directly ABOVE listener → balanced panning
//
// Direction = {0,+1,0}; dot with right={1,0,0} = 0 → balanced.
// ---------------------------------------------------------------------------
TEST(AudioSpatial_ILD, SourceAboveListenerIsBalanced)
{
    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());

    const auto src = make_source(122u, { 0.0F, 10.0F, 0.0F });
    mixer.add_source(src);

    const auto mix = mixer.compute_mix(122u);
    EXPECT_NEAR(mix.left_gain, mix.right_gain, 0.05F)
        << "Source directly above should be balanced L/R";
}

// ---------------------------------------------------------------------------
// TEST 19 — Doppler: both stationary → pitch ≈ 1.0
// ---------------------------------------------------------------------------
TEST(AudioSpatial_Doppler, BothStationaryPitchIsUnity)
{
    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());  // velocity = {0,0,0}

    auto src = make_source(130u, { 10.0F, 0.0F, 0.0F });
    src.velocity = { 0.0F, 0.0F, 0.0F };
    mixer.add_source(src);

    const auto mix = mixer.compute_mix(130u);
    EXPECT_NEAR(mix.doppler_pitch, 1.0F, 1e-4F)
        << "Both stationary: pitch must be 1.0 (got " << mix.doppler_pitch << ")";
}

// ---------------------------------------------------------------------------
// TEST 20 — Doppler: zero source velocity → pitch = (c + v_l) / c
//
// Listener moving toward source at 34.3 m/s (10% of c=343).
// Source at +X, listener velocity = {+34.3, 0, 0}? No — listener moving
// TOWARD source means velocity in +X direction if source is at +X.
// Wait: src_to_listener = -to_src = {-1,0,0}; v_listener =
//   dot({+34.3,0,0}, {-1,0,0}) = -34.3.  That makes pitch lower.
//
// Let us verify correctly: source at +X means direction listener→source = +X.
// src_to_listener = -that = -X = {-1,0,0}.
// Listener velocity {-34.3,0,0} (moving in -X, i.e., TOWARD the source):
//   v_listener = dot({-34.3,0,0}, {-1,0,0}) = +34.3.
//   pitch = (343+34.3)/343 ≈ 1.1.
// ---------------------------------------------------------------------------
TEST(AudioSpatial_Doppler, ZeroSourceVelocityPitchMatchesFormula)
{
    cd::audio::spatial::Listener l{};
    l.position         = { 0.0F, 0.0F, 0.0F };
    l.orientation_quat = { 0.0F, 0.0F, 0.0F, 1.0F };
    l.velocity         = { -34.3F, 0.0F, 0.0F };  // moving toward source at +X

    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(l);

    auto src = make_source(131u, { 50.0F, 0.0F, 0.0F });
    src.velocity = { 0.0F, 0.0F, 0.0F };
    mixer.add_source(src);

    const auto mix = mixer.compute_mix(131u);
    // pitch = (343 + 34.3) / 343 ≈ 1.1 (base pitch = 1.0)
    const float expected = (343.0F + 34.3F) / 343.0F;
    EXPECT_NEAR(mix.doppler_pitch, expected, 0.002F)
        << "Listener approaching, zero source velocity: pitch should be "
        << expected << " (got " << mix.doppler_pitch << ")";
}

// ---------------------------------------------------------------------------
// TEST 21 — Doppler: listener moving PERPENDICULAR to source axis → pitch ≈ 1.0
//
// Source is at {50, 0, 0} (along +X from listener).
// Listener moves in +Z direction: its velocity component along X axis is 0.
// Source stationary: v_source component along src_to_listener axis is also 0.
// ⇒ numerator = c + 0 = c, denominator = c - 0 = c → pitch = 1.0.
// ---------------------------------------------------------------------------
TEST(AudioSpatial_Doppler, PerpendicularMotionNoDopplerShift)
{
    cd::audio::spatial::Listener l{};
    l.position         = { 0.0F, 0.0F, 0.0F };
    l.orientation_quat = { 0.0F, 0.0F, 0.0F, 1.0F };
    l.velocity         = { 0.0F, 0.0F, 30.0F };  // perpendicular to source axis (+Z)

    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(l);

    // Source along +X — perpendicular to listener motion (+Z)
    const auto src = make_source(132u, { 50.0F, 0.0F, 0.0F });
    mixer.add_source(src);

    const auto mix = mixer.compute_mix(132u);
    // src_to_listener = {-1,0,0}; dot({0,0,30},{-1,0,0}) = 0 → v_listener = 0
    // v_source_toward = 0 → pitch = (343+0)/(343-0) = 1.0
    EXPECT_NEAR(mix.doppler_pitch, 1.0F, 0.002F)
        << "Perpendicular motion: Doppler pitch must be 1.0 (got " << mix.doppler_pitch << ")";
}

// ---------------------------------------------------------------------------
// TEST 22 — Doppler: listener approaching stationary source → pitch > 1.0
// ---------------------------------------------------------------------------
TEST(AudioSpatial_Doppler, ListenerApproachingRaisesPitch)
{
    cd::audio::spatial::Listener l{};
    l.position         = { 0.0F, 0.0F, 0.0F };
    l.orientation_quat = { 0.0F, 0.0F, 0.0F, 1.0F };
    l.velocity         = { -20.0F, 0.0F, 0.0F };  // toward source at +X

    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(l);

    const auto src = make_source(133u, { 30.0F, 0.0F, 0.0F });
    mixer.add_source(src);

    const auto mix = mixer.compute_mix(133u);
    EXPECT_GT(mix.doppler_pitch, 1.0F)
        << "Listener approaching source: pitch should be > 1.0 (got " << mix.doppler_pitch << ")";
}

// ---------------------------------------------------------------------------
// TEST 23 — Doppler: source at near-sound-speed → clamped to [0.1, 4.0]
// ---------------------------------------------------------------------------
TEST(AudioSpatial_Doppler, ExtremeDopplerIsClamped)
{
    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());

    auto src = make_source(134u, { 10.0F, 0.0F, 0.0F });
    // Source moving at 500 m/s toward listener (supersonic)
    src.velocity = { -500.0F, 0.0F, 0.0F };
    mixer.add_source(src);

    const auto mix = mixer.compute_mix(134u);
    EXPECT_LE(mix.doppler_pitch, 4.0F)
        << "Extreme Doppler must clamp to 4.0 (got " << mix.doppler_pitch << ")";
    EXPECT_GE(mix.doppler_pitch, 0.1F)
        << "Extreme Doppler must clamp to 0.1 minimum (got " << mix.doppler_pitch << ")";
}

// ---------------------------------------------------------------------------
// TEST 24 — Source gain=2.0 → channels scaled by gain
// ---------------------------------------------------------------------------
TEST(AudioSpatial_Gain, GainScalesChannels)
{
    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());

    // Source at inner radius (attenuation=1.0), balanced pan (in front),
    // gain=2.0 → each channel ≈ 0.5 * 2.0 = 1.0
    cd::audio::spatial::SoundSource src = make_source(140u, { 0.0F, 0.0F, -2.0F },
                                                       /*inner_radius=*/2.0F, /*outer_radius=*/50.0F);
    src.gain = 2.0F;
    mixer.add_source(src);

    const auto mix = mixer.compute_mix(140u);
    EXPECT_NEAR(mix.distance_attenuation, 1.0F, 1e-5F);  // at inner radius
    // With unity atten, balanced pan: each ILD=0.5, scaled by gain=2 → 1.0
    EXPECT_NEAR(mix.left_gain,  1.0F, 0.05F)
        << "gain=2 should scale left channel";
    EXPECT_NEAR(mix.right_gain, 1.0F, 0.05F)
        << "gain=2 should scale right channel";
}

// ---------------------------------------------------------------------------
// TEST 25 — Source gain=0.0 → both channels silent
// ---------------------------------------------------------------------------
TEST(AudioSpatial_Gain, ZeroGainSilentChannels)
{
    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());

    cd::audio::spatial::SoundSource src = make_source(141u, { 0.0F, 0.0F, -5.0F });
    src.gain = 0.0F;
    mixer.add_source(src);

    const auto mix = mixer.compute_mix(141u);
    EXPECT_NEAR(mix.left_gain,  0.0F, 1e-6F) << "gain=0 must silence left";
    EXPECT_NEAR(mix.right_gain, 0.0F, 1e-6F) << "gain=0 must silence right";
}

// ---------------------------------------------------------------------------
// TEST 26 — compute_mix on unknown id returns default unity mix
// ---------------------------------------------------------------------------
TEST(AudioSpatial_Negative, ComputeMixUnknownIdReturnsDefault)
{
    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());

    const auto mix = mixer.compute_mix(9999u);
    EXPECT_NEAR(mix.left_gain,            1.0F, 1e-5F);
    EXPECT_NEAR(mix.right_gain,           1.0F, 1e-5F);
    EXPECT_NEAR(mix.doppler_pitch,        1.0F, 1e-5F);
    EXPECT_NEAR(mix.distance_attenuation, 1.0F, 1e-5F);
}

// ---------------------------------------------------------------------------
// TEST 27 — update_source_position on unknown id → no crash (no-op)
// ---------------------------------------------------------------------------
TEST(AudioSpatial_Negative, UpdatePositionUnknownIdNoOp)
{
    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());

    // Must not crash or assert.
    mixer.update_source_position(8888u, { 1.0F, 2.0F, 3.0F });
    EXPECT_EQ(mixer.source_count(), 0u);
}

// ---------------------------------------------------------------------------
// TEST 28 — add_source with duplicate id overwrites previous state
// ---------------------------------------------------------------------------
TEST(AudioSpatial_Management, DuplicateIdOverwrites)
{
    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());

    // Add source to the right
    const auto src_right = make_source(50u, { 10.0F, 0.0F, 0.0F });
    mixer.add_source(src_right);
    EXPECT_EQ(mixer.source_count(), 1u);

    const auto mix_right = mixer.compute_mix(50u);
    EXPECT_GT(mix_right.right_gain, mix_right.left_gain);

    // Overwrite with source to the left
    const auto src_left = make_source(50u, { -10.0F, 0.0F, 0.0F });
    mixer.add_source(src_left);
    EXPECT_EQ(mixer.source_count(), 1u) << "Count must not grow on duplicate id";

    const auto mix_left = mixer.compute_mix(50u);
    EXPECT_GT(mix_left.left_gain, mix_left.right_gain)
        << "After overwrite, source should now pan left";
}

// ---------------------------------------------------------------------------
// TEST 29 — remove_source on unknown id → no crash (no-op)
// ---------------------------------------------------------------------------
TEST(AudioSpatial_Negative, RemoveUnknownIdIsNoOp)
{
    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());
    mixer.add_source(make_source(60u, { 1.0F, 0.0F, 0.0F }));

    mixer.remove_source(7777u);  // unknown — must not crash
    EXPECT_EQ(mixer.source_count(), 1u) << "Existing source must survive remove of unknown id";
}

// ---------------------------------------------------------------------------
// TEST 30 — Source at far right (azimuth_dot ≈ +1) → right_gain ≈ src.gain * atten
// ---------------------------------------------------------------------------
TEST(AudioSpatial_ILD, FarRightChannelMaximised)
{
    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());

    // Source directly to the right, close enough to be inside inner radius.
    cd::audio::spatial::SoundSource src = make_source(150u, { 0.5F, 0.0F, 0.0F },
                                                       /*inner_radius=*/2.0F, /*outer_radius=*/50.0F);
    src.gain = 1.0F;
    mixer.add_source(src);

    const auto mix = mixer.compute_mix(150u);
    // azimuth_dot = dot({1,0,0}, {1,0,0}) = 1 → right_ild = 1.0, left_ild = 0.0
    EXPECT_NEAR(mix.right_gain, 1.0F, 0.01F)
        << "Fully right source: right_gain should approach 1.0 (got " << mix.right_gain << ")";
    EXPECT_NEAR(mix.left_gain,  0.0F, 0.01F)
        << "Fully right source: left_gain should approach 0.0 (got " << mix.left_gain << ")";
}

// ---------------------------------------------------------------------------
// TEST 31 — Source at far left (azimuth_dot ≈ -1) → left_gain ≈ src.gain * atten
// ---------------------------------------------------------------------------
TEST(AudioSpatial_ILD, FarLeftChannelMaximised)
{
    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());

    cd::audio::spatial::SoundSource src = make_source(151u, { -0.5F, 0.0F, 0.0F },
                                                       /*inner_radius=*/2.0F, /*outer_radius=*/50.0F);
    src.gain = 1.0F;
    mixer.add_source(src);

    const auto mix = mixer.compute_mix(151u);
    EXPECT_NEAR(mix.left_gain,  1.0F, 0.01F)
        << "Fully left source: left_gain should approach 1.0 (got " << mix.left_gain << ")";
    EXPECT_NEAR(mix.right_gain, 0.0F, 0.01F)
        << "Fully left source: right_gain should approach 0.0 (got " << mix.right_gain << ")";
}

// ---------------------------------------------------------------------------
// TEST 32 — Listener facing +X (90° yaw): source at +X is "in front" (balanced)
//
// Rotate 90° around Y axis: quat = {0, sin(45°), 0, cos(45°)}.
// After rotation: listener forward = +X, right = +Z.
// Source at {5,0,0} is directly in front → azimuth_dot with right{0,0,1} = 0.
// ---------------------------------------------------------------------------
TEST(AudioSpatial_ILD, RotatedListenerFront)
{
    cd::audio::spatial::Listener l{};
    l.position = { 0.0F, 0.0F, 0.0F };

    // 90° rotation around +Y: q = (0, sin(π/4), 0, cos(π/4))
    const float s = std::sin(std::numbers::pi_v<float> / 4.0F);
    const float c = std::cos(std::numbers::pi_v<float> / 4.0F);
    l.orientation_quat = { 0.0F, s, 0.0F, c };
    l.velocity = { 0.0F, 0.0F, 0.0F };

    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(l);

    const auto src = make_source(160u, { 5.0F, 0.0F, 0.0F });
    mixer.add_source(src);

    const auto mix = mixer.compute_mix(160u);
    EXPECT_NEAR(mix.left_gain, mix.right_gain, 0.05F)
        << "Rotated listener: source in front should balance L/R "
        << "(left=" << mix.left_gain << " right=" << mix.right_gain << ")";
}

// ---------------------------------------------------------------------------
// TEST 33 — kExponential: rolloff_factor=0 treated as 1.0 (no crash, valid)
// ---------------------------------------------------------------------------
TEST(AudioSpatial_Attenuation, ExponentialZeroRolloffFallsBackToOne)
{
    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());

    cd::audio::spatial::SoundSource src = make_source(170u, { 4.0F, 0.0F, 0.0F },
                                                       /*inner_radius=*/2.0F, /*outer_radius=*/10.0F);
    src.attenuation_model = cd::audio::spatial::AttenuationModel::kExponential;
    src.rolloff_factor    = 0.0F;  // invalid → treated as 1.0
    mixer.add_source(src);

    const auto mix = mixer.compute_mix(170u);
    EXPECT_FALSE(std::isnan(mix.distance_attenuation)) << "No NaN on zero rolloff";
    EXPECT_GE(mix.distance_attenuation, 0.0F);
    EXPECT_LE(mix.distance_attenuation, 1.0F);

    // At rolloff=1 (fallback): exp(-1*(4-2)/2) = exp(-1) ≈ 0.3679
    const float expected = std::exp(-1.0F * (4.0F - 2.0F) / 2.0F);
    EXPECT_NEAR(mix.distance_attenuation, expected, 1e-5F);
}

// ---------------------------------------------------------------------------
// TEST 34 — kExponential large rolloff decays faster than kLinear at same d
// ---------------------------------------------------------------------------
TEST(AudioSpatial_Attenuation, ExponentialLargeRolloffDecaysFaster)
{
    const float inner = 2.0F;
    const float outer = 100.0F;
    const float d     = 6.0F;   // midway but past inner

    auto make_src_model = [&](uint64_t id, cd::audio::spatial::AttenuationModel m,
                               float rolloff) {
        cd::audio::spatial::SoundSource src = make_source(id, { d, 0.0F, 0.0F },
                                                           inner, outer);
        src.attenuation_model = m;
        src.rolloff_factor    = rolloff;
        return src;
    };

    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());
    mixer.add_source(make_src_model(180u, cd::audio::spatial::AttenuationModel::kLinear,      1.0F));
    mixer.add_source(make_src_model(181u, cd::audio::spatial::AttenuationModel::kExponential, 5.0F));

    const float atten_linear = mixer.compute_mix(180u).distance_attenuation;
    const float atten_exp    = mixer.compute_mix(181u).distance_attenuation;

    EXPECT_LT(atten_exp, atten_linear)
        << "kExponential with high rolloff should decay faster than kLinear at same d";
}

// ---------------------------------------------------------------------------
// TEST 35 — kInverseSquare decays faster than kLinear at same distance
// ---------------------------------------------------------------------------
TEST(AudioSpatial_Attenuation, InverseSquareDecaysFasterThanLinear)
{
    const float inner = 2.0F;
    const float outer = 100.0F;
    const float d     = 6.0F;

    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());

    cd::audio::spatial::SoundSource src_lin = make_source(190u, { d, 0.0F, 0.0F }, inner, outer);
    src_lin.attenuation_model = cd::audio::spatial::AttenuationModel::kLinear;
    mixer.add_source(src_lin);

    cd::audio::spatial::SoundSource src_sq = make_source(191u, { d, 0.0F, 0.0F }, inner, outer);
    src_sq.attenuation_model = cd::audio::spatial::AttenuationModel::kInverseSquare;
    mixer.add_source(src_sq);

    const float atten_lin = mixer.compute_mix(190u).distance_attenuation;
    const float atten_sq  = mixer.compute_mix(191u).distance_attenuation;

    EXPECT_LT(atten_sq, atten_lin)
        << "kInverseSquare should attenuate more than kLinear at d=" << d;

    // Mathematical check: lin=inner/d, sq=(inner/d)^2
    EXPECT_NEAR(atten_lin, inner / d,                  1e-5F);
    EXPECT_NEAR(atten_sq,  (inner / d) * (inner / d),  1e-5F);
}

// ---------------------------------------------------------------------------
// TEST 36 — outer < inner config → outer clamped, no crash, valid mix
// ---------------------------------------------------------------------------
TEST(AudioSpatial_Edge, OuterLessThanInnerNocrash)
{
    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());

    // Deliberately bad config: outer < inner
    cd::audio::spatial::SoundSource src = make_source(200u, { 5.0F, 0.0F, 0.0F },
                                                       /*inner_radius=*/10.0F, /*outer_radius=*/2.0F);
    mixer.add_source(src);

    const auto mix = mixer.compute_mix(200u);
    // Implementation clamps outer to inner+0.001 when outer <= inner.
    // Source at d=5 is within [inner=10] → inside inner → attenuation=1.0
    EXPECT_FALSE(std::isnan(mix.distance_attenuation));
    EXPECT_GE(mix.distance_attenuation, 0.0F);
    EXPECT_LE(mix.distance_attenuation, 1.0F);
}

// ---------------------------------------------------------------------------
// TEST 37 — Multiple attenuation models on different sources in same mixer
// ---------------------------------------------------------------------------
TEST(AudioSpatial_MultiSource, MultipleModelsCoexist)
{
    const float inner = 1.0F;
    const float outer = 100.0F;
    const float d     = 4.0F;

    cd::audio::spatial::SpatialMixer mixer;
    mixer.set_listener(make_listener_at_origin());

    cd::audio::spatial::SoundSource s1 = make_source(201u, { d, 0.0F, 0.0F }, inner, outer);
    s1.attenuation_model = cd::audio::spatial::AttenuationModel::kLinear;

    cd::audio::spatial::SoundSource s2 = make_source(202u, { d, 0.0F, 0.0F }, inner, outer);
    s2.attenuation_model = cd::audio::spatial::AttenuationModel::kInverseSquare;

    cd::audio::spatial::SoundSource s3 = make_source(203u, { d, 0.0F, 0.0F }, inner, outer);
    s3.attenuation_model  = cd::audio::spatial::AttenuationModel::kExponential;
    s3.rolloff_factor     = 1.0F;

    mixer.add_source(s1);
    mixer.add_source(s2);
    mixer.add_source(s3);
    EXPECT_EQ(mixer.source_count(), 3u);

    const float a1 = mixer.compute_mix(201u).distance_attenuation;
    const float a2 = mixer.compute_mix(202u).distance_attenuation;
    const float a3 = mixer.compute_mix(203u).distance_attenuation;

    EXPECT_NEAR(a1, inner / d,                          1e-5F) << "kLinear";
    EXPECT_NEAR(a2, (inner / d) * (inner / d),          1e-5F) << "kInverseSquare";
    EXPECT_NEAR(a3, std::exp(-1.0F * (d - inner) / inner), 1e-5F) << "kExponential";

    // Ordering: kLinear > kExponential(rf=1) > kInverseSquare at d=4, inner=1
    // kLinear = 1/4 = 0.25
    // kInvSq  = 1/16 = 0.0625
    // kExp    = exp(-3) ≈ 0.0498
    // So: linear > inv_sq > exp at this distance (both drop quickly).
    // Simply verify all are distinct and in valid range.
    EXPECT_NE(a1, a2);
    EXPECT_NE(a1, a3);
    EXPECT_GE(a1, 0.0F);
    EXPECT_LE(a1, 1.0F);
    EXPECT_GE(a2, 0.0F);
    EXPECT_LE(a2, 1.0F);
    EXPECT_GE(a3, 0.0F);
    EXPECT_LE(a3, 1.0F);
}

}  // anonymous namespace
