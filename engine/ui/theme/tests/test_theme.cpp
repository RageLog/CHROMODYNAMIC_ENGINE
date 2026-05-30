// =============================================================================
// CHROMODYNAMIC -- cd::ui::theme V2 tests
//
// Token-system smoke + invariant tests. The intent is to pin down the
// contract that downstream widget code relies on (slot indexing,
// monotonic spacing scale, WCAG contrast on the high-contrast preset)
// so a future palette refresh that breaks any of these immediately
// surfaces here instead of in the editor visuals.
// =============================================================================
#include <cd/ui/theme/Theme.hpp>
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
