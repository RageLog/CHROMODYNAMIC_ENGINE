// =============================================================================
// cd::post_camera unit tests.
//
// Camera/composition post-fx (vignette, chromatic aberration, film grain) is
// header-only: a Settings struct for editor UI binding + an inline GLSL
// helper string consumed by forward shaders. There is no CPU kernel to
// evaluate, so coverage here is ADD-ONLY host-side contract locking:
//   * Settings field defaults (all effects OFF by default — the library
//     must never bias the rendered frame unless the editor dials a value).
//   * GLSL helper-string sanity (every documented helper present, no
//     accidental truncation of the constexpr string at build time).
//
// AAA throughout. No effect math / GLSL / default param is altered.
// =============================================================================
#include <cd/post/camera/Camera.hpp>

#include <gtest/gtest.h>

#include <string_view>

namespace
{

using cd::post::camera::kInlineCameraGlsl;
using cd::post::camera::Settings;

// ---- Settings default contract ---------------------------------------------
// The whole camera-fx group is opt-in: a freshly constructed Settings must
// leave the rendered frame BYTE-IDENTICAL (every dial at 0). If any default
// drifts off zero a default-on effect would silently change golden output.

TEST(PostCamera, DefaultVignetteIsZero)
{
    const Settings s {};
    EXPECT_FLOAT_EQ(s.vignette, 0.0F);
}

TEST(PostCamera, DefaultChromaticAberrationIsZero)
{
    const Settings s {};
    EXPECT_FLOAT_EQ(s.chromatic_aberration, 0.0F);
}

TEST(PostCamera, DefaultFilmGrainIsZero)
{
    const Settings s {};
    EXPECT_FLOAT_EQ(s.film_grain, 0.0F);
}

TEST(PostCamera, AllDefaultsZeroMeansNoOpFrame)
{
    // Arrange / Act
    const Settings s {};
    // Assert — the sum of every dial is exactly 0, i.e. the editor has to
    // opt into each effect; nothing is forced on the default render.
    EXPECT_FLOAT_EQ(s.vignette + s.chromatic_aberration + s.film_grain, 0.0F);
}

TEST(PostCamera, SettingsFieldsAreIndependentlyAssignable)
{
    // Lock that the three dials are separate members (a struct collapse
    // would let one assignment clobber another).
    Settings s {};
    s.vignette = 0.25F;
    s.chromatic_aberration = 0.5F;
    s.film_grain = 0.75F;
    EXPECT_FLOAT_EQ(s.vignette, 0.25F);
    EXPECT_FLOAT_EQ(s.chromatic_aberration, 0.5F);
    EXPECT_FLOAT_EQ(s.film_grain, 0.75F);
}

// ---- Inline GLSL helper-string contract ------------------------------------

TEST(PostCamera, InlineGlslIsNonEmpty)
{
    EXPECT_FALSE(kInlineCameraGlsl.empty());
}

TEST(PostCamera, InlineGlslDeclaresEveryHelper)
{
    const std::string_view glsl { kInlineCameraGlsl };
    EXPECT_NE(glsl.find("camera_vignette"), std::string_view::npos);
    EXPECT_NE(glsl.find("camera_chromatic"), std::string_view::npos);
    EXPECT_NE(glsl.find("camera_grain"), std::string_view::npos);
}

TEST(PostCamera, InlineGlslVignetteUsesSmoothstepMask)
{
    // The vignette helper must keep its smoothstep edge mask; a regression
    // to a hard linear ramp would change the silhouette darkening curve.
    const std::string_view glsl { kInlineCameraGlsl };
    EXPECT_NE(glsl.find("smoothstep(0.0, 1.4, radial)"), std::string_view::npos);
}

TEST(PostCamera, InlineGlslChromaticSplitsRedAndBlue)
{
    // Cheap chromatic aberration shifts R warm / B cool by ±0.08 * strength.
    const std::string_view glsl { kInlineCameraGlsl };
    EXPECT_NE(glsl.find("1.0 + strength * 0.08"), std::string_view::npos);
    EXPECT_NE(glsl.find("1.0 - strength * 0.08"), std::string_view::npos);
}

TEST(PostCamera, InlineGlslGrainHashConstantsPresent)
{
    // The hash-noise grain uses the canonical (12.9898, 78.233) dot + the
    // 43758.5453 magic constant; lock it so the grain pattern is stable.
    const std::string_view glsl { kInlineCameraGlsl };
    EXPECT_NE(glsl.find("12.9898, 78.233"), std::string_view::npos);
    EXPECT_NE(glsl.find("43758.5453"), std::string_view::npos);
}

}  // namespace
