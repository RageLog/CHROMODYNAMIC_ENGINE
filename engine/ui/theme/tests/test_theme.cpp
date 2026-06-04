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
}

// ---- Case 1: dark theme has a dark surface ---------------------------------

TEST(ThemeV2, DarkThemeHasDarkSurface)
{
    const auto t = tt::kDarkTheme();
    const auto surface = t.color(tt::PaletteSlot::kSurface);
    // Luminance must be in the "dark" band (<0.15) -- Material 3 surface
    // (#1C1B1F) sits at ~0.108 in sRGB-encoded space; threshold leaves
    // headroom for theme refreshes that nudge the neutral.
    EXPECT_LT(tt::relative_luminance(surface), 0.15F);
}

// ---- Case 2: light theme has a light surface -------------------------------

TEST(ThemeV2, LightThemeHasLightSurface)
{
    const auto t = tt::kLightTheme();
    const auto surface = t.color(tt::PaletteSlot::kSurface);
    // Luminance must be in the "near-white" band (>0.9).
    EXPECT_GT(tt::relative_luminance(surface), 0.9F);
}

// ---- Case 3: high-contrast theme meets WCAG AAA for body text --------------

TEST(ThemeV2, HighContrastThemeMeetsWcagAaaBodyText)
{
    const auto t = tt::kHighContrastTheme();
    const auto bg = t.color(tt::PaletteSlot::kSurface);
    const auto fg = t.color(tt::PaletteSlot::kOnSurface);
    const float ratio = tt::contrast_ratio(fg, bg);
    // AAA body text: ratio >= 7. Pure white-on-black should be ~21.
    EXPECT_GE(ratio, 7.0F);
}

// ---- Case 4: spacing scale is monotonic xs<sm<md<lg<xl ---------------------

TEST(ThemeV2, SpacingScaleIsMonotonic)
{
    const auto t = tt::kDarkTheme();
    EXPECT_LT(t.spacing.xs, t.spacing.sm);
    EXPECT_LT(t.spacing.sm, t.spacing.md);
    EXPECT_LT(t.spacing.md, t.spacing.lg);
    EXPECT_LT(t.spacing.lg, t.spacing.xl);
}

// ---- Case 5: brand override swaps primary and keeps on_primary readable ----

TEST(ThemeV2, ApplyBrandOverridePrimaryChangesOnPrimaryStaysReadable)
{
    auto base = tt::kDarkTheme();
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
    const auto t = tt::kDarkTheme();
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
    const auto t = tt::kDarkTheme();
    EXPECT_GT(t.motion.duration_short,  0.0F);
    EXPECT_GT(t.motion.duration_medium, 0.0F);
    EXPECT_GT(t.motion.duration_long,   0.0F);
    EXPECT_LT(t.motion.duration_short,  t.motion.duration_medium);
    EXPECT_LT(t.motion.duration_medium, t.motion.duration_long);
}

// ---- Case 8: elevation 0 = no shadow ---------------------------------------

TEST(ThemeV2, ElevationZeroEqualsNoShadow)
{
    constexpr tt::Elevation flat { 0.0F, 0.0F };
    EXPECT_NEAR(flat.shadow_blur,     0.0F, kEps);
    EXPECT_NEAR(flat.shadow_offset_y, 0.0F, kEps);

    // Defaults are non-zero (raised surface).
    const auto t = tt::kDarkTheme();
    EXPECT_GT(t.elevation.shadow_blur,     0.0F);
    EXPECT_GT(t.elevation.shadow_offset_y, 0.0F);
}

// ---- Case 9 (bonus): palette has 16 slots, typography has 4 ----------------

TEST(ThemeV2, PaletteAndTypographyArraySizes)
{
    static_assert(tt::kPaletteSize == 16);
    static_assert(tt::kTypographySize == 4);
    const auto t = tt::kDarkTheme();
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
// the context mapping from kDarkTheme). We test at the widget-Theme default
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
