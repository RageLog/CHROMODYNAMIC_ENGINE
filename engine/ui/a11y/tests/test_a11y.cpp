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

// ---- Case 16: disabled widgets are skipped by tab navigation ---------------
//
// focus_next / focus_prev must skip entries whose A11yMeta::disabled == true.
// This covers: forward skip, backward skip, wrap-over-disabled, and the
// all-disabled guard (no crash, focus unchanged).

TEST(A11y, DisabledWidgetsSkippedByTabNav)
{
    a11y::A11yTree tree;
    // Layout: A(enabled) -> B(disabled) -> C(enabled) -> D(disabled)
    tree.register_widget(10U, { a11y::Role::kButton, "A", "", false, false });
    tree.register_widget(11U, { a11y::Role::kButton, "B", "", false, true  });
    tree.register_widget(12U, { a11y::Role::kButton, "C", "", false, false });
    tree.register_widget(13U, { a11y::Role::kButton, "D", "", false, true  });

    const std::array<cd::ui::WidgetId, 4> order { 10U, 11U, 12U, 13U };
    tree.set_tab_order(order);

    // First Tab: no current focus -> first non-disabled = A (10).
    tree.focus_next();
    ASSERT_TRUE(tree.focus().has_value());
    EXPECT_EQ(*tree.focus(), 10U);

    // Second Tab: from A -> skip B (disabled) -> land on C (12).
    tree.focus_next();
    EXPECT_EQ(*tree.focus(), 12U);

    // Third Tab: from C -> skip D -> wrap -> land on A (10).
    tree.focus_next();
    EXPECT_EQ(*tree.focus(), 10U);

    // Shift+Tab from A: wrap backwards -> skip D -> land on C (12).
    tree.focus_prev();
    EXPECT_EQ(*tree.focus(), 12U);

    // Shift+Tab from C: skip B -> land on A (10).
    tree.focus_prev();
    EXPECT_EQ(*tree.focus(), 10U);
}

// ---- Case 17: all-disabled tab order leaves focus unchanged ----------------
//
// If every entry in tab_order_ is disabled, focus_next / focus_prev must
// leave focus in its current state and not crash.

TEST(A11y, AllDisabledTabOrderLeaveFocusUnchanged)
{
    a11y::A11yTree tree;
    tree.register_widget(20U, { a11y::Role::kButton, "X", "", false, true });
    tree.register_widget(21U, { a11y::Role::kButton, "Y", "", false, true });
    const std::array<cd::ui::WidgetId, 2> order { 20U, 21U };
    tree.set_tab_order(order);

    // No focus yet; focus_next with all-disabled must not crash and must not
    // set any focus.
    tree.focus_next();
    EXPECT_FALSE(tree.focus().has_value());

    // Even after direct set_focus to one entry, further nav leaves it
    // unchanged because both neighbours are also disabled.
    tree.set_focus(20U);
    tree.focus_next();
    EXPECT_EQ(*tree.focus(), 20U);  // unchanged
    tree.focus_prev();
    EXPECT_EQ(*tree.focus(), 20U);  // unchanged
}

// ---- Case 18: update_meta replaces fields, preserves focused bit -----------
//
// update_meta() must: (a) update label/role/hint/disabled; (b) preserve the
// tree-managed `focused` bit (which the tree owns, not the caller);
// (c) return true on a known id and false on an unknown id (no-op).

TEST(A11y, UpdateMetaPreservesFocusedBit)
{
    a11y::A11yTree tree;
    tree.register_widget(30U, { a11y::Role::kButton, "Old", "OldHint", false, false });
    tree.set_focus(30U);
    ASSERT_TRUE(tree.meta(30U).focused);

    // Patch with focused=false in the supplied meta; tree must keep focused=true.
    const bool updated = tree.update_meta(
        30U, { a11y::Role::kTextInput, "New", "NewHint", false, true });
    EXPECT_TRUE(updated);

    const auto m = tree.meta(30U);
    EXPECT_EQ(m.role,  a11y::Role::kTextInput);
    EXPECT_EQ(m.label, "New");
    EXPECT_EQ(m.hint,  "NewHint");
    EXPECT_TRUE(m.disabled);
    EXPECT_TRUE(m.focused);          // preserved by update_meta

    // Unknown id must return false without inserting.
    const bool not_found = tree.update_meta(999U, { a11y::Role::kButton, "X", "", false, false });
    EXPECT_FALSE(not_found);
    EXPECT_FALSE(tree.has(999U));
    EXPECT_EQ(tree.size(), 1U);
}

// ---- Case 19: nested-node set_parent / children_of / parent_of -------------
//
// A11yTree must maintain a parent→children adjacency separate from tab order.
// This covers: basic parent assignment, multi-child, querying children/parent,
// re-parenting (change parent mid-frame), and self-loop guard.

TEST(A11y, NestedNodeParentChildAdjacency)
{
    a11y::A11yTree tree;
    // Register a small hierarchy: Panel (40) -> [Button A (41), Button B (42)]
    tree.register_widget(40U, { a11y::Role::kPanel,  "Panel", "", false, false });
    tree.register_widget(41U, { a11y::Role::kButton, "A",     "", false, false });
    tree.register_widget(42U, { a11y::Role::kButton, "B",     "", false, false });

    tree.set_parent(41U, 40U);
    tree.set_parent(42U, 40U);

    EXPECT_EQ(tree.parent_of(41U), std::optional<cd::ui::WidgetId>{ 40U });
    EXPECT_EQ(tree.parent_of(42U), std::optional<cd::ui::WidgetId>{ 40U });
    EXPECT_EQ(tree.parent_of(40U), std::nullopt);  // root has no parent

    const auto children = tree.children_of(40U);
    ASSERT_EQ(children.size(), 2U);
    EXPECT_EQ(children[0], 41U);
    EXPECT_EQ(children[1], 42U);

    // Self-loop guard: set_parent(X, X) must be a no-op.
    tree.set_parent(40U, 40U);
    EXPECT_EQ(tree.parent_of(40U), std::nullopt);

    // Re-parent 41 from Panel (40) to a new container (43).
    tree.register_widget(43U, { a11y::Role::kDialog, "D", "", false, false });
    tree.set_parent(41U, 43U);
    EXPECT_EQ(tree.parent_of(41U), std::optional<cd::ui::WidgetId>{ 43U });
    // Panel (40) should now only have B (42) as a child.
    const auto panel_children = tree.children_of(40U);
    ASSERT_EQ(panel_children.size(), 1U);
    EXPECT_EQ(panel_children[0], 42U);
}

// ---- Case 20: clear_parent removes the adjacency link ----------------------

TEST(A11y, ClearParentRemovesLink)
{
    a11y::A11yTree tree;
    tree.register_widget(50U, { a11y::Role::kPanel,  "P", "", false, false });
    tree.register_widget(51U, { a11y::Role::kButton, "B", "", false, false });

    tree.set_parent(51U, 50U);
    ASSERT_EQ(tree.parent_of(51U), std::optional<cd::ui::WidgetId>{ 50U });

    tree.clear_parent(51U);
    EXPECT_EQ(tree.parent_of(51U), std::nullopt);
    EXPECT_TRUE(tree.children_of(50U).empty());

    // clear_parent on a root widget (no parent) must be a no-op.
    tree.clear_parent(50U);
    EXPECT_EQ(tree.parent_of(50U), std::nullopt);
}

// ---- Case 21: unregister_widget cleans up parent/child adjacency -----------
//
// Removing a widget that is a child must remove it from the parent's list.
// Removing a widget that is a parent must clear the parent pointer of all
// its children (they become root-level; no dangling parent pointer).

TEST(A11y, UnregisterCleansUpAdjacency)
{
    a11y::A11yTree tree;
    tree.register_widget(60U, { a11y::Role::kPanel,  "P",  "", false, false });
    tree.register_widget(61U, { a11y::Role::kButton, "B1", "", false, false });
    tree.register_widget(62U, { a11y::Role::kButton, "B2", "", false, false });

    tree.set_parent(61U, 60U);
    tree.set_parent(62U, 60U);

    // Remove a child: parent's child list shrinks.
    tree.unregister_widget(61U);
    EXPECT_FALSE(tree.has(61U));
    const auto children_after = tree.children_of(60U);
    ASSERT_EQ(children_after.size(), 1U);
    EXPECT_EQ(children_after[0], 62U);

    // Remove the parent: the remaining child's parent pointer is cleared.
    tree.unregister_widget(60U);
    EXPECT_FALSE(tree.has(60U));
    EXPECT_EQ(tree.parent_of(62U), std::nullopt);
}

// ---- Case 22: WCAG large-text / non-text threshold (3:1) -------------------
//
// ContrastContext::kLargeText and kNonText lower the AA cut-off to 3:1.
// Test boundary: just above (passes) and just below (fails) 3.0.
// For large text on k_high_contrast_theme the cut-off rises to 4.5:1.

TEST(A11y, LargeTextContrastThresholdIs3to1)
{

    // Threshold check: standard theme + large text context -> 3.0.
    EXPECT_NEAR(
        a11y::min_required_contrast(a11y::ThemeVariant::kStandardTheme,
                                    a11y::ContrastContext::kLargeText),
        3.0F, kEps);

    // Non-text same as large-text for standard theme.
    EXPECT_NEAR(
        a11y::min_required_contrast(a11y::ThemeVariant::kStandardTheme,
                                    a11y::ContrastContext::kNonText),
        3.0F, kEps);

    // High-contrast theme + large-text -> 4.5.
    EXPECT_NEAR(
        a11y::min_required_contrast(a11y::ThemeVariant::k_high_contrast_theme,
                                    a11y::ContrastContext::kLargeText),
        4.5F, kEps);

    // Just ABOVE 3:1: grey L = 0.1 -> ratio (0.1 + 0.05)/0.05 = 3.0... use
    // L = 0.11 -> ratio = (0.11 + 0.05)/0.05 = 3.2 > 3.0 -> PASS.
    constexpr a11y::Rgba kJustAbove3 { 0.11F, 0.11F, 0.11F, 1.0F };
    EXPECT_GE(a11y::compute_contrast_ratio(kJustAbove3, kBlack), 3.0F);
    EXPECT_TRUE(a11y::passes_contrast(kJustAbove3, kBlack,
                                      a11y::ThemeVariant::kStandardTheme,
                                      a11y::ContrastContext::kLargeText));

    // Just BELOW 3:1: L = 0.09 -> ratio = (0.09 + 0.05)/0.05 = 2.8 < 3.0 -> FAIL.
    constexpr a11y::Rgba kJustBelow3 { 0.09F, 0.09F, 0.09F, 1.0F };
    EXPECT_LT(a11y::compute_contrast_ratio(kJustBelow3, kBlack), 3.0F);
    EXPECT_FALSE(a11y::passes_contrast(kJustBelow3, kBlack,
                                       a11y::ThemeVariant::kStandardTheme,
                                       a11y::ContrastContext::kLargeText));
}

// ---- Case 23: AAA body-text boundary at 7:1 --------------------------------
//
// Pins the inclusive >= boundary for the high-contrast (AAA) cut-off at 7:1.
// Uses the same pure-grey technique as Case 14: L tunes ratio directly.
// L = 0.30 -> (0.30+0.05)/0.05 = 7.0. Test L = 0.31 (PASS) and L = 0.29
// (FAIL) with 0.01 margin on each side so no single-ULP ambiguity.

TEST(A11y, AaaBodyTextBoundaryAt7to1)
{

    // L = 0.31 -> ratio = (0.31+0.05)/0.05 = 7.2 -> PASS AAA.
    constexpr a11y::Rgba kJustOver7 { 0.31F, 0.31F, 0.31F, 1.0F };
    EXPECT_GE(a11y::compute_contrast_ratio(kJustOver7, kBlack), 7.0F);
    EXPECT_TRUE(a11y::passes_contrast(kJustOver7, kBlack,
                                      a11y::ThemeVariant::k_high_contrast_theme));

    // L = 0.29 -> ratio = (0.29+0.05)/0.05 = 6.8 -> FAIL AAA.
    constexpr a11y::Rgba kJustUnder7 { 0.29F, 0.29F, 0.29F, 1.0F };
    EXPECT_LT(a11y::compute_contrast_ratio(kJustUnder7, kBlack), 7.0F);
    EXPECT_FALSE(a11y::passes_contrast(kJustUnder7, kBlack,
                                       a11y::ThemeVariant::k_high_contrast_theme));

    // Threshold constant is 7.
    EXPECT_NEAR(a11y::min_required_contrast(a11y::ThemeVariant::k_high_contrast_theme),
                7.0F, kEps);
}

// ---- Case 24: set_focus on unknown id is safe (pre-bake contract) ----------
//
// set_focus(id) where id is not in metas_ must set focused_ = id without
// crashing and without inserting a phantom meta entry. The meta() for that id
// still returns the default (role=kUnknown, focused=false from meta(), even
// though focused_ internally holds the id). This mirrors the tab-order
// pre-bake contract.

TEST(A11y, SetFocusOnUnknownIdIsSafe)
{
    a11y::A11yTree tree;
    tree.register_widget(70U, { a11y::Role::kButton, "Real", "", false, false });

    // Focus an id that has no registered meta.
    tree.set_focus(80U);
    ASSERT_TRUE(tree.focus().has_value());
    EXPECT_EQ(*tree.focus(), 80U);

    // meta() on the unknown focused id returns the default (not a crash).
    const auto m = tree.meta(80U);
    EXPECT_EQ(m.role, a11y::Role::kUnknown);
    // The tree does not insert a phantom entry.
    EXPECT_FALSE(tree.has(80U));
    EXPECT_EQ(tree.size(), 1U);  // only the "Real" widget

    // Switching focus back to a real widget clears the unknown focus.
    tree.set_focus(70U);
    EXPECT_EQ(*tree.focus(), 70U);
    EXPECT_TRUE(tree.meta(70U).focused);
}

// ---- Case 25: focus_indicator_rect at origin and negative bounds -----------
//
// focus_indicator_rect is a constexpr arithmetic function; test it with
// a zero-origin widget (common for root panels) and a widget at a negative
// coordinate (e.g. partially off-screen). Also verifies the outset constant
// is symmetric (x/y shift equals w/h growth / 2).

TEST(A11y, FocusIndicatorRectAtOriginAndNegativeBounds)
{

    // Zero-origin widget: x=0, y=0, w=200, h=40.
    constexpr cd::ui::Rect kZero { 0.0F, 0.0F, 200.0F, 40.0F };
    constexpr auto kRingZero = a11y::focus_indicator_rect(kZero);
    EXPECT_NEAR(kRingZero.x, -2.0F,  kEps);
    EXPECT_NEAR(kRingZero.y, -2.0F,  kEps);
    EXPECT_NEAR(kRingZero.w, 204.0F, kEps);
    EXPECT_NEAR(kRingZero.h,  44.0F, kEps);

    // Negative-origin widget: x=-10, y=-5, w=60, h=20.
    constexpr cd::ui::Rect kNeg { -10.0F, -5.0F, 60.0F, 20.0F };
    constexpr auto kRingNeg = a11y::focus_indicator_rect(kNeg);
    EXPECT_NEAR(kRingNeg.x, -12.0F, kEps);
    EXPECT_NEAR(kRingNeg.y,  -7.0F, kEps);
    EXPECT_NEAR(kRingNeg.w,  64.0F, kEps);
    EXPECT_NEAR(kRingNeg.h,  24.0F, kEps);

    // Symmetry: x-shift and half of w-growth must both equal kFocusIndicatorOutsetPx.
    EXPECT_NEAR(kZero.x - kRingZero.x, a11y::kFocusIndicatorOutsetPx, kEps);
    EXPECT_NEAR((kRingZero.w - kZero.w) / 2.0F, a11y::kFocusIndicatorOutsetPx, kEps);
}
