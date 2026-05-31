// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_animator/tests/test_animator.cpp
//
// phase556-557-558 — unit tests for cd::editor::panel::animator::Animator.
//
// All tests are headless (no ImGui / no RHI). We verify:
//   * DefaultCtorHasNoClip            — clip_id() == kInvalidClipId,
//                                        time/duration/playing/looping defaults.
//   * SetClipRoundTrips               — set_clip / clip_id round-trip + clear.
//   * AddRemoveClips                  — clip browser add/remove/dedup behaviour.
//   * SetTimeRoundTrips               — set_time / time round-trip + negative clamp.
//   * SetDurationRoundTrips           — set_duration / duration + negative clamp.
//   * PlayPauseLoopToggles            — set_playing / is_playing + set_looping.
//   * DrawDefaultDoesNotCrash         — draw() with default state emits background.
//   * DrawWithClipAndTimeEmitsQuads   — draw() with active clip + non-zero time
//                                        emits scrubber fill geometry.
//   * DrawZeroBoundsDoesNotCrash      — draw() on a zero-size rect returns early.
// =============================================================================
#include <cd/editor/panel_animator/Animator.hpp>

#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

namespace anim = cd::editor::panel::animator;

// ---------------------------------------------------------------------------
// TEST(AnimatorPanel, DefaultCtorHasNoClip)
// ---------------------------------------------------------------------------
TEST(AnimatorPanel, DefaultCtorHasNoClip)
{
    const anim::Animator animator;

    EXPECT_EQ(animator.clip_id(), anim::kInvalidClipId);
    EXPECT_EQ(animator.clip_count(), static_cast<std::size_t>(0U));
    EXPECT_FLOAT_EQ(animator.time(),     0.0F);
    EXPECT_FLOAT_EQ(animator.duration(), 0.0F);
    EXPECT_FALSE(animator.is_playing());
    EXPECT_FALSE(animator.is_looping());
}

// ---------------------------------------------------------------------------
// TEST(AnimatorPanel, SetClipRoundTrips)
// ---------------------------------------------------------------------------
TEST(AnimatorPanel, SetClipRoundTrips)
{
    anim::Animator animator;

    animator.set_clip(42U);
    EXPECT_EQ(animator.clip_id(), 42U);

    // Rebind to a different id.
    animator.set_clip(7U);
    EXPECT_EQ(animator.clip_id(), 7U);

    // Clearing with the sentinel.
    animator.set_clip(anim::kInvalidClipId);
    EXPECT_EQ(animator.clip_id(), anim::kInvalidClipId);
}

// ---------------------------------------------------------------------------
// TEST(AnimatorPanel, AddRemoveClips)
// ---------------------------------------------------------------------------
TEST(AnimatorPanel, AddRemoveClips)
{
    anim::Animator animator;

    // Empty at start.
    EXPECT_EQ(animator.clip_count(), static_cast<std::size_t>(0U));

    // Add three clips.
    animator.add_clip(1U);
    animator.add_clip(2U);
    animator.add_clip(3U);
    EXPECT_EQ(animator.clip_count(), static_cast<std::size_t>(3U));

    // Duplicate add is silently ignored.
    animator.add_clip(2U);
    EXPECT_EQ(animator.clip_count(), static_cast<std::size_t>(3U));

    // Remove one clip.
    animator.remove_clip(2U);
    EXPECT_EQ(animator.clip_count(), static_cast<std::size_t>(2U));

    // Remove non-existent is a no-op.
    animator.remove_clip(99U);
    EXPECT_EQ(animator.clip_count(), static_cast<std::size_t>(2U));

    // Remove all.
    animator.remove_clip(1U);
    animator.remove_clip(3U);
    EXPECT_EQ(animator.clip_count(), static_cast<std::size_t>(0U));
}

// ---------------------------------------------------------------------------
// TEST(AnimatorPanel, SetTimeRoundTrips)
// ---------------------------------------------------------------------------
TEST(AnimatorPanel, SetTimeRoundTrips)
{
    anim::Animator animator;

    animator.set_time(1.5F);
    EXPECT_FLOAT_EQ(animator.time(), 1.5F);

    // Negative values are clamped to 0.
    animator.set_time(-0.5F);
    EXPECT_FLOAT_EQ(animator.time(), 0.0F);

    // Zero is accepted.
    animator.set_time(0.0F);
    EXPECT_FLOAT_EQ(animator.time(), 0.0F);
}

// ---------------------------------------------------------------------------
// TEST(AnimatorPanel, SetDurationRoundTrips)
// ---------------------------------------------------------------------------
TEST(AnimatorPanel, SetDurationRoundTrips)
{
    anim::Animator animator;

    animator.set_duration(3.0F);
    EXPECT_FLOAT_EQ(animator.duration(), 3.0F);

    // Negative values are clamped to 0.
    animator.set_duration(-1.0F);
    EXPECT_FLOAT_EQ(animator.duration(), 0.0F);
}

// ---------------------------------------------------------------------------
// TEST(AnimatorPanel, PlayPauseLoopToggles)
// ---------------------------------------------------------------------------
TEST(AnimatorPanel, PlayPauseLoopToggles)
{
    anim::Animator animator;

    EXPECT_FALSE(animator.is_playing());
    animator.set_playing(true);
    EXPECT_TRUE(animator.is_playing());
    animator.set_playing(false);
    EXPECT_FALSE(animator.is_playing());

    EXPECT_FALSE(animator.is_looping());
    animator.set_looping(true);
    EXPECT_TRUE(animator.is_looping());
    animator.set_looping(false);
    EXPECT_FALSE(animator.is_looping());
}

// ---------------------------------------------------------------------------
// TEST(AnimatorPanel, DrawDefaultDoesNotCrash)
// ---------------------------------------------------------------------------
TEST(AnimatorPanel, DrawDefaultDoesNotCrash)
{
    anim::Animator                animator;
    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 400.0F, 600.0F };

    batcher.begin_frame();
    // Must not throw or crash.
    animator.draw(batcher, theme, bounds);

    // Background quad must have been emitted (at least one command).
    EXPECT_GE(batcher.command_count(), static_cast<std::size_t>(1U));
}

// ---------------------------------------------------------------------------
// TEST(AnimatorPanel, DrawWithClipAndTimeEmitsQuads)
// ---------------------------------------------------------------------------
TEST(AnimatorPanel, DrawWithClipAndTimeEmitsQuads)
{
    anim::Animator animator;
    animator.add_clip(1U);
    animator.add_clip(2U);
    animator.set_clip(1U);
    animator.set_duration(10.0F);
    animator.set_time(5.0F);   // 50% through — scrubber fill is non-zero.
    animator.set_playing(true);
    animator.set_looping(true);

    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 400.0F, 600.0F };

    batcher.begin_frame();
    animator.draw(batcher, theme, bounds);

    // Expect: background + separator + 2 clip bars (track + highlight each) +
    //         sub-separator + scrubber track + scrubber fill + play indicator +
    //         loop indicator = 11+ quads = 44+ verts.
    EXPECT_GE(batcher.vertex_count(), static_cast<std::size_t>(44U));
}

// ---------------------------------------------------------------------------
// TEST(AnimatorPanel, DrawZeroBoundsDoesNotCrash)
// ---------------------------------------------------------------------------
TEST(AnimatorPanel, DrawZeroBoundsDoesNotCrash)
{
    anim::Animator                animator;
    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   zero { 0.0F, 0.0F, 0.0F, 0.0F };

    batcher.begin_frame();
    // Must not throw or crash.
    animator.draw(batcher, theme, zero);

    // bounds.is_valid() == false → returns early after the background quad.
    // Row quads must NOT be emitted, vertex count stays very low.
    EXPECT_LE(batcher.vertex_count(), static_cast<std::size_t>(8U));
}
