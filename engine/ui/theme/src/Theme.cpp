// =============================================================================
// CHROMODYNAMIC -- cd/ui/theme/Theme.cpp
//
// Phase 474 -- built-in palettes (dark / light / high-contrast) plus the
// brand-override helper. Palette values follow the Material 3 reference
// scheme (https://m3.material.io/styles/color/system) so callers porting
// from Figma / Material kits map slot-for-slot. Float values quoted in
// linear 0..1 space (not sRGB) -- callers wanting sRGB hex equivalents
// can read the comments next to each token.
// =============================================================================
#include <cd/ui/theme/Theme.hpp>

#include <array>

namespace cd::ui::theme
{

namespace
{

// Convenience: build a ColorToken from an 8-bit sRGB triple. We DO NOT
// linearise (i.e. apply the sRGB EOTF) here -- the tokens are stored as
// the same numeric values the design system documents so a designer
// comparing the Figma artboard to the engine output sees the same
// hex. The renderer can choose to treat these as sRGB at upload time.
constexpr ColorToken rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) noexcept
{
    return ColorToken {
        static_cast<float>(r) / 255.0F,
        static_cast<float>(g) / 255.0F,
        static_cast<float>(b) / 255.0F,
        static_cast<float>(a) / 255.0F,
    };
}

constexpr Spacing kDefaultSpacing { 2.0F, 4.0F, 8.0F, 16.0F, 32.0F };
constexpr Motion  kDefaultMotion  { 100.0F, 250.0F, 500.0F };
constexpr Elevation kDefaultElevation { 4.0F, 2.0F };

// Pick on_primary so the foreground reads cleanly over the chosen
// primary. Threshold 0.179 is the WCAG cut-off used by Material 3 for
// switching foregrounds between black and white.
constexpr ColorToken pick_on_color(ColorToken bg) noexcept
{
    const float l = relative_luminance(bg);
    if (l > 0.179F)
    {
        return ColorToken { 0.0F, 0.0F, 0.0F, 1.0F };
    }
    return ColorToken { 1.0F, 1.0F, 1.0F, 1.0F };
}

}  // namespace

// ---- Dark theme ------------------------------------------------------------
//
// Material 3 reference "dark" palette. Surface is a near-black neutral
// (#1C1B1F), primary is a muted lavender (#D0BCFF) so it pops against
// the dark surface while staying inside the brand chroma envelope.
Theme kDarkTheme() noexcept
{
    Theme t;
    t.palette[static_cast<std::size_t>(PaletteSlot::kPrimary)]            = rgb(0xD0, 0xBC, 0xFF);  // primary
    t.palette[static_cast<std::size_t>(PaletteSlot::kOnPrimary)]          = rgb(0x37, 0x1E, 0x73);  // on primary
    t.palette[static_cast<std::size_t>(PaletteSlot::kPrimaryContainer)]   = rgb(0x4F, 0x37, 0x8B);  // primary container
    t.palette[static_cast<std::size_t>(PaletteSlot::kOnPrimaryContainer)] = rgb(0xEA, 0xDD, 0xFF);  // on primary container
    t.palette[static_cast<std::size_t>(PaletteSlot::kSecondary)]          = rgb(0xCC, 0xC2, 0xDC);  // secondary
    t.palette[static_cast<std::size_t>(PaletteSlot::kOnSecondary)]        = rgb(0x33, 0x2D, 0x41);  // on secondary
    t.palette[static_cast<std::size_t>(PaletteSlot::kSurface)]            = rgb(0x1C, 0x1B, 0x1F);  // surface
    t.palette[static_cast<std::size_t>(PaletteSlot::kOnSurface)]          = rgb(0xE6, 0xE1, 0xE5);  // on surface
    t.palette[static_cast<std::size_t>(PaletteSlot::kSurfaceVariant)]     = rgb(0x49, 0x45, 0x4F);  // surface variant
    t.palette[static_cast<std::size_t>(PaletteSlot::kOnSurfaceVariant)]   = rgb(0xCA, 0xC4, 0xD0);  // on surface variant
    t.palette[static_cast<std::size_t>(PaletteSlot::kBackground)]         = rgb(0x1C, 0x1B, 0x1F);  // background
    t.palette[static_cast<std::size_t>(PaletteSlot::kOnBackground)]       = rgb(0xE6, 0xE1, 0xE5);  // on background
    t.palette[static_cast<std::size_t>(PaletteSlot::kError)]              = rgb(0xF2, 0xB8, 0xB5);  // error
    t.palette[static_cast<std::size_t>(PaletteSlot::kOnError)]            = rgb(0x60, 0x14, 0x10);  // on error
    t.palette[static_cast<std::size_t>(PaletteSlot::kOutline)]            = rgb(0x93, 0x8F, 0x99);  // outline
    t.palette[static_cast<std::size_t>(PaletteSlot::kInverseSurface)]     = rgb(0xE6, 0xE1, 0xE5);  // inverse surface (= on-surface for dark)

    t.typography[static_cast<std::size_t>(TypographySlot::kBody)]    = Typography { "default", 14.0F, 1.4F,  0.0F   };
    t.typography[static_cast<std::size_t>(TypographySlot::kHeading)] = Typography { "default", 22.0F, 1.25F, -0.01F };
    t.typography[static_cast<std::size_t>(TypographySlot::kLabel)]   = Typography { "default", 12.0F, 1.3F,  0.02F  };
    t.typography[static_cast<std::size_t>(TypographySlot::kCaption)] = Typography { "default", 11.0F, 1.3F,  0.03F  };

    t.spacing   = kDefaultSpacing;
    t.motion    = kDefaultMotion;
    t.elevation = kDefaultElevation;
    return t;
}

// ---- Light theme -----------------------------------------------------------
//
// Material 3 reference "light" palette. Surface is near-white
// (#FFFBFE), primary is a stronger purple (#6750A4) so it stays
// readable against the bright surface.
Theme kLightTheme() noexcept
{
    Theme t;
    t.palette[static_cast<std::size_t>(PaletteSlot::kPrimary)]            = rgb(0x67, 0x50, 0xA4);  // primary
    t.palette[static_cast<std::size_t>(PaletteSlot::kOnPrimary)]          = rgb(0xFF, 0xFF, 0xFF);  // on primary
    t.palette[static_cast<std::size_t>(PaletteSlot::kPrimaryContainer)]   = rgb(0xEA, 0xDD, 0xFF);  // primary container
    t.palette[static_cast<std::size_t>(PaletteSlot::kOnPrimaryContainer)] = rgb(0x21, 0x00, 0x5D);  // on primary container
    t.palette[static_cast<std::size_t>(PaletteSlot::kSecondary)]          = rgb(0x62, 0x5B, 0x71);  // secondary
    t.palette[static_cast<std::size_t>(PaletteSlot::kOnSecondary)]        = rgb(0xFF, 0xFF, 0xFF);  // on secondary
    t.palette[static_cast<std::size_t>(PaletteSlot::kSurface)]            = rgb(0xFF, 0xFB, 0xFE);  // surface
    t.palette[static_cast<std::size_t>(PaletteSlot::kOnSurface)]          = rgb(0x1C, 0x1B, 0x1F);  // on surface
    t.palette[static_cast<std::size_t>(PaletteSlot::kSurfaceVariant)]     = rgb(0xE7, 0xE0, 0xEC);  // surface variant
    t.palette[static_cast<std::size_t>(PaletteSlot::kOnSurfaceVariant)]   = rgb(0x49, 0x45, 0x4F);  // on surface variant
    t.palette[static_cast<std::size_t>(PaletteSlot::kBackground)]         = rgb(0xFF, 0xFB, 0xFE);  // background
    t.palette[static_cast<std::size_t>(PaletteSlot::kOnBackground)]       = rgb(0x1C, 0x1B, 0x1F);  // on background
    t.palette[static_cast<std::size_t>(PaletteSlot::kError)]              = rgb(0xB3, 0x26, 0x1E);  // error
    t.palette[static_cast<std::size_t>(PaletteSlot::kOnError)]            = rgb(0xFF, 0xFF, 0xFF);  // on error
    t.palette[static_cast<std::size_t>(PaletteSlot::kOutline)]            = rgb(0x79, 0x74, 0x7E);  // outline
    t.palette[static_cast<std::size_t>(PaletteSlot::kInverseSurface)]     = rgb(0x31, 0x30, 0x33);  // inverse surface (dark-on-light)

    t.typography[static_cast<std::size_t>(TypographySlot::kBody)]    = Typography { "default", 14.0F, 1.4F,  0.0F   };
    t.typography[static_cast<std::size_t>(TypographySlot::kHeading)] = Typography { "default", 22.0F, 1.25F, -0.01F };
    t.typography[static_cast<std::size_t>(TypographySlot::kLabel)]   = Typography { "default", 12.0F, 1.3F,  0.02F  };
    t.typography[static_cast<std::size_t>(TypographySlot::kCaption)] = Typography { "default", 11.0F, 1.3F,  0.03F  };

    t.spacing   = kDefaultSpacing;
    t.motion    = kDefaultMotion;
    t.elevation = kDefaultElevation;
    return t;
}

// ---- High-contrast theme ---------------------------------------------------
//
// Maximum-contrast palette for accessibility (WCAG AAA body-text =>
// contrast ratio >= 7:1). Surface is pure black, on_surface is pure
// white -- contrast is 21:1, well above AAA. Primary is canary yellow
// so a colour-blind user can still distinguish action affordances.
Theme kHighContrastTheme() noexcept
{
    Theme t;
    t.palette[static_cast<std::size_t>(PaletteSlot::kPrimary)]            = rgb(0xFF, 0xEB, 0x3B);  // primary (yellow, AAA on black)
    t.palette[static_cast<std::size_t>(PaletteSlot::kOnPrimary)]          = rgb(0x00, 0x00, 0x00);
    t.palette[static_cast<std::size_t>(PaletteSlot::kPrimaryContainer)]   = rgb(0xFF, 0xF1, 0x76);
    t.palette[static_cast<std::size_t>(PaletteSlot::kOnPrimaryContainer)] = rgb(0x00, 0x00, 0x00);
    t.palette[static_cast<std::size_t>(PaletteSlot::kSecondary)]          = rgb(0x00, 0xE5, 0xFF);  // cyan, AAA on black
    t.palette[static_cast<std::size_t>(PaletteSlot::kOnSecondary)]        = rgb(0x00, 0x00, 0x00);
    t.palette[static_cast<std::size_t>(PaletteSlot::kSurface)]            = rgb(0x00, 0x00, 0x00);  // pure black
    t.palette[static_cast<std::size_t>(PaletteSlot::kOnSurface)]          = rgb(0xFF, 0xFF, 0xFF);  // pure white (21:1)
    t.palette[static_cast<std::size_t>(PaletteSlot::kSurfaceVariant)]     = rgb(0x10, 0x10, 0x10);
    t.palette[static_cast<std::size_t>(PaletteSlot::kOnSurfaceVariant)]   = rgb(0xFF, 0xFF, 0xFF);
    t.palette[static_cast<std::size_t>(PaletteSlot::kBackground)]         = rgb(0x00, 0x00, 0x00);
    t.palette[static_cast<std::size_t>(PaletteSlot::kOnBackground)]       = rgb(0xFF, 0xFF, 0xFF);
    t.palette[static_cast<std::size_t>(PaletteSlot::kError)]              = rgb(0xFF, 0x52, 0x52);  // red-A200, ~5.5:1 + iconography
    t.palette[static_cast<std::size_t>(PaletteSlot::kOnError)]            = rgb(0x00, 0x00, 0x00);
    t.palette[static_cast<std::size_t>(PaletteSlot::kOutline)]            = rgb(0xFF, 0xFF, 0xFF);  // outlines visible at any size
    t.palette[static_cast<std::size_t>(PaletteSlot::kInverseSurface)]     = rgb(0xFF, 0xFF, 0xFF);

    // Slightly larger body text for legibility -- still under 16pt so it
    // composes with web design systems that pin body=14.
    t.typography[static_cast<std::size_t>(TypographySlot::kBody)]    = Typography { "default", 15.0F, 1.5F,  0.0F   };
    t.typography[static_cast<std::size_t>(TypographySlot::kHeading)] = Typography { "default", 24.0F, 1.3F,  -0.01F };
    t.typography[static_cast<std::size_t>(TypographySlot::kLabel)]   = Typography { "default", 13.0F, 1.4F,  0.02F  };
    t.typography[static_cast<std::size_t>(TypographySlot::kCaption)] = Typography { "default", 12.0F, 1.4F,  0.03F  };

    t.spacing   = kDefaultSpacing;
    t.motion    = kDefaultMotion;
    t.elevation = kDefaultElevation;
    return t;
}

// ---- Brand override --------------------------------------------------------

Theme apply_brand_override(Theme theme, ColorToken brand) noexcept
{
    theme.palette[static_cast<std::size_t>(PaletteSlot::kPrimary)]   = brand;
    theme.palette[static_cast<std::size_t>(PaletteSlot::kOnPrimary)] = pick_on_color(brand);
    return theme;
}

}  // namespace cd::ui::theme
