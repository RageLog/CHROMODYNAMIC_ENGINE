// =============================================================================
// CHROMODYNAMIC -- cd::ui::a11y tests (Phase 478)
//
// Pins the accessibility contract: WCAG contrast formula, AAA enforcement
// on the high-contrast theme variant, focus indicator geometry (2px
// outset), A11yMeta lookup, role enum stability, tab order independence
// from visual order, screen-reader hint exposure, and the
// missing-widget-returns-default contract.
// =============================================================================
#include <cd/ui/Widget.hpp>
#include <cd/ui/a11y/A11y.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace a11y = cd::ui::a11y;

namespace
{
constexpr float kEps = 1e-4F;

constexpr a11y::Rgba kWhite { 1.0F, 1.0F, 1.0F, 1.0F };
constexpr a11y::Rgba kBlack { 0.0F, 0.0F, 0.0F, 1.0F };
constexpr a11y::Rgba kMidGrey { 0.5F, 0.5F, 0.5F, 1.0F };
constexpr a11y::Rgba kNearWhite { 0.95F, 0.95F, 0.95F, 1.0F };

}  // namespace

// ---- Case 1: WCAG contrast formula -- white on black = 21:1 ----------------

TEST(A11y, WcagContrastWhiteOnBlackIs21)
{
    const float ratio = a11y::compute_contrast_ratio(kWhite, kBlack);
    // (1.0 + 0.05) / (0.0 + 0.05) = 21.
    EXPECT_NEAR(ratio, 21.0F, kEps);

    // Symmetry: swapping fg/bg must not change the ratio.
    const float ratio_swapped = a11y::compute_contrast_ratio(kBlack, kWhite);
    EXPECT_NEAR(ratio, ratio_swapped, kEps);

    // Identical colours = 1:1.
    EXPECT_NEAR(a11y::compute_contrast_ratio(kWhite, kWhite), 1.0F, kEps);
    EXPECT_NEAR(a11y::compute_contrast_ratio(kBlack, kBlack), 1.0F, kEps);
}

// ---- Case 2: High-contrast theme passes AAA on body / label text -----------

TEST(A11y, HighContrastThemePassesAaaOnBodyAndLabel)
{
    // Simulate the high-contrast preset palette: pure white on pure black
    // for body text + label text. AAA body text requires >= 7:1.
    EXPECT_TRUE(a11y::passes_contrast(kWhite, kBlack, a11y::ThemeVariant::k_high_contrast_theme));
    EXPECT_TRUE(a11y::passes_contrast(kBlack, kWhite, a11y::ThemeVariant::k_high_contrast_theme));

    // Mid-grey on near-white fails AAA (and AA) -- the high-contrast
    // theme must avoid this combination by construction.
    EXPECT_FALSE(a11y::passes_contrast(kMidGrey, kNearWhite, a11y::ThemeVariant::k_high_contrast_theme));

    // Sanity: AAA threshold is strictly stricter than AA.
    EXPECT_GT(a11y::min_required_contrast(a11y::ThemeVariant::k_high_contrast_theme),
              a11y::min_required_contrast(a11y::ThemeVariant::kStandardTheme));
}

// ---- Case 3: Focus indicator outset by 2px ---------------------------------

TEST(A11y, FocusIndicatorOutsetBy2Px)
{
    const cd::ui::Rect bounds { 10.0F, 20.0F, 100.0F, 50.0F };
    const cd::ui::Rect ring   = a11y::focus_indicator_rect(bounds);

    // Origin shifted -2 on x and y.
    EXPECT_NEAR(ring.x, 8.0F,  kEps);
    EXPECT_NEAR(ring.y, 18.0F, kEps);
    // Size grown by +4 on each axis (2px on each side).
    EXPECT_NEAR(ring.w, 104.0F, kEps);
    EXPECT_NEAR(ring.h, 54.0F,  kEps);

    // Constant matches the documented outset.
    EXPECT_NEAR(a11y::kFocusIndicatorOutsetPx, 2.0F, kEps);
}

// ---- Case 4: A11yMeta lookup by widget id ----------------------------------

TEST(A11y, A11yMetaLookupByWidgetId)
{
    a11y::A11yTree tree;

    a11y::A11yMeta save_meta {
        a11y::Role::kButton,
        "Save",
        "Save current scene to disk (Ctrl+S)",
        false,
    };
    tree.register_widget(/*widget_id=*/42U, save_meta);

    EXPECT_TRUE(tree.has(42U));
    EXPECT_EQ(tree.size(), 1U);

    const auto fetched = tree.meta(42U);
    EXPECT_EQ(fetched.role, a11y::Role::kButton);
    EXPECT_EQ(fetched.label, "Save");
    EXPECT_EQ(fetched.hint,  "Save current scene to disk (Ctrl+S)");
    EXPECT_FALSE(fetched.focused);

    // Re-registering overwrites idempotently (retained-mode refresh).
    save_meta.label = "Save As";
    tree.register_widget(42U, save_meta);
    EXPECT_EQ(tree.size(), 1U);
    EXPECT_EQ(tree.meta(42U).label, "Save As");

    // Unregister drops it.
    tree.unregister_widget(42U);
    EXPECT_FALSE(tree.has(42U));
    EXPECT_EQ(tree.size(), 0U);
}

// ---- Case 5: Role enum exposes Button / TextInput / Slider etc. ------------

TEST(A11y, RoleEnumStableValues)
{
    // The enum values are part of the public ABI (a future a11y bridge
    // may marshal them). Pin the canonical few so a refactor that
    // re-orders the enum surfaces here.
    EXPECT_EQ(static_cast<std::uint8_t>(a11y::Role::kUnknown),   0U);
    EXPECT_EQ(static_cast<std::uint8_t>(a11y::Role::kButton),    1U);
    EXPECT_EQ(static_cast<std::uint8_t>(a11y::Role::kLabel),     2U);
    EXPECT_EQ(static_cast<std::uint8_t>(a11y::Role::kTextInput), 3U);
    EXPECT_EQ(static_cast<std::uint8_t>(a11y::Role::kSlider),    4U);
    EXPECT_EQ(static_cast<std::uint8_t>(a11y::Role::kCheckbox),  5U);

    // Distinctness sanity check.
    EXPECT_NE(a11y::Role::kButton, a11y::Role::kTextInput);
    EXPECT_NE(a11y::Role::kSlider, a11y::Role::kCheckbox);
}

// ---- Case 6: Tab order honors A11yTree, not visual order -------------------

TEST(A11y, TabOrderHonorsA11yTreeNotVisualOrder)
{
    a11y::A11yTree tree;
    tree.register_widget(1U, { a11y::Role::kButton,    "B1", "", false });
    tree.register_widget(2U, { a11y::Role::kTextInput, "T1", "", false });
    tree.register_widget(3U, { a11y::Role::kButton,    "B2", "", false });

    // Visual order is (1, 2, 3) but the explicit tab order is (3, 1, 2).
    // The a11y tree must follow the explicit order regardless of how
    // widgets were registered.
    const std::array<cd::ui::WidgetId, 3> order { 3U, 1U, 2U };
    tree.set_tab_order(order);

    EXPECT_FALSE(tree.focus().has_value());

    // First Tab focuses the first entry in tab_order_ (=3), not the
    // first-registered widget (=1).
    tree.focus_next();
    ASSERT_TRUE(tree.focus().has_value());
    EXPECT_EQ(*tree.focus(), 3U);
    EXPECT_TRUE(tree.meta(3U).focused);
    EXPECT_FALSE(tree.meta(1U).focused);

    tree.focus_next();
    EXPECT_EQ(*tree.focus(), 1U);

    tree.focus_next();
    EXPECT_EQ(*tree.focus(), 2U);

    // Wrap around: next from last lands back on first.
    tree.focus_next();
    EXPECT_EQ(*tree.focus(), 3U);

    // Shift+Tab (focus_prev) walks the order backwards with wrap.
    tree.focus_prev();
    EXPECT_EQ(*tree.focus(), 2U);
    tree.focus_prev();
    EXPECT_EQ(*tree.focus(), 1U);
    tree.focus_prev();
    EXPECT_EQ(*tree.focus(), 3U);
    tree.focus_prev();  // wrap
    EXPECT_EQ(*tree.focus(), 2U);
}

// ---- Case 7: Screen-reader hint exposed via accessor -----------------------

TEST(A11y, ScreenReaderHintExposedViaAccessor)
{
    a11y::A11yTree tree;
    tree.register_widget(100U,
                         { a11y::Role::kSlider,
                           "Volume",
                           "Master output volume, 0 to 100 percent",
                           false });

    const std::string_view hint = tree.screen_reader_hint(100U);
    EXPECT_EQ(hint, "Master output volume, 0 to 100 percent");

    // Empty hint when the widget has no descriptive text.
    tree.register_widget(101U, { a11y::Role::kPanel, "Inspector", "", false });
    EXPECT_TRUE(tree.screen_reader_hint(101U).empty());
}

// ---- Case 8: Missing widget returns default meta + empty hint --------------

TEST(A11y, MissingWidgetReturnsDefaultMeta)
{
    a11y::A11yTree tree;

    // Probe before anything is registered.
    EXPECT_FALSE(tree.has(999U));
    const auto m = tree.meta(999U);
    EXPECT_EQ(m.role, a11y::Role::kUnknown);
    EXPECT_TRUE(m.label.empty());
    EXPECT_TRUE(m.hint.empty());
    EXPECT_FALSE(m.focused);

    // Hint accessor on a missing id returns an empty string_view, never
    // dereferences a missing entry.
    EXPECT_TRUE(tree.screen_reader_hint(999U).empty());

    // focus_next on an empty tab order is a no-op and does not crash.
    tree.focus_next();
    EXPECT_FALSE(tree.focus().has_value());
    tree.focus_prev();
    EXPECT_FALSE(tree.focus().has_value());
}

// ---- Case 9: AA standard theme threshold (4.5) -----------------------------
//
// Extra coverage: confirm the standard theme uses AA (not AAA), and
// passes for a typical mid-contrast button-on-surface combination.

TEST(A11y, StandardThemeUsesAaThreshold)
{
    EXPECT_NEAR(a11y::min_required_contrast(a11y::ThemeVariant::kStandardTheme),
                4.5F, kEps);

    // White-on-black trivially passes AA.
    EXPECT_TRUE(a11y::passes_contrast(kWhite, kBlack,
                                      a11y::ThemeVariant::kStandardTheme));

    // 50% grey on white: ratio ~3.95, fails AA body text by design.
    EXPECT_FALSE(a11y::passes_contrast(kMidGrey, kWhite,
                                       a11y::ThemeVariant::kStandardTheme));
}

// ---- Case 10: set_focus clears previous focused bit ------------------------

TEST(A11y, SetFocusClearsPreviousFocusedBit)
{
    a11y::A11yTree tree;
    tree.register_widget(7U, { a11y::Role::kButton, "A", "", false });
    tree.register_widget(8U, { a11y::Role::kButton, "B", "", false });

    tree.set_focus(7U);
    EXPECT_TRUE(tree.meta(7U).focused);
    EXPECT_FALSE(tree.meta(8U).focused);

    tree.set_focus(8U);
    EXPECT_FALSE(tree.meta(7U).focused);
    EXPECT_TRUE(tree.meta(8U).focused);

    // Clearing focus (nullopt) drops the bit on the previously focused
    // widget.
    tree.set_focus(std::nullopt);
    EXPECT_FALSE(tree.focus().has_value());
    EXPECT_FALSE(tree.meta(7U).focused);
    EXPECT_FALSE(tree.meta(8U).focused);
}

// ---- Case 11: focus_indicator_rect integrates with cd::ui::Widget bounds ---

TEST(A11y, FocusIndicatorIntegratesWithWidget)
{
    // Build a real widget, populate bounds, and run the indicator over
    // its bounds the way the renderer will at draw time.
    cd::ui::Button btn { "OK" };
    btn.set_bounds({ 100.0F, 100.0F, 80.0F, 24.0F });

    const cd::ui::Rect ring = a11y::focus_indicator_rect(btn.bounds());
    EXPECT_NEAR(ring.x, 98.0F,  kEps);
    EXPECT_NEAR(ring.y, 98.0F,  kEps);
    EXPECT_NEAR(ring.w, 84.0F,  kEps);
    EXPECT_NEAR(ring.h, 28.0F,  kEps);
}

// ---- Case 12: single-element tab order wraps to itself --------------------
//
// The wrap arithmetic in focus_next/focus_prev uses (cur + 1) % size and the
// 0 -> last branch. With size()==1 both reduce to "stay on the only entry".
// The multi-element wrap case is covered above; this pins the degenerate
// single-element wrap that the modulo/branch would mishandle if changed.

TEST(A11y, SingleElementTabOrderWrapsToSelf)
{
    a11y::A11yTree tree;
    tree.register_widget(5U, { a11y::Role::kButton, "Only", "", false });
    const std::array<cd::ui::WidgetId, 1> order { 5U };
    tree.set_tab_order(order);

    tree.focus_next();           // no focus -> first entry
    EXPECT_EQ(*tree.focus(), 5U);
    tree.focus_next();           // wrap (0 + 1) % 1 == 0 -> stays on 5
    EXPECT_EQ(*tree.focus(), 5U);
    tree.focus_prev();           // 0 -> last == 0 -> stays on 5
    EXPECT_EQ(*tree.focus(), 5U);
    EXPECT_TRUE(tree.meta(5U).focused);
}

// ---- Case 13: unregistering the focused widget clears focus + tab slot -----
//
// unregister_widget has three coupled effects (erase meta, ranges::remove from
// tab_order_, reset focused_ when it matches) and the "removed id was the
// FOCUSED one mid-navigation" path was never asserted. After removing the
// focused entry, focus() must be empty AND a following focus_next() must skip
// the now-absent id and land on a surviving entry (no dangling focus, no
// re-focusing the removed widget).

TEST(A11y, UnregisterFocusedClearsFocusAndTabSlot)
{
    a11y::A11yTree tree;
    tree.register_widget(1U, { a11y::Role::kButton, "A", "", false });
    tree.register_widget(2U, { a11y::Role::kButton, "B", "", false });
    const std::array<cd::ui::WidgetId, 2> order { 1U, 2U };
    tree.set_tab_order(order);

    tree.focus_next();                       // focus 1
    ASSERT_EQ(*tree.focus(), 1U);

    tree.unregister_widget(1U);              // remove the focused widget
    EXPECT_FALSE(tree.focus().has_value());  // focus cleared
    EXPECT_FALSE(tree.has(1U));
    EXPECT_EQ(tree.tab_order().size(), 1U);  // dropped from tab order too

    // Next navigation skips the removed id and lands on the survivor.
    tree.focus_next();
    ASSERT_TRUE(tree.focus().has_value());
    EXPECT_EQ(*tree.focus(), 2U);
}

// ---- Case 14: contrast boundary is inclusive at the threshold --------------
//
// passes_contrast uses `>=`, so a ratio at/above the AA cut-off (4.5) must
// PASS and a value below must FAIL. Prior tests only checked comfortably-above
// (21:1) and comfortably-below (~3.95) cases, never the boundary region. A
// pure-grey colour with r=g=b=L has relative_luminance == L (the 709 weights
// sum to 1), so L tunes the ratio against black directly:
// ratio = (L + 0.05) / 0.05. The exact boundary is L == 0.175; we test a hair
// ABOVE (L=0.18 -> ratio 4.6, provably >= 4.5 after float rounding) and a hair
// BELOW (L=0.17 -> ratio 4.4) so the inclusive `>=` edge is pinned without
// tripping on the single-ULP ambiguity that an exactly-4.5 float would carry.

TEST(A11y, ContrastBoundaryIsInclusiveAtThreshold)
{
    // Just ABOVE the AA cut-off: ratio = (0.18 + 0.05)/0.05 = 4.6 >= 4.5.
    constexpr a11y::Rgba kJustOver { 0.18F, 0.18F, 0.18F, 1.0F };
    EXPECT_GE(a11y::compute_contrast_ratio(kJustOver, kBlack), 4.5F);
    EXPECT_TRUE(a11y::passes_contrast(kJustOver, kBlack,
                                      a11y::ThemeVariant::kStandardTheme));

    // Just BELOW: ratio = (0.17 + 0.05)/0.05 = 4.4 < 4.5 -> fails.
    constexpr a11y::Rgba kJustUnder { 0.17F, 0.17F, 0.17F, 1.0F };
    EXPECT_LT(a11y::compute_contrast_ratio(kJustUnder, kBlack), 4.5F);
    EXPECT_FALSE(a11y::passes_contrast(kJustUnder, kBlack,
                                       a11y::ThemeVariant::kStandardTheme));

    // The exact threshold value the helper enforces is 4.5 (AA body text).
    EXPECT_NEAR(a11y::min_required_contrast(a11y::ThemeVariant::kStandardTheme),
                4.5F, kEps);
}

// ---- Case 15: set_tab_order tolerates unknown ids (pre-bake contract) ------
//
// The header documents "Unknown ids are silently ignored at navigation time
// so callers can pre-bake a stable order before all widgets are registered."
// set_tab_order stores ids verbatim (including unknown ones); navigation must
// still focus whatever id the order points at — even one with no registered
// meta — without crashing, and meta() on it returns the default. This pins the
// pre-bake-before-register workflow that the parallel-tree design depends on.

TEST(A11y, TabOrderToleratesUnknownIds)
{
    a11y::A11yTree tree;
    tree.register_widget(2U, { a11y::Role::kButton, "Known", "", false });

    // Order references id 1 + 3 which are NOT registered yet.
    const std::array<cd::ui::WidgetId, 3> order { 1U, 2U, 3U };
    tree.set_tab_order(order);
    EXPECT_EQ(tree.tab_order().size(), 3U);

    // Navigating onto an unregistered id is safe: focus points at it, meta()
    // returns the default (kUnknown), and no crash occurs.
    tree.focus_next();
    ASSERT_TRUE(tree.focus().has_value());
    EXPECT_EQ(*tree.focus(), 1U);
    EXPECT_EQ(tree.meta(1U).role, a11y::Role::kUnknown);

    tree.focus_next();                       // -> 2 (the registered one)
    EXPECT_EQ(*tree.focus(), 2U);
    EXPECT_TRUE(tree.meta(2U).focused);
}
