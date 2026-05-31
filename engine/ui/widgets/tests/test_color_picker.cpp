// =============================================================================
// CHROMODYNAMIC -- cd::ui::widgets::ColorPicker tests (Phase T2.2)
//
// 6 cases:
//   1. rgb_to_oklch -> oklch_to_rgb round-trip identity within 1e-3
//   2. Wheel drag updates HSV (hue / saturation)
//   3. RGB slider updates in sync with HSV wheel
//   4. Hex input "#RRGGBB" + "#RRGGBBAA" parse
//   5. Palette history: adds on commit, oldest evicts at 9th
//   6. oklch_to_rgb stays in [0,1] for valid LCh inputs
//
// All tests are CPU-only / headless: no GPU, no font. The draw() path is
// exercised in the cross-widget smoke test in test_widgets.cpp.
// =============================================================================
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/ColorPicker.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace w = cd::ui::widgets;
namespace r = cd::ui::renderer;

namespace
{

constexpr w::Rect kPickerRect { 0.0F, 0.0F, 260.0F, 320.0F };

[[nodiscard]] w::InputState make_press(float mx, float my)
{
    w::InputState in;
    in.pointer.mouse_x       = mx;
    in.pointer.mouse_y       = my;
    in.pointer.left_down     = true;
    in.pointer.left_pressed  = true;
    in.pointer.left_released = false;
    return in;
}

[[nodiscard]] w::InputState make_drag(float mx, float my)
{
    w::InputState in;
    in.pointer.mouse_x       = mx;
    in.pointer.mouse_y       = my;
    in.pointer.left_down     = true;
    in.pointer.left_pressed  = false;
    in.pointer.left_released = false;
    return in;
}

[[nodiscard]] w::InputState make_release(float mx, float my)
{
    w::InputState in;
    in.pointer.mouse_x       = mx;
    in.pointer.mouse_y       = my;
    in.pointer.left_down     = false;
    in.pointer.left_pressed  = false;
    in.pointer.left_released = true;
    return in;
}

}  // namespace

// =============================================================================
// Case 1 -- OKLCh round-trip identity
// =============================================================================

TEST(UiColorPickerOklch, RoundTripIdentity)
{
    // Sample several colours across the gamut.
    constexpr std::array<std::array<float, 3>, 6> kSamples {{
        {{1.0F, 0.0F, 0.0F}},   // red
        {{0.0F, 1.0F, 0.0F}},   // green
        {{0.0F, 0.0F, 1.0F}},   // blue
        {{1.0F, 1.0F, 0.0F}},   // yellow
        {{0.5F, 0.5F, 0.5F}},   // mid-grey
        {{0.2F, 0.6F, 0.9F}},   // sky-blue
    }};

    for (const auto& s : kSamples)
    {
        const float r_in = s[0];
        const float g_in = s[1];
        const float b_in = s[2];

        const w::Float3 lch = w::rgb_to_oklch(r_in, g_in, b_in);
        const w::Float3 rgb = w::oklch_to_rgb(lch.x, lch.y, lch.z);

        EXPECT_NEAR(rgb.x, r_in, 1e-3F) << "R mismatch for sample "
            << r_in << " " << g_in << " " << b_in;
        EXPECT_NEAR(rgb.y, g_in, 1e-3F) << "G mismatch for sample "
            << r_in << " " << g_in << " " << b_in;
        EXPECT_NEAR(rgb.z, b_in, 1e-3F) << "B mismatch for sample "
            << r_in << " " << g_in << " " << b_in;
    }
}

// =============================================================================
// Case 2 -- Wheel drag updates HSV (hue / saturation)
// =============================================================================

TEST(UiColorPickerTick, WheelRingDragUpdatesHue)
{
    // For kPickerRect (260x320):
    //   wheel_size = min(260, 320*0.48) = min(260, 153.6) = 153.6
    //   wheel_cx   = 0 + (260 - 153.6)/2 = 53.2
    //   wheel_cy   = 0 (top-left y)
    //   Centre     = (53.2 + 76.8, 76.8) = (130, 76.8)
    //   ring_bw    = 153.6 * 0.15 = 23.04
    //   mid-ring r = 76.8 - 11.52 = 65.28

    const float cx = 130.0F;
    const float cy = 76.8F;
    const float ring_r = 65.28F;

    w::ColorPicker cp;
    cp.set_rect(kPickerRect);
    // Give a well-defined non-zero hue so the comparison is meaningful.
    cp.set_hsv(90.0F, 1.0F, 1.0F);  // lime-green

    const float hue_before = cp.hue();

    // Click at 180 deg (left of centre) -- angle should become ~180.
    const float mx = cx - ring_r;
    const float my = cy;

    cp.tick(make_press(mx, my));
    cp.tick(make_drag(mx, my));
    cp.tick(make_release(mx, my));

    const float hue_after = cp.hue();

    // Hue must have changed.
    EXPECT_NE(hue_before, hue_after);
    // atan2(0, -1) * (180/pi) = 180 degrees.
    EXPECT_NEAR(hue_after, 180.0F, 15.0F);
}

TEST(UiColorPickerTick, WheelInnerDragUpdatesSaturation)
{
    // inner_x = wheel_cx + ring_bw = 53.2 + 23.04 = 76.24
    // inner_y = ring_bw            = 23.04
    // inner_w = inner_h = 153.6 - 2*23.04 = 107.52
    const float inner_x  = 76.24F;
    const float inner_y  = 23.04F;

    w::ColorPicker cp;
    cp.set_rect(kPickerRect);
    cp.set_hsv(180.0F, 0.5F, 0.5F);

    // Drag to left edge of inner square -> saturation = 0.
    cp.tick(make_press(inner_x + 1.0F, inner_y + 30.0F));
    cp.tick(make_release(inner_x + 1.0F, inner_y + 30.0F));

    EXPECT_NEAR(cp.saturation(), 0.0F, 0.02F);

    // Drag to right edge -> saturation = 1.
    const float inner_right = inner_x + 107.52F - 1.0F;
    cp.tick(make_press(inner_right, inner_y + 30.0F));
    cp.tick(make_release(inner_right, inner_y + 30.0F));

    EXPECT_NEAR(cp.saturation(), 1.0F, 0.02F);
}

// =============================================================================
// Case 3 -- RGB slider updates HSV mirror in sync
// =============================================================================

TEST(UiColorPickerTick, RgbSliderSyncsHsv)
{
    // slider layout:
    //   wheel_size = 153.6, wheel_y ends at 153.6, gap = 6 -> sliders start at 159.6
    //   slider_h   = 320 * 0.09 = 28.8
    //   slider_x   = 4, slider_w = 252
    //
    //   r_slider centre y = 159.6 + 28.8/2 = 174.0

    w::ColorPicker cp;
    cp.set_rect(kPickerRect);
    cp.set_value(w::ColorF { 1.0F, 0.0F, 0.0F, 1.0F });  // pure red

    const float r_slider_mid_y = 159.6F + 14.4F;          // mid of r_slider
    const float r_slider_half  = 4.0F + 252.0F * 0.5F;    // 50% along track

    cp.tick(make_press(r_slider_half, r_slider_mid_y));
    cp.tick(make_release(r_slider_half, r_slider_mid_y));

    // Red channel should be approximately 0.5.
    EXPECT_NEAR(cp.value().r, 0.5F, 0.05F);

    // Brightness (V = max(r,g,b)) should be near 0.5.
    EXPECT_NEAR(cp.brightness(), 0.5F, 0.1F);

    // Hue should remain near 0 (red axis).
    EXPECT_NEAR(cp.hue(), 0.0F, 5.0F);
}

// =============================================================================
// Case 4 -- Hex input parse "#RRGGBB" and "#RRGGBBAA"
// =============================================================================

TEST(UiColorPickerHex, ParseRRGGBB)
{
    w::ColorPicker cp;
    EXPECT_TRUE(cp.set_from_hex("#FF8000"));

    EXPECT_NEAR(cp.value().r, 1.0F,             1e-3F);
    EXPECT_NEAR(cp.value().g, 128.0F / 255.0F,  1e-3F);
    EXPECT_NEAR(cp.value().b, 0.0F,             1e-3F);
    EXPECT_NEAR(cp.value().a, 1.0F,             1e-3F);
}

TEST(UiColorPickerHex, ParseRRGGBBAA)
{
    w::ColorPicker cp;
    EXPECT_TRUE(cp.set_from_hex("#1A2B3C80"));

    EXPECT_NEAR(cp.value().r, 0x1A / 255.0F, 1e-3F);
    EXPECT_NEAR(cp.value().g, 0x2B / 255.0F, 1e-3F);
    EXPECT_NEAR(cp.value().b, 0x3C / 255.0F, 1e-3F);
    EXPECT_NEAR(cp.value().a, 0x80 / 255.0F, 1e-3F);
}

TEST(UiColorPickerHex, InvalidHexRejected)
{
    w::ColorPicker cp;
    cp.set_value(w::ColorF { 1.0F, 0.0F, 0.0F, 1.0F });

    EXPECT_FALSE(cp.set_from_hex("FFFFFF"));    // missing '#'
    EXPECT_FALSE(cp.set_from_hex("#GGHHII"));   // invalid hex digits
    EXPECT_FALSE(cp.set_from_hex("#FFF"));      // too short

    EXPECT_NEAR(cp.value().r, 1.0F, 1e-4F);
    EXPECT_NEAR(cp.value().g, 0.0F, 1e-4F);
    EXPECT_NEAR(cp.value().b, 0.0F, 1e-4F);
}

TEST(UiColorPickerHex, HexStringRoundTrip)
{
    w::ColorPicker cp;
    cp.set_value(w::ColorF { 0.0F, 0.5F, 1.0F, 0.75F });

    const std::string s = cp.hex_string();
    ASSERT_EQ(s[0], '#');
    EXPECT_EQ(s.size(), 9U);  // RRGGBBAA since alpha != 1.0

    w::ColorPicker cp2;
    EXPECT_TRUE(cp2.set_from_hex(s));
    EXPECT_NEAR(cp2.value().r, cp.value().r, 1.0F / 255.0F);
    EXPECT_NEAR(cp2.value().g, cp.value().g, 1.0F / 255.0F);
    EXPECT_NEAR(cp2.value().b, cp.value().b, 1.0F / 255.0F);
    EXPECT_NEAR(cp2.value().a, cp.value().a, 1.0F / 255.0F);
}

// =============================================================================
// Case 5 -- Palette history: adds on commit, oldest evicts at 9th
// =============================================================================

TEST(UiColorPickerPalette, AddsOnCommitAndEvictsAt9th)
{
    w::ColorPicker cp;
    constexpr std::size_t kSlots = w::ColorPicker::kPaletteSlots;  // 8

    // Fill 8 slots.
    for (std::size_t i = 0U; i < kSlots; ++i)
    {
        cp.set_value(w::ColorF {
            static_cast<float>(i) / static_cast<float>(kSlots),
            0.0F, 0.0F, 1.0F
        });
        cp.commit_to_palette();
        EXPECT_EQ(cp.palette_used(), i + 1U);
    }
    EXPECT_EQ(cp.palette_used(), kSlots);

    // 9th commit: oldest evicted, used stays at 8.
    cp.set_value(w::ColorF { 0.99F, 0.5F, 0.1F, 1.0F });
    cp.commit_to_palette();
    EXPECT_EQ(cp.palette_used(), kSlots);

    // 10th commit: still 8.
    cp.set_value(w::ColorF { 0.1F, 0.2F, 0.3F, 1.0F });
    cp.commit_to_palette();
    EXPECT_EQ(cp.palette_used(), kSlots);

    // palette() array has kSlots entries accessible.
    const auto& pal = cp.palette();
    static_assert(std::tuple_size_v<std::decay_t<decltype(pal)>> == kSlots,
                  "palette must have kPaletteSlots entries");
    (void) pal;
}

TEST(UiColorPickerPalette, SlotCountIncreasesUntilFull)
{
    w::ColorPicker cp;
    EXPECT_EQ(cp.palette_used(), 0U);

    cp.set_value(w::ColorF { 1.0F, 0.0F, 0.0F, 1.0F });
    cp.commit_to_palette();
    EXPECT_EQ(cp.palette_used(), 1U);

    cp.set_value(w::ColorF { 0.0F, 1.0F, 0.0F, 1.0F });
    cp.commit_to_palette();
    EXPECT_EQ(cp.palette_used(), 2U);
}

// =============================================================================
// Case 6 -- oklch_to_rgb stays in [0,1] for valid LCh inputs
// =============================================================================

TEST(UiColorPickerOklch, OutputClamped0To1ForValidLch)
{
    constexpr int   kStepsL = 5;
    constexpr int   kStepsC = 5;
    constexpr int   kStepsH = 8;
    constexpr float kPi     = std::numbers::pi_v<float>;

    for (int li = 0; li <= kStepsL; ++li)
    {
        const float L = static_cast<float>(li) / static_cast<float>(kStepsL);
        for (int ci = 0; ci <= kStepsC; ++ci)
        {
            const float C = static_cast<float>(ci) / static_cast<float>(kStepsC) * 0.4F;
            for (int hi = 0; hi < kStepsH; ++hi)
            {
                const float h = static_cast<float>(hi) / static_cast<float>(kStepsH)
                                * 2.0F * kPi;

                const w::Float3 rgb = w::oklch_to_rgb(L, C, h);

                EXPECT_GE(rgb.x, 0.0F) << "r<0  L=" << L << " C=" << C << " h=" << h;
                EXPECT_LE(rgb.x, 1.0F) << "r>1  L=" << L << " C=" << C << " h=" << h;
                EXPECT_GE(rgb.y, 0.0F) << "g<0  L=" << L << " C=" << C << " h=" << h;
                EXPECT_LE(rgb.y, 1.0F) << "g>1  L=" << L << " C=" << C << " h=" << h;
                EXPECT_GE(rgb.z, 0.0F) << "b<0  L=" << L << " C=" << C << " h=" << h;
                EXPECT_LE(rgb.z, 1.0F) << "b>1  L=" << L << " C=" << C << " h=" << h;
            }
        }
    }
}

// =============================================================================
// Draw smoke -- ColorPicker emits quads without font / GPU
// =============================================================================

TEST(UiColorPickerDraw, EmitsQuadsWithoutFont)
{
    w::ColorPicker cp;
    cp.set_rect(kPickerRect);
    cp.set_value(w::ColorF { 0.4F, 0.7F, 0.2F, 0.8F });
    EXPECT_TRUE(cp.set_from_hex("#AA3366"));

    r::DrawBatcher batcher;
    batcher.begin_frame();
    cp.draw(batcher, nullptr, w::Theme{});

    EXPECT_GT(batcher.vertex_count(), 0U);
    EXPECT_GT(batcher.index_count(),  0U);
    EXPECT_GT(batcher.command_count(), 0U);
}
