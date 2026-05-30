# cd::ui_theme

**Purpose**: V2 design token system for the `cd::ui::*` family. Replaces the legacy 2-palette stub at `engine/ui/include/cd/ui/Theme.hpp` (which is kept untouched so pre-V2 call sites continue to compile). Phase 474 of the UI roadmap.

**Namespace**: `cd::ui::theme`.

**Headers**: `cd/ui/theme/Theme.hpp` (note the `/theme/` segment -- this is a NEW path, distinct from the legacy `cd/ui/Theme.hpp`).

**Dependencies**: `cd::core` only. Deliberately NOT `cd::ui`, so headless asset / tooling / CLI consumers can read tokens without dragging the widget tree in.

## Primary types

- `cd::ui::theme::ColorToken` -- linear-friendly RGBA float in [0..1].
- `cd::ui::theme::Typography` -- `font_name`, `size_pt`, `line_height` multiplier, `letter_spacing` in em.
- `cd::ui::theme::Spacing` -- 5-step semantic scale: `xs / sm / md / lg / xl` (pixels @ 1.0 DPI scale).
- `cd::ui::theme::Motion` -- 3 duration buckets: `duration_short / duration_medium / duration_long` (milliseconds).
- `cd::ui::theme::Elevation` -- `shadow_blur`, `shadow_offset_y` (pixels). Elevation level 0 = no shadow.
- `cd::ui::theme::Theme` -- aggregate: 16-slot palette (`std::array<ColorToken, 16>`) indexed via `PaletteSlot`, 4-slot typography (`std::array<Typography, 4>`) indexed via `TypographySlot`, plus the `Spacing` / `Motion` / `Elevation` sub-structs.

## Palette slots (Material 3 names)

`kPrimary / kOnPrimary / kPrimaryContainer / kOnPrimaryContainer / kSecondary / kOnSecondary / kSurface / kOnSurface / kSurfaceVariant / kOnSurfaceVariant / kBackground / kOnBackground / kError / kOnError / kOutline / kInverseSurface`.

These match the Material 3 reference scheme (https://m3.material.io/styles/color/system) so callers porting from Figma / MUI / web design systems map slot-for-slot.

## Typography slots

`kBody / kHeading / kLabel / kCaption`.

## Built-in themes

- `cd::ui::theme::kDarkTheme()` -- Material 3 dark reference, near-black surface with lavender primary.
- `cd::ui::theme::kLightTheme()` -- Material 3 light reference, near-white surface with purple primary.
- `cd::ui::theme::kHighContrastTheme()` -- accessibility preset, WCAG AAA body-text contrast (`contrast_ratio(on_surface, surface) >= 7`, in practice 21:1 white-on-black).

## Brand override

```cpp
auto theme = cd::ui::theme::kDarkTheme();
theme = cd::ui::theme::apply_brand_override(theme, cd::ui::theme::ColorToken{0.05F, 0.10F, 0.45F, 1.0F});
// theme.color(PaletteSlot::kPrimary) is now the brand colour, and
// theme.color(PaletteSlot::kOnPrimary) is auto-flipped to white/black
// so body text stays readable against the new primary.
```

## Helpers

- `relative_luminance(ColorToken)` -- Rec. 709 perceived luminance.
- `contrast_ratio(ColorToken a, ColorToken b)` -- WCAG 2.1 contrast in [1..21].

## Usage

```cpp
#include <cd/ui/theme/Theme.hpp>

namespace tt = cd::ui::theme;
const auto theme = tt::kDarkTheme();
const tt::ColorToken surface = theme.color(tt::PaletteSlot::kSurface);
const tt::Typography body    = theme.font(tt::TypographySlot::kBody);
const float pad_md           = theme.spacing.md;
const float fade_ms          = theme.motion.duration_medium;
```

## Test command

`ctest --preset ninja-debug -R cd_test_theme --output-on-failure`. 10 cases covering dark/light/high-contrast invariants, monotonic spacing, brand override readability, typography sanity, motion ordering, elevation-0 = no-shadow, palette/typography sizes, and contrast symmetry.

## Out of scope (V3+)

- Per-state palette (hover / pressed / disabled) -- deferred until the widget catalog settles on an interaction model.
- Token theming via TOML/JSON at runtime -- the V2 contract is compile-time `constexpr`-friendly so binaries can pin a theme without an asset pipeline.
- Animated theme transitions -- callers can interpolate `ColorToken` and `Spacing` by hand for now.
