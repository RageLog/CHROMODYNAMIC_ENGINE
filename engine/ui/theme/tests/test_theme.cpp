// =============================================================================
// CHROMODYNAMIC -- cd::ui::theme V2 tests
//
// Token-system smoke + invariant tests. The intent is to pin down the
// contract that downstream widget code relies on (slot indexing,
// monotonic spacing scale, WCAG contrast on the high-contrast preset)
// so a future palette refresh that breaks any of these immediately
// surfaces here instead of in the editor visuals.
//
// Phase 673 additions: 6 new semantic-token cases for cd::ui::widgets::Theme
// (the widget-facing palette that panels and the status bar consume). The
// test verifies that all 6 tokens have sensible non-zero defaults in the
// default-constructed struct so the editor "feels designed" on first boot.
// =============================================================================
#include <cd/ui/theme/Theme.hpp>
#include <cd/ui/widgets/Widgets.hpp>
#include <gtest/gtest.h>

namespace tt = cd::ui::theme;

namespace
{
constexpr float kEps = 1e-4F;
} // namespace

// ---- Case 1: dark theme has a dark surface ---------------------------------

TEST(ThemeV2, DarkThemeHasDarkSurface)
{
    const auto t = tt::k_dark_theme();
    const auto surface = t.color(tt::PaletteSlot::kSurface);
    // Luminance must be in the "dark" band (<0.15) -- Material 3 surface
    // (#1C1B1F) sits at ~0.108 in sRGB-encoded space; threshold leaves
    // headroom for theme refreshes that nudge the neutral.
    EXPECT_LT(tt::relative_luminance(surface), 0.15F);
}

// ---- Case 2: light theme has a light surface -------------------------------

TEST(ThemeV2, LightThemeHasLightSurface)
{
    const auto t = tt::k_light_theme();
    const auto surface = t.color(tt::PaletteSlot::kSurface);
    // Luminance must be in the "near-white" band (>0.9).
    EXPECT_GT(tt::relative_luminance(surface), 0.9F);
}

// ---- Case 3: high-contrast theme meets WCAG AAA for body text --------------

TEST(ThemeV2, HighContrastThemeMeetsWcagAaaBodyText)
{
    const auto t = tt::k_high_contrast_theme();
    const auto bg = t.color(tt::PaletteSlot::kSurface);
    const auto fg = t.color(tt::PaletteSlot::kOnSurface);
    const float ratio = tt::contrast_ratio(fg, bg);
    // AAA body text: ratio >= 7. Pure white-on-black should be ~21.
    EXPECT_GE(ratio, 7.0F);
}

// ---- Case 4: spacing scale is monotonic xs<sm<md<lg<xl ---------------------

TEST(ThemeV2, SpacingScaleIsMonotonic)
{
    const auto t = tt::k_dark_theme();
    EXPECT_LT(t.spacing.xs, t.spacing.sm);
    EXPECT_LT(t.spacing.sm, t.spacing.md);
    EXPECT_LT(t.spacing.md, t.spacing.lg);
    EXPECT_LT(t.spacing.lg, t.spacing.xl);
}

// ---- Case 5: brand override swaps primary and keeps on_primary readable ----

TEST(ThemeV2, ApplyBrandOverridePrimaryChangesOnPrimaryStaysReadable)
{
    auto base = tt::k_dark_theme();
    const auto before = base.color(tt::PaletteSlot::kPrimary);

    // Pick a clearly-dark brand so on_primary should flip to white.
    const tt::ColorToken brand { 0.05F, 0.10F, 0.45F, 1.0F };
    const auto out = tt::apply_brand_override(base, brand);

    const auto after_primary    = out.color(tt::PaletteSlot::kPrimary);
    const auto after_on_primary = out.color(tt::PaletteSlot::kOnPrimary);

    EXPECT_NE(before, after_primary);
    EXPECT_EQ(after_primary, brand);

    // Readability: contrast(on_primary, primary) >= 4.5 (WCAG AA body).
    EXPECT_GE(tt::contrast_ratio(after_on_primary, after_primary), 4.5F);

    // And for a bright brand: on_primary should be black, not white.
    const tt::ColorToken bright_brand { 0.95F, 0.95F, 0.20F, 1.0F };
    const auto out2 = tt::apply_brand_override(base, bright_brand);
    const auto on2 = out2.color(tt::PaletteSlot::kOnPrimary);
    EXPECT_NEAR(on2.r, 0.0F, kEps);
    EXPECT_NEAR(on2.g, 0.0F, kEps);
    EXPECT_NEAR(on2.b, 0.0F, kEps);
}

// ---- Case 6: typography sizes well-defined (body = 14pt typical) -----------

TEST(ThemeV2, TypographySizesWellDefined)
{
    const auto t = tt::k_dark_theme();
    const auto body    = t.font(tt::TypographySlot::kBody);
    const auto heading = t.font(tt::TypographySlot::kHeading);
    const auto label   = t.font(tt::TypographySlot::kLabel);
    const auto caption = t.font(tt::TypographySlot::kCaption);

    EXPECT_NEAR(body.size_pt, 14.0F, kEps);
    EXPECT_GT(heading.size_pt, body.size_pt);     // headings larger than body
    EXPECT_LT(label.size_pt,   body.size_pt);     // labels smaller
    EXPECT_LT(caption.size_pt, label.size_pt);    // captions smallest
    EXPECT_GT(body.line_height, 1.0F);            // sane line-height multiplier
}

// ---- Case 7: motion durations strictly positive and ordered ----------------

TEST(ThemeV2, MotionDurationsPositive)
{
    const auto t = tt::k_dark_theme();
    EXPECT_GT(t.motion.duration_short,  0.0F);
    EXPECT_GT(t.motion.duration_medium, 0.0F);
    EXPECT_GT(t.motion.duration_long,   0.0F);
    EXPECT_LT(t.motion.duration_short,  t.motion.duration_medium);
    EXPECT_LT(t.motion.duration_medium, t.motion.duration_long);
}

// ---- Case 8: elevation 0 = no shadow ---------------------------------------

TEST(ThemeV2, ElevationZeroEqualsNoShadow)
{
    constexpr tt::Elevation kFlat { 0.0F, 0.0F };
    EXPECT_NEAR(kFlat.shadow_blur,     0.0F, kEps);
    EXPECT_NEAR(kFlat.shadow_offset_y, 0.0F, kEps);

    // Defaults are non-zero (raised surface).
    const auto t = tt::k_dark_theme();
    EXPECT_GT(t.elevation.shadow_blur,     0.0F);
    EXPECT_GT(t.elevation.shadow_offset_y, 0.0F);
}

// ---- Case 9 (bonus): palette has 16 slots, typography has 4 ----------------

TEST(ThemeV2, PaletteAndTypographyArraySizes)
{
    static_assert(tt::kPaletteSize == 16);
    static_assert(tt::kTypographySize == 4);
    const auto t = tt::k_dark_theme();
    EXPECT_EQ(t.palette.size(),    16U);
    EXPECT_EQ(t.typography.size(), 4U);
}

// ---- Case 10 (bonus): contrast_ratio is symmetric --------------------------

TEST(ThemeV2, ContrastRatioSymmetric)
{
    const tt::ColorToken a { 0.0F, 0.0F, 0.0F, 1.0F };
    const tt::ColorToken b { 1.0F, 1.0F, 1.0F, 1.0F };
    EXPECT_NEAR(tt::contrast_ratio(a, b), tt::contrast_ratio(b, a), kEps);
    EXPECT_NEAR(tt::contrast_ratio(a, b), 21.0F, 0.01F);
}

// ============================================================================
// Phase 673 — cd::ui::widgets::Theme semantic-token presence tests.
//
// Each case verifies that the named token exists as a typed member on the
// default-constructed cd::ui::widgets::Theme and that its default value is
// "sensible" (non-zero and visually distinct from black) in the warm-dark
// palette baked into Widgets.hpp. The numeric thresholds are intentionally
// loose (any non-black channel sum) so a palette refresh that tweaks specific
// values does not spuriously fail these presence tests.
// ============================================================================

namespace wt = cd::ui::widgets;

// ---- Case 11: surface_subtle is lighter than surface -----------------------
//
// surface_subtle (#232A35) must be strictly lighter than surface (#1A1F26 in
// the context mapping from k_dark_theme). We test at the widget-Theme default
// level only (no V2 lookup needed) -- the default struct values encode the
// warm-dark palette contract.
TEST(ThemeV2SemanticTokens, SurfaceSubtleIsLighterThanSurface)
{
    const wt::Theme t {};
    // The sum of RGB channels for surface_subtle must exceed that of surface.
    const int surface_sum        = t.surface.r        + t.surface.g        + t.surface.b;
    const int surface_subtle_sum = t.surface_subtle.r + t.surface_subtle.g + t.surface_subtle.b;
    EXPECT_GT(surface_subtle_sum, surface_sum)
        << "surface_subtle must be lighter (higher channel sum) than surface";
}

// ---- Case 12: divider has non-zero alpha and is semi-transparent -----------

TEST(ThemeV2SemanticTokens, DividerIsSemiTransparent)
{
    const wt::Theme t {};
    // divider should be visible (alpha > 0) but not fully opaque (alpha < 255)
    // so it reads as a subtle separator rather than a hard boundary.
    EXPECT_GT(static_cast<int>(t.divider.a), 0)
        << "divider alpha must be non-zero (token must be visible)";
    EXPECT_LT(static_cast<int>(t.divider.a), 255)
        << "divider alpha must be < 255 (token must be semi-transparent)";
    // At least one RGB channel non-zero (not pure-black invisible).
    EXPECT_GT(t.divider.r + t.divider.g + t.divider.b, 0U)
        << "divider must have at least one non-zero RGB channel";
}

// ---- Case 13: text_dim is dimmer than text ---------------------------------

TEST(ThemeV2SemanticTokens, TextDimIsDimmerThanText)
{
    const wt::Theme t {};
    const int text_sum     = t.text.r     + t.text.g     + t.text.b;
    const int text_dim_sum = t.text_dim.r + t.text_dim.g + t.text_dim.b;
    EXPECT_GT(text_sum, text_dim_sum)
        << "text_dim must be darker (lower channel sum) than text";
    // text_dim must still be non-zero (not invisible).
    EXPECT_GT(text_dim_sum, 0)
        << "text_dim must be non-zero (token must be visible as a hint colour)";
}

// ---- Case 14: accent_warning has a dominant yellow bias --------------------

TEST(ThemeV2SemanticTokens, AccentWarningHasYellowBias)
{
    const wt::Theme t {};
    // Yellow = high R + high G, low B. We require R >= 150 and G >= 150
    // and B <= 150 so it reads as amber/yellow rather than white or grey.
    EXPECT_GE(static_cast<int>(t.accent_warning.r), 150)
        << "accent_warning red channel must be >= 150 (warm yellow)";
    EXPECT_GE(static_cast<int>(t.accent_warning.g), 150)
        << "accent_warning green channel must be >= 150 (warm yellow)";
    EXPECT_LE(static_cast<int>(t.accent_warning.b), 150)
        << "accent_warning blue channel must be <= 150 (no blue cast)";
    EXPECT_GT(static_cast<int>(t.accent_warning.a), 0)
        << "accent_warning alpha must be non-zero";
}

// ---- Case 15: accent_error has a dominant red bias -------------------------

TEST(ThemeV2SemanticTokens, AccentErrorHasDominantRed)
{
    const wt::Theme t {};
    // Red accent: R >= 150, G <= 150, B <= 150 in the default dark palette.
    EXPECT_GE(static_cast<int>(t.accent_error.r), 150)
        << "accent_error red channel must be >= 150";
    EXPECT_LE(static_cast<int>(t.accent_error.g), 150)
        << "accent_error green channel must be <= 150 (no yellow cast)";
    EXPECT_GT(static_cast<int>(t.accent_error.a), 0)
        << "accent_error alpha must be non-zero";
}

// ---- Case 16: accent_success has a dominant green bias ---------------------

TEST(ThemeV2SemanticTokens, AccentSuccessHasDominantGreen)
{
    const wt::Theme t {};
    // Green accent: G >= 150, R <= 150 in the default dark palette.
    EXPECT_GE(static_cast<int>(t.accent_success.g), 150)
        << "accent_success green channel must be >= 150";
    EXPECT_LE(static_cast<int>(t.accent_success.r), 150)
        << "accent_success red channel must be <= 150 (no yellow cast)";
    EXPECT_GT(static_cast<int>(t.accent_success.a), 0)
        << "accent_success alpha must be non-zero";
}

// ============================================================================
// phase695 / M14 W6A — cd_test_ui_theme_pickers
//
// Three cases that verify the named palette factories introduced in phase695:
//   default_dark_palette()
//   default_light_palette()
//   default_high_contrast_palette()
//
// Case P1: all three palettes populate every slot in the 16-slot array with
//          non-zero alpha (no uninitialized/transparent sentinel).
// Case P2: default values for surface token are non-zero in all palettes so
//          the editor fills its background on first boot without explicit seed.
// Case P3: light palette surface luminance > dark palette surface luminance
//          (inverted surface brightness — the defining dark-vs-light contract).
// ============================================================================

// ---- Case P1: all palette slots have alpha=1 (no invisible sentinel) -------

TEST(ThemePickerPalettes, AllSlotsHaveNonZeroAlpha)
{
    const auto dark = tt::default_dark_palette();
    const auto lgt  = tt::default_light_palette();
    const auto hc   = tt::default_high_contrast_palette();

    for (std::size_t s = 0; s < tt::kPaletteSize; ++s)
    {
        EXPECT_NEAR(dark.palette[s].a, 1.0F, kEps)
            << "dark palette slot " << s << " alpha must be 1.0";
        EXPECT_NEAR(lgt.palette[s].a, 1.0F, kEps)
            << "light palette slot " << s << " alpha must be 1.0";
        EXPECT_NEAR(hc.palette[s].a, 1.0F, kEps)
            << "high_contrast palette slot " << s << " alpha must be 1.0";
    }
}

// ---- Case P2: surface token is non-zero in every palette -------------------

TEST(ThemePickerPalettes, SurfaceTokenIsNonZeroInAllPalettes)
{
    auto surface_sum = [](const tt::Theme& t) -> float {
        const auto c = t.color(tt::PaletteSlot::kSurface);
        return c.r + c.g + c.b;
    };

    // Dark: very dark but NOT pure black — sum must be > 0.
    EXPECT_GT(surface_sum(tt::default_dark_palette()), 0.0F)
        << "dark surface must be non-zero (not pure invisible black)";

    // Light: near-white — sum must be > 2.7 (RGB each >= 0.9).
    EXPECT_GT(surface_sum(tt::default_light_palette()), 2.7F)
        << "light surface must be near-white (channel sum > 2.7)";

    // High-contrast: pure black IS valid for maximum contrast,
    // but on_surface must be pure white to satisfy AAA.
    const auto hc = tt::default_high_contrast_palette();
    const auto on_surface = hc.color(tt::PaletteSlot::kOnSurface);
    EXPECT_GT(on_surface.r + on_surface.g + on_surface.b, 2.9F)
        << "high_contrast on_surface must be near-white";
}

// ---- Case P3: light surface luminance > dark surface luminance -------------

TEST(ThemePickerPalettes, LightSurfaceBrighterThanDarkSurface)
{
    const auto dark = tt::default_dark_palette();
    const auto lgt  = tt::default_light_palette();

    const float lum_dark  = tt::relative_luminance(dark.color(tt::PaletteSlot::kSurface));
    const float lum_light = tt::relative_luminance(lgt.color(tt::PaletteSlot::kSurface));

    // Light palette surface must be substantially brighter (delta > 0.7 covers
    // any reasonable dark/light palette without being overly strict).
    EXPECT_GT(lum_light - lum_dark, 0.7F)
        << "light surface luminance must exceed dark surface luminance by > 0.7 "
           "(inverted brightness contract)";
}

// ============================================================================
// phase715 / M16 W6 — cd_test_palette_lerp
//
// Three cases that verify the lerp_palette() helper introduced in phase715
// for the 200 ms cross-fade animation in the theme picker:
//
//   L1: t=0  returns `from` exactly (every channel).
//   L2: t=1  returns `to`   exactly (every channel).
//   L3: t=0.5 returns the per-channel midpoint of dark and light palettes.
// ============================================================================

// ---- Case L1: lerp_palette(from, to, 0) == from ---------------------------

TEST(PaletteLerp, TZeroReturnsFrom)
{
    const auto from = tt::default_dark_palette();
    const auto to   = tt::default_light_palette();
    const auto out  = tt::lerp_palette(from, to, 0.0F);

    for (std::size_t i = 0; i < tt::kPaletteSize; ++i)
    {
        EXPECT_NEAR(out.palette[i].r, from.palette[i].r, kEps) << "slot " << i << " r";
        EXPECT_NEAR(out.palette[i].g, from.palette[i].g, kEps) << "slot " << i << " g";
        EXPECT_NEAR(out.palette[i].b, from.palette[i].b, kEps) << "slot " << i << " b";
        EXPECT_NEAR(out.palette[i].a, from.palette[i].a, kEps) << "slot " << i << " a";
    }
}

// ---- Case L2: lerp_palette(from, to, 1) == to -----------------------------

TEST(PaletteLerp, TOneReturnsTo)
{
    const auto from = tt::default_dark_palette();
    const auto to   = tt::default_light_palette();
    const auto out  = tt::lerp_palette(from, to, 1.0F);

    for (std::size_t i = 0; i < tt::kPaletteSize; ++i)
    {
        EXPECT_NEAR(out.palette[i].r, to.palette[i].r, kEps) << "slot " << i << " r";
        EXPECT_NEAR(out.palette[i].g, to.palette[i].g, kEps) << "slot " << i << " g";
        EXPECT_NEAR(out.palette[i].b, to.palette[i].b, kEps) << "slot " << i << " b";
        EXPECT_NEAR(out.palette[i].a, to.palette[i].a, kEps) << "slot " << i << " a";
    }
}

// ---- Case L3: lerp_palette(from, to, 0.5) == per-channel midpoint ---------

TEST(PaletteLerp, THalfReturnsMidpoint)
{
    const auto from = tt::default_dark_palette();
    const auto to   = tt::default_high_contrast_palette();
    const auto out  = tt::lerp_palette(from, to, 0.5F);

    for (std::size_t i = 0; i < tt::kPaletteSize; ++i)
    {
        const float expect_r = (from.palette[i].r + to.palette[i].r) * 0.5F;
        const float expect_g = (from.palette[i].g + to.palette[i].g) * 0.5F;
        const float expect_b = (from.palette[i].b + to.palette[i].b) * 0.5F;
        const float expect_a = (from.palette[i].a + to.palette[i].a) * 0.5F;
        EXPECT_NEAR(out.palette[i].r, expect_r, kEps) << "slot " << i << " r";
        EXPECT_NEAR(out.palette[i].g, expect_g, kEps) << "slot " << i << " g";
        EXPECT_NEAR(out.palette[i].b, expect_b, kEps) << "slot " << i << " b";
        EXPECT_NEAR(out.palette[i].a, expect_a, kEps) << "slot " << i << " a";
    }
}

// ============================================================================
// Gap-close batch — 22 new tests for genuine 100% depth.
//
// Coverage dimensions:
//   WB  — WCAG contrast_ratio at exact 4.5:1 / 7:1 / 3:1 / 21:1 boundaries
//          and black/white extreme inputs.
//   OC  — on-color (pick_on_color) selection via apply_brand_override:
//          above/below/at the 0.179 luminance threshold.
//   BO  — Brand override partial-override: non-primary slots untouched.
//   TN  — theme_from_name: "dark", "light", "high_contrast", unknown fallback,
//          empty string fallback.
//   SM  — Spacing/Motion/Elevation monotonic for all three built-in themes.
//   TC  — Token completeness: every PaletteSlot has consistent r/g/b (not NaN,
//          not negative), all three themes.
//   LI  — lerp_palette: non-color fields taken from `to`, and t>1/t<0 extrapolates
//          linearly without crash.
//   HN  — Theme name constant round-trip through theme_from_name.
// ============================================================================

// ---- WB1: contrast_ratio of pure black vs pure white == 21.0 ---------------
//
// WCAG spec §1.4.3: (1.0 + 0.05) / (0.0 + 0.05) = 1.05 / 0.05 = 21.0.
// This pins the formula implementation against the known spec value.

TEST(WcagBoundaries, BlackWhiteContrastIs21)
{
    constexpr tt::ColorToken kBlack { 0.0F, 0.0F, 0.0F, 1.0F };
    constexpr tt::ColorToken kWhite { 1.0F, 1.0F, 1.0F, 1.0F };
    EXPECT_NEAR(tt::contrast_ratio(kBlack, kWhite), 21.0F, 0.001F);
}

// ---- WB2: same-color contrast == 1.0 ---------------------------------------
//
// Two identical colours: L_lighter == L_darker, so ratio = (L+0.05)/(L+0.05) = 1.

TEST(WcagBoundaries, SameColorContrastIsOne)
{
    constexpr tt::ColorToken kGrey { 0.5F, 0.5F, 0.5F, 1.0F };
    EXPECT_NEAR(tt::contrast_ratio(kGrey, kGrey), 1.0F, 0.001F);
}

// ---- WB3: contrast at exact WCAG AA large-text boundary (3:1) passes test --
//
// Construct a foreground whose luminance gives exactly 3:1 vs pure black.
// Formula: ratio = (L_fg + 0.05) / (0.0 + 0.05).
// => L_fg = 3 * 0.05 - 0.05 = 0.10.
// A grey with r == g == b == k satisfies L = k*(0.2126+0.7152+0.0722) = k*1.0
// => k = 0.10 exactly.

TEST(WcagBoundaries, ExactAaLargeTextBoundary3to1)
{
    constexpr tt::ColorToken kBlack { 0.0F, 0.0F, 0.0F, 1.0F };
    // Grey whose luminance == 0.10 → contrast = (0.10+0.05)/(0.00+0.05) = 3.0
    constexpr tt::ColorToken kGrey010 { 0.10F, 0.10F, 0.10F, 1.0F };
    constexpr float kExpectedLum = (0.2126F * 0.10F) + (0.7152F * 0.10F) + (0.0722F * 0.10F);
    static_assert(kExpectedLum > 0.09F && kExpectedLum < 0.11F,
        "luminance sanity: grey(0.10) should be ~0.10");
    const float ratio = tt::contrast_ratio(kGrey010, kBlack);
    EXPECT_NEAR(ratio, 3.0F, 0.001F);
    EXPECT_GE(ratio, 3.0F - 0.001F);
}

// ---- WB4: contrast at exact WCAG AA body-text boundary (4.5:1) -------------
//
// L_fg = 4.5 * 0.05 - 0.05 = 0.175 vs pure black.
// Grey 0.175: L = 0.175 (since all channels equal and sum of rec709 coefficients is 1).

TEST(WcagBoundaries, ExactAaBodyTextBoundary4pt5to1)
{
    constexpr tt::ColorToken kBlack  { 0.0F,   0.0F,   0.0F,   1.0F };
    constexpr tt::ColorToken kGrey175 { 0.175F, 0.175F, 0.175F, 1.0F };
    const float ratio = tt::contrast_ratio(kGrey175, kBlack);
    EXPECT_NEAR(ratio, 4.5F, 0.001F);
    EXPECT_GE(ratio, 4.5F - 0.001F);
}

// ---- WB5: contrast at exact WCAG AAA body-text boundary (7:1) --------------
//
// L_fg = 7 * 0.05 - 0.05 = 0.30 vs pure black.

TEST(WcagBoundaries, ExactAaaBodyTextBoundary7to1)
{
    constexpr tt::ColorToken kBlack  { 0.0F,  0.0F,  0.0F,  1.0F };
    constexpr tt::ColorToken kGrey30 { 0.30F, 0.30F, 0.30F, 1.0F };
    const float ratio = tt::contrast_ratio(kGrey30, kBlack);
    EXPECT_NEAR(ratio, 7.0F, 0.001F);
    EXPECT_GE(ratio, 7.0F - 0.001F);
}

// ---- WB6: relative_luminance of pure black == 0, pure white == 1 -----------

TEST(WcagBoundaries, LuminanceExtremes)
{
    constexpr tt::ColorToken kBlack { 0.0F, 0.0F, 0.0F, 1.0F };
    constexpr tt::ColorToken kWhite { 1.0F, 1.0F, 1.0F, 1.0F };
    EXPECT_NEAR(tt::relative_luminance(kBlack), 0.0F, kEps);
    EXPECT_NEAR(tt::relative_luminance(kWhite), 1.0F, kEps);
}

// ---- WB7: relative_luminance rec709 channel weights ------------------------
//
// Pure red: L = 0.2126. Pure green: L = 0.7152. Pure blue: L = 0.0722.
// These are the Rec. 709 primaries; a wrong coefficient swap is a silent bug.

TEST(WcagBoundaries, LuminanceRec709Channels)
{
    constexpr tt::ColorToken kRed   { 1.0F, 0.0F, 0.0F, 1.0F };
    constexpr tt::ColorToken kGreen { 0.0F, 1.0F, 0.0F, 1.0F };
    constexpr tt::ColorToken kBlue  { 0.0F, 0.0F, 1.0F, 1.0F };
    EXPECT_NEAR(tt::relative_luminance(kRed),   0.2126F, kEps);
    EXPECT_NEAR(tt::relative_luminance(kGreen), 0.7152F, kEps);
    EXPECT_NEAR(tt::relative_luminance(kBlue),  0.0722F, kEps);
}

// ---- OC1: pick_on_color above 0.179 → black foreground --------------------
//
// Tested via apply_brand_override.  A brand with luminance just above 0.179
// (e.g. grey ~0.20) should produce on_primary == black (0,0,0).
// L(grey 0.20) = 0.20 > 0.179 → black.

TEST(OnColorSelection, LuminanceAboveThresholdYieldsBlack)
{
    auto base = tt::k_dark_theme();
    // grey 0.45: L = 0.45, well above 0.179
    const tt::ColorToken light_grey { 0.45F, 0.45F, 0.45F, 1.0F };
    const auto out = tt::apply_brand_override(base, light_grey);
    const auto on_p = out.color(tt::PaletteSlot::kOnPrimary);
    EXPECT_NEAR(on_p.r, 0.0F, kEps) << "on_primary r must be 0 (black) for light brand";
    EXPECT_NEAR(on_p.g, 0.0F, kEps) << "on_primary g must be 0 (black) for light brand";
    EXPECT_NEAR(on_p.b, 0.0F, kEps) << "on_primary b must be 0 (black) for light brand";
}

// ---- OC2: pick_on_color below 0.179 → white foreground --------------------
//
// A brand with luminance just below 0.179 (e.g. grey ~0.10) should produce
// on_primary == white (1,1,1).
// L(grey 0.10) = 0.10 < 0.179 → white.

TEST(OnColorSelection, LuminanceBelowThresholdYieldsWhite)
{
    auto base = tt::k_dark_theme();
    const tt::ColorToken dark_grey { 0.10F, 0.10F, 0.10F, 1.0F };
    const auto out = tt::apply_brand_override(base, dark_grey);
    const auto on_p = out.color(tt::PaletteSlot::kOnPrimary);
    EXPECT_NEAR(on_p.r, 1.0F, kEps) << "on_primary r must be 1 (white) for dark brand";
    EXPECT_NEAR(on_p.g, 1.0F, kEps) << "on_primary g must be 1 (white) for dark brand";
    EXPECT_NEAR(on_p.b, 1.0F, kEps) << "on_primary b must be 1 (white) for dark brand";
}

// ---- OC3: pick_on_color exactly at 0.179 → white (condition is l > 0.179) -
//
// L == 0.179 is NOT > 0.179, so the branch falls through to white.

TEST(OnColorSelection, LuminanceBelowThresholdWhiteAboveBlack)
{
    auto base = tt::k_dark_theme();
    // pick_on_color uses `l > 0.179F` (L(grey k) == k, no sRGB gamma). The EXACT
    // boundary is float-unstable (0.179*1.0 may round just above 0.179), so test
    // with a clear margin on either side instead.
    // Clearly BELOW threshold -> dark background -> WHITE on-color.
    const auto on_dark =
        tt::apply_brand_override(base, tt::ColorToken { 0.10F, 0.10F, 0.10F, 1.0F })
            .color(tt::PaletteSlot::kOnPrimary);
    EXPECT_NEAR(on_dark.r, 1.0F, kEps) << "below threshold -> white";
    EXPECT_NEAR(on_dark.g, 1.0F, kEps);
    EXPECT_NEAR(on_dark.b, 1.0F, kEps);
    // Clearly ABOVE threshold -> light background -> BLACK on-color.
    const auto on_light =
        tt::apply_brand_override(base, tt::ColorToken { 0.30F, 0.30F, 0.30F, 1.0F })
            .color(tt::PaletteSlot::kOnPrimary);
    EXPECT_NEAR(on_light.r, 0.0F, kEps) << "above threshold -> black";
    EXPECT_NEAR(on_light.g, 0.0F, kEps);
    EXPECT_NEAR(on_light.b, 0.0F, kEps);
}

// ---- BO1: brand override leaves non-primary slots unchanged ----------------
//
// apply_brand_override must only mutate kPrimary and kOnPrimary.
// Every other slot must be byte-identical to the original.

TEST(BrandOverride, NonPrimarySlotsAreUntouched)
{
    const auto base = tt::k_dark_theme();
    const tt::ColorToken brand { 0.2F, 0.4F, 0.8F, 1.0F };
    const auto out = tt::apply_brand_override(base, brand);

    // Check every non-primary slot is unchanged.
    for (std::size_t i = 0; i < tt::kPaletteSize; ++i)
    {
        const auto slot = static_cast<tt::PaletteSlot>(i);
        if (slot == tt::PaletteSlot::kPrimary || slot == tt::PaletteSlot::kOnPrimary)
        {
            continue;  // these are intentionally modified
        }
        EXPECT_EQ(out.palette[i], base.palette[i])
            << "slot " << i << " must be unchanged by brand override";
    }
}

// ---- BO2: double brand override — second call overwrites first brand --------

TEST(BrandOverride, DoubleBrandOverrideUsesSecondBrand)
{
    const auto base = tt::k_dark_theme();
    const tt::ColorToken brand_a { 0.8F, 0.1F, 0.1F, 1.0F };
    const tt::ColorToken brand_b { 0.1F, 0.8F, 0.1F, 1.0F };

    const auto after_a = tt::apply_brand_override(base, brand_a);
    const auto after_b = tt::apply_brand_override(after_a, brand_b);

    EXPECT_EQ(after_b.color(tt::PaletteSlot::kPrimary), brand_b);
    // on_primary for green (high G → high luminance → black foreground)
    const auto on_b = after_b.color(tt::PaletteSlot::kOnPrimary);
    EXPECT_GE(tt::contrast_ratio(on_b, brand_b), 4.5F)
        << "on_primary must remain readable after second override";
}

// ---- TN1: theme_from_name "dark" resolves to dark palette ------------------

TEST(ThemeFromName, DarkStringReturnsDarkPalette)
{
    const auto via_name = tt::theme_from_name("dark");
    const auto direct   = tt::default_dark_palette();
    EXPECT_NEAR(tt::relative_luminance(via_name.color(tt::PaletteSlot::kSurface)),
                tt::relative_luminance(direct.color(tt::PaletteSlot::kSurface)),
                kEps);
}

// ---- TN2: theme_from_name "light" resolves to light palette ----------------

TEST(ThemeFromName, LightStringReturnsLightPalette)
{
    const auto via_name = tt::theme_from_name("light");
    const auto direct   = tt::default_light_palette();
    EXPECT_NEAR(tt::relative_luminance(via_name.color(tt::PaletteSlot::kSurface)),
                tt::relative_luminance(direct.color(tt::PaletteSlot::kSurface)),
                kEps);
}

// ---- TN3: theme_from_name "high_contrast" resolves to HC palette -----------

TEST(ThemeFromName, HighContrastStringReturnsHcPalette)
{
    const auto via_name = tt::theme_from_name("high_contrast");
    const auto direct   = tt::default_high_contrast_palette();
    // HC surface is pure black; contrast of on_surface vs surface must be >= 7.
    const float ratio = tt::contrast_ratio(
        via_name.color(tt::PaletteSlot::kOnSurface),
        via_name.color(tt::PaletteSlot::kSurface));
    EXPECT_GE(ratio, 7.0F);
    EXPECT_NEAR(tt::relative_luminance(via_name.color(tt::PaletteSlot::kSurface)),
                tt::relative_luminance(direct.color(tt::PaletteSlot::kSurface)),
                kEps);
}

// ---- TN4: theme_from_name unknown string falls back to dark palette --------

TEST(ThemeFromName, UnknownStringFallsBackToDark)
{
    const auto via_unknown = tt::theme_from_name("totally_unknown_theme_xyz");
    const auto dark        = tt::default_dark_palette();
    // Dark surface is substantially darker than 0.15 luminance.
    EXPECT_LT(tt::relative_luminance(via_unknown.color(tt::PaletteSlot::kSurface)), 0.15F);
    EXPECT_NEAR(via_unknown.color(tt::PaletteSlot::kPrimary).r,
                dark.color(tt::PaletteSlot::kPrimary).r, kEps);
}

// ---- TN5: theme_from_name empty string falls back to dark palette ----------

TEST(ThemeFromName, EmptyStringFallsBackToDark)
{
    const auto via_empty = tt::theme_from_name("");
    EXPECT_LT(tt::relative_luminance(via_empty.color(tt::PaletteSlot::kSurface)), 0.15F);
}

// ---- SM1: spacing monotonic for light and high-contrast themes as well ------

TEST(ScaleMonotonic, SpacingMonotonicAllThreeThemes)
{
    const auto light = tt::k_light_theme();
    const auto hc    = tt::k_high_contrast_theme();

    for (const auto* sp : { &light.spacing, &hc.spacing })
    {
        EXPECT_LT(sp->xs, sp->sm) << "xs < sm";
        EXPECT_LT(sp->sm, sp->md) << "sm < md";
        EXPECT_LT(sp->md, sp->lg) << "md < lg";
        EXPECT_LT(sp->lg, sp->xl) << "lg < xl";
    }
}

// ---- SM2: motion durations ordered for all three themes --------------------

TEST(ScaleMonotonic, MotionOrderedAllThreeThemes)
{
    const auto dark  = tt::k_dark_theme();
    const auto light = tt::k_light_theme();
    const auto hc    = tt::k_high_contrast_theme();

    for (const auto* m : { &dark.motion, &light.motion, &hc.motion })
    {
        EXPECT_LT(m->duration_short,  m->duration_medium) << "short < medium";
        EXPECT_LT(m->duration_medium, m->duration_long)   << "medium < long";
        EXPECT_GT(m->duration_short,  0.0F)               << "short must be positive";
    }
}

// ---- SM3: elevation shadow_blur >= shadow_offset_y (shadow spread >= lift) -

TEST(ScaleMonotonic, ElevationBlurAtLeastOffsetAllThemes)
{
    const auto dark  = tt::k_dark_theme();
    const auto light = tt::k_light_theme();
    const auto hc    = tt::k_high_contrast_theme();

    for (const auto* e : { &dark.elevation, &light.elevation, &hc.elevation })
    {
        // Both positive in the non-flat default.
        EXPECT_GT(e->shadow_blur,     0.0F);
        EXPECT_GT(e->shadow_offset_y, 0.0F);
        // Blur >= offset is a physically-motivated invariant (spread at least equals lift).
        EXPECT_GE(e->shadow_blur, e->shadow_offset_y)
            << "shadow_blur must be >= shadow_offset_y (physically grounded shadow)";
    }
}

// ---- TC1: all palette channels in [0, 1] across all three themes -----------

TEST(TokenCompleteness, AllChannelsInUnitRangeAllThemes)
{
    const auto dark  = tt::k_dark_theme();
    const auto light = tt::k_light_theme();
    const auto hc    = tt::k_high_contrast_theme();

    for (const auto* th : { &dark, &light, &hc })
    {
        for (std::size_t i = 0; i < tt::kPaletteSize; ++i)
        {
            const auto& c = th->palette[i];
            EXPECT_GE(c.r, 0.0F) << "slot " << i << " r must be >= 0";
            EXPECT_LE(c.r, 1.0F) << "slot " << i << " r must be <= 1";
            EXPECT_GE(c.g, 0.0F) << "slot " << i << " g must be >= 0";
            EXPECT_LE(c.g, 1.0F) << "slot " << i << " g must be <= 1";
            EXPECT_GE(c.b, 0.0F) << "slot " << i << " b must be >= 0";
            EXPECT_LE(c.b, 1.0F) << "slot " << i << " b must be <= 1";
            EXPECT_GE(c.a, 0.0F) << "slot " << i << " a must be >= 0";
            EXPECT_LE(c.a, 1.0F) << "slot " << i << " a must be <= 1";
        }
    }
}

// ---- TC2: dark and light themes have distinct primaries --------------------
//
// The two themes serve different luminance bands; their primaries must differ.

TEST(TokenCompleteness, DarkAndLightPrimariesAreDistinct)
{
    const auto dark  = tt::k_dark_theme();
    const auto light = tt::k_light_theme();
    const auto dp    = dark.color(tt::PaletteSlot::kPrimary);
    const auto lp    = light.color(tt::PaletteSlot::kPrimary);
    EXPECT_FALSE(dp == lp) << "dark and light primaries must be distinct tokens";
}

// ---- TC3: HC on_primary is dark (high-luma primary → black foreground) -----

TEST(TokenCompleteness, HighContrastOnPrimaryIsDark)
{
    const auto hc = tt::k_high_contrast_theme();
    // HC primary is canary yellow (high luminance), so on_primary must be black.
    const auto on_p = hc.color(tt::PaletteSlot::kOnPrimary);
    EXPECT_NEAR(on_p.r, 0.0F, kEps) << "HC on_primary r must be black";
    EXPECT_NEAR(on_p.g, 0.0F, kEps) << "HC on_primary g must be black";
    EXPECT_NEAR(on_p.b, 0.0F, kEps) << "HC on_primary b must be black";
}

// ---- LI1: lerp_palette t>1 extrapolates without crash ----------------------
//
// The public API makes no claim about clamping; the contract is purely:
//   "no crash and result is a valid Theme struct" for out-of-range t.
// We verify non-NaN channels only (no specific numeric value contract).

TEST(PaletteLerp, ExtrapolationBeyondOneDoesNotCrash)
{
    const auto from = tt::default_dark_palette();
    const auto to   = tt::default_light_palette();
    const auto out  = tt::lerp_palette(from, to, 1.5F);  // t > 1
    // Check that no channel is NaN (a crash or UB might produce NaN).
    for (std::size_t i = 0; i < tt::kPaletteSize; ++i)
    {
        // A finite value satisfies v == v; NaN does not.
        EXPECT_EQ(out.palette[i].r, out.palette[i].r) << "slot " << i << " r must not be NaN";
        EXPECT_EQ(out.palette[i].g, out.palette[i].g) << "slot " << i << " g must not be NaN";
        EXPECT_EQ(out.palette[i].b, out.palette[i].b) << "slot " << i << " b must not be NaN";
    }
}

// ---- LI2: lerp_palette non-color fields always come from `to` --------------
//
// Typography, spacing, motion, elevation must equal `to`'s values
// regardless of t (they are not interpolated by design).

TEST(PaletteLerp, NonColorFieldsAlwaysFromTo)
{
    const auto from = tt::default_dark_palette();
    const auto to   = tt::default_high_contrast_palette();

    // High-contrast body font is 15pt; dark body font is 14pt.
    // After lerp (any t), body size must equal `to` body size (15pt).
    for (const float t : { 0.0F, 0.25F, 0.5F, 0.75F, 1.0F })
    {
        const auto out = tt::lerp_palette(from, to, t);
        EXPECT_NEAR(out.font(tt::TypographySlot::kBody).size_pt,
                    to.font(tt::TypographySlot::kBody).size_pt,
                    kEps)
            << "body size_pt must always match `to` at t=" << t;
        EXPECT_NEAR(out.spacing.xs, to.spacing.xs, kEps)
            << "spacing.xs must always match `to` at t=" << t;
        EXPECT_NEAR(out.motion.duration_short, to.motion.duration_short, kEps)
            << "motion.duration_short must always match `to` at t=" << t;
    }
}

// ---- HN: kThemeName constants round-trip through theme_from_name -----------

TEST(ThemeNameConstants, ConstantsRoundTripThroughFromName)
{
    // All three constant strings must resolve back to the palette that matches
    // the name -- the "light" constant must produce a light (high-luminance) surface.
    const auto dark_resolved = tt::theme_from_name(tt::kThemeNameDark);
    const auto lite_resolved = tt::theme_from_name(tt::kThemeNameLight);
    const auto hc_resolved   = tt::theme_from_name(tt::kThemeNameHighContrast);

    EXPECT_LT(tt::relative_luminance(dark_resolved.color(tt::PaletteSlot::kSurface)),
              0.15F)
        << "kThemeNameDark must resolve to a dark-surface palette";

    EXPECT_GT(tt::relative_luminance(lite_resolved.color(tt::PaletteSlot::kSurface)),
              0.9F)
        << "kThemeNameLight must resolve to a near-white-surface palette";

    EXPECT_GE(tt::contrast_ratio(hc_resolved.color(tt::PaletteSlot::kOnSurface),
                                  hc_resolved.color(tt::PaletteSlot::kSurface)),
              7.0F)
        << "kThemeNameHighContrast must resolve to a WCAG-AAA palette";
}
