// =============================================================================
// CHROMODYNAMIC -- samples/engine/hello_engine/SponzaFixtures.hpp
//
// T1.7 phase543 (Sponza golden-image readback).
//
// The 5 fixed Tier 1 camera fixtures shared between:
//   * the hello_engine sample (CLI flag --golden-fixture N drives the
//     camera + the swapchain-readback PNG dump path)
//   * the test_sponza_golden gtest (CPU-synth tests + the subprocess-
//     driven GPU compare gated by CD_SPONZA_GOLDEN_REQUIRE=1).
//
// Keeping the fixture table in one TU-neutral header guarantees the
// sample and the test cannot drift -- a regression like "test points at
// camera 3 but sample uses camera 2" is impossible to write.
//
// Values are nominal Sponza-Atrium coordinates (entrance at -X end of
// the nave, vegetation in the NE corner outdoor pots, floor-reflection
// looking down at the marble floor near the central well). They are
// committed to source so the test stays reproducible regardless of which
// Sponza glTF revision is on disk.
// =============================================================================
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace cd::hello_engine::sponza_fixtures
{

/// One Tier 1 fixture: filename slug + camera pose + UI tint.
struct Fixture
{
    std::string_view slug;        ///< sponza_<slug>.png filename
    std::string_view description; ///< human-readable header / docs
    float            eye[3];      ///< world-space camera position
    float            target[3];   ///< world-space look-at target
    float            fov_y_deg;   ///< vertical FOV in degrees
    /// "Mood" tint applied to the CPU-synth fallback gradient in the
    /// imgdiff tests. Distinct per fixture so a regression in one
    /// fixture's setup is never silently absorbed by another.
    float            mood[3];
};

/// Render dimensions for the captured PNG. 256x144 keeps the file
/// small + diff-friendly while matching the existing reference set.
inline constexpr std::uint32_t kFixtureWidth  = 256U;
inline constexpr std::uint32_t kFixtureHeight = 144U;

/// Five fixed camera poses. Index = fixture id; slug = filename suffix.
inline constexpr std::array<Fixture, 5> kFixtures { {
    { "entrance",   "Sponza main entrance looking down the nave",
      { -11.50F, 1.60F,  0.00F }, {  0.00F, 1.60F,  0.00F }, 60.0F,
      { 1.00F, 0.85F, 0.60F } },
    { "nave",       "Mid-nave looking at the far apse",
      {  -2.00F, 2.40F,  0.00F }, {  8.00F, 2.20F,  0.00F }, 55.0F,
      { 0.95F, 0.90F, 0.80F } },
    { "arch",       "Side-arch looking through the colonnade",
      {   0.00F, 4.80F, -2.50F }, {  0.00F, 1.20F,  4.50F }, 65.0F,
      { 0.80F, 0.85F, 1.00F } },
    { "vegetation", "Close-up of the foliage / alpha-test plants",
      {   6.00F, 1.30F,  3.50F }, {  3.80F, 1.10F,  2.00F }, 35.0F,
      { 0.55F, 0.95F, 0.60F } },
    { "floor",      "Looking down at the marble floor for reflections",
      {   0.00F, 3.20F,  0.00F }, {  0.50F, 0.00F,  0.40F }, 70.0F,
      { 0.90F, 0.92F, 1.00F } },
} };

/// Convenience: total fixture count for range checks at the CLI parsers.
inline constexpr std::size_t kFixtureCount = kFixtures.size();

}  // namespace cd::hello_engine::sponza_fixtures
