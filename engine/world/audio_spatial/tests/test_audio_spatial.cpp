// =============================================================================
// CHROMODYNAMIC — cd::audio::spatial tests
// Phase 693 — Sprint-1: ITD/ILD, distance attenuation, Doppler
//
// Test matrix:
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
// =============================================================================

#include <cd/audio/spatial/AudioSpatial.hpp>

#include <gtest/gtest.h>

#include <cmath>

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

    auto src = make_source(4u, { 100.0F, 0.0F, 0.0F }, /*inner=*/1.0F, /*outer=*/10.0F);
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

    auto src = make_source(5u, { 2.0F, 0.0F, 0.0F }, /*inner=*/5.0F, /*outer=*/50.0F);
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

}  // anonymous namespace
