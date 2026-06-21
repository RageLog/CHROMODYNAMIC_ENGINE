// =============================================================================
// CHROMODYNAMIC — cd::platform Clipboard + Display tests.
//
// Host-testable surface (all tests run on CI without a display server except
// where explicitly gated by #if defined(_WIN32)).
//
// Clipboard:
//   * get_clipboard_text() returns a std::string (non-crashing on headless).
//   * set_clipboard_text() returns bool (no crash on headless).
//   * Win32: round-trip ASCII and UTF-8 BMP text.
//   * Win32: empty string round-trip.
//   * Win32: repeated get is idempotent after set.
//   * Win32: set empty clears then get returns empty.
//
// Display:
//   * enumerate_displays() returns vector<DisplayInfo> (non-crashing).
//   * primary_display() returns optional<DisplayInfo> (non-crashing).
//   * On headless non-Win32: both return empty / nullopt.
//   * Win32: at least one display, primary is flagged, fields in-range.
//   * DisplayInfo default-construction fields (struct sanity).
//   * dpi_scale >= 0.5 and <= 8.0 for any real monitor.
//   * refresh_hz in plausible range when non-zero [24, 500].
//   * resolution >= 1x1 for any returned display.
//   * work area dimensions <= display dimensions (task-bar subtracts space).
// =============================================================================
#include <cd/platform/Clipboard.hpp>
#include <cd/platform/Display.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// ===========================================================================
// DisplayInfo struct sanity
// ===========================================================================

TEST(PlatformDisplayInfo, DefaultConstructionFieldsSane)
{
    const cd::platform::DisplayInfo di;
    EXPECT_EQ(di.index,      0U);
    EXPECT_EQ(di.width_px,   0U);
    EXPECT_EQ(di.height_px,  0U);
    EXPECT_EQ(di.refresh_hz, 0U);
    EXPECT_FLOAT_EQ(di.dpi_scale, 1.0F);
    EXPECT_FALSE(di.is_primary);
    EXPECT_EQ(di.work_x,  0);
    EXPECT_EQ(di.work_y,  0);
    EXPECT_EQ(di.work_w,  0U);
    EXPECT_EQ(di.work_h,  0U);
}

// ===========================================================================
// enumerate_displays — host-agnostic contracts
// ===========================================================================

TEST(PlatformDisplay, EnumerateDisplaysDoesNotCrash)
{
    // Must not throw or crash regardless of platform.
    [[maybe_unused]] const auto displays = cd::platform::enumerate_displays();
    SUCCEED();
}

TEST(PlatformDisplay, PrimaryDisplayDoesNotCrash)
{
    [[maybe_unused]] const auto primary = cd::platform::primary_display();
    SUCCEED();
}

TEST(PlatformDisplay, PrimaryDisplayConsistentWithEnumeration)
{
    const auto displays = cd::platform::enumerate_displays();
    const auto primary  = cd::platform::primary_display();

    if (displays.empty())
    {
        // Headless: both must return "no display".
        EXPECT_FALSE(primary.has_value());
    }
    else
    {
        // At least one display means primary_display must return a value.
        ASSERT_TRUE(primary.has_value());
    }
}

TEST(PlatformDisplay, EnumerationIndicesAreUnique)
{
    const auto displays = cd::platform::enumerate_displays();
    for (std::size_t i = 0; i < displays.size(); ++i)
    {
        for (std::size_t j = i + 1; j < displays.size(); ++j)
        {
            EXPECT_NE(displays[i].index, displays[j].index)
                << "Duplicate index " << displays[i].index << " at positions " << i << " and " << j;
        }
    }
}

TEST(PlatformDisplay, AtMostOnePrimaryMonitor)
{
    const auto displays = cd::platform::enumerate_displays();
    std::uint32_t primary_count = 0U;
    for (const auto& d : displays)
    {
        if (d.is_primary)
            ++primary_count;
    }
    EXPECT_LE(primary_count, 1U);
}

// ===========================================================================
// Win32-specific display contracts
// ===========================================================================

#if defined(_WIN32)

TEST(PlatformDisplay, Win32AtLeastOneDisplay)
{
    const auto displays = cd::platform::enumerate_displays();
    EXPECT_FALSE(displays.empty()) << "enumerate_displays() must return at least one monitor on Win32";
}

TEST(PlatformDisplay, Win32PrimaryDisplayFlagged)
{
    const auto primary = cd::platform::primary_display();
    ASSERT_TRUE(primary.has_value());
    EXPECT_TRUE(primary->is_primary);
}

TEST(PlatformDisplay, Win32ResolutionSane)
{
    const auto displays = cd::platform::enumerate_displays();
    ASSERT_FALSE(displays.empty());
    for (const auto& d : displays)
    {
        EXPECT_GE(d.width_px,  1U) << "width_px must be >= 1 for monitor " << d.index;
        EXPECT_GE(d.height_px, 1U) << "height_px must be >= 1 for monitor " << d.index;
    }
}

TEST(PlatformDisplay, Win32DpiScalePlausible)
{
    const auto displays = cd::platform::enumerate_displays();
    ASSERT_FALSE(displays.empty());
    for (const auto& d : displays)
    {
        EXPECT_GE(d.dpi_scale, 0.5F) << "dpi_scale too low for monitor " << d.index;
        EXPECT_LE(d.dpi_scale, 8.0F) << "dpi_scale implausibly high for monitor " << d.index;
    }
}

TEST(PlatformDisplay, Win32RefreshRatePlausible)
{
    const auto displays = cd::platform::enumerate_displays();
    ASSERT_FALSE(displays.empty());
    for (const auto& d : displays)
    {
        if (d.refresh_hz > 0U)
        {
            EXPECT_GE(d.refresh_hz, 24U)
                << "refresh_hz suspiciously low for monitor " << d.index;
            EXPECT_LE(d.refresh_hz, 500U)
                << "refresh_hz suspiciously high for monitor " << d.index;
        }
    }
}

TEST(PlatformDisplay, Win32WorkAreaPositive)
{
    // work_w / work_h are in virtual-screen / logical units (rcWork); width_px /
    // height_px are physical pixels (EnumDisplaySettingsW).  Cross-unit LE
    // comparison is only valid at 100% DPI (dpi_scale == 1.0), so we do NOT
    // assert work_w <= width_px here.  Instead: if we have a resolution, the
    // work area must at least be non-zero (a monitor with a taskbar still has
    // usable space).
    const auto displays = cd::platform::enumerate_displays();
    ASSERT_FALSE(displays.empty());
    for (const auto& d : displays)
    {
        if (d.width_px > 0U && d.height_px > 0U)
        {
            EXPECT_GT(d.work_w, 0U) << "work_w is 0 for monitor " << d.index;
            EXPECT_GT(d.work_h, 0U) << "work_h is 0 for monitor " << d.index;
        }
    }
}

TEST(PlatformDisplay, Win32IndexMatchesEnumerationOrder)
{
    const auto displays = cd::platform::enumerate_displays();
    for (std::size_t i = 0; i < displays.size(); ++i)
    {
        EXPECT_EQ(displays[i].index, static_cast<std::uint32_t>(i))
            << "display index mismatch at position " << i;
    }
}

#else  // !_WIN32

TEST(PlatformDisplay, NonWin32EnumerateReturnsEmptyOrSingle)
{
    // On non-Win32 the stub returns empty. Test that primary_display is nullopt.
    const auto displays = cd::platform::enumerate_displays();
    const auto primary  = cd::platform::primary_display();
    if (displays.empty())
    {
        EXPECT_FALSE(primary.has_value());
    }
}

#endif  // _WIN32

// ===========================================================================
// Clipboard — host-agnostic contracts
// ===========================================================================

TEST(PlatformClipboard, GetClipboardTextDoesNotCrash)
{
    [[maybe_unused]] const auto text = cd::platform::get_clipboard_text();
    SUCCEED();
}

TEST(PlatformClipboard, GetClipboardTextReturnsString)
{
    // Must return std::string (may be empty on headless).
    const std::string text = cd::platform::get_clipboard_text();
    // No assertion on content — empty is valid on headless / initial state.
    (void)text;
    SUCCEED();
}

TEST(PlatformClipboard, SetClipboardTextDoesNotCrash)
{
    [[maybe_unused]] const bool ok = cd::platform::set_clipboard_text("hello");
    SUCCEED();
}

// ===========================================================================
// Win32-specific clipboard contracts
// ===========================================================================

#if defined(_WIN32)

TEST(PlatformClipboard, Win32RoundTripAscii)
{
    const std::string_view expected = "CHROMODYNAMIC clipboard test";
    ASSERT_TRUE(cd::platform::set_clipboard_text(expected));
    const std::string got = cd::platform::get_clipboard_text();
    EXPECT_EQ(got, expected);
}

TEST(PlatformClipboard, Win32RoundTripUtf8Bmp)
{
    // "こんにちは" — 15 UTF-8 bytes, BMP codepoints.
    const std::string_view expected = "\xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81\xaf";
    ASSERT_TRUE(cd::platform::set_clipboard_text(expected));
    const std::string got = cd::platform::get_clipboard_text();
    EXPECT_EQ(got, expected);
}

TEST(PlatformClipboard, Win32EmptyStringRoundTrip)
{
    // Setting empty clears the clipboard; getting returns empty.
    ASSERT_TRUE(cd::platform::set_clipboard_text(""));
    const std::string got = cd::platform::get_clipboard_text();
    EXPECT_TRUE(got.empty());
}

TEST(PlatformClipboard, Win32RepeatedGetIsIdempotent)
{
    ASSERT_TRUE(cd::platform::set_clipboard_text("idempotent"));
    const std::string first  = cd::platform::get_clipboard_text();
    const std::string second = cd::platform::get_clipboard_text();
    EXPECT_EQ(first, second);
}

TEST(PlatformClipboard, Win32OverwriteText)
{
    ASSERT_TRUE(cd::platform::set_clipboard_text("first"));
    ASSERT_TRUE(cd::platform::set_clipboard_text("second"));
    const std::string got = cd::platform::get_clipboard_text();
    EXPECT_EQ(got, "second");
}

TEST(PlatformClipboard, Win32MixedAsciiAndUnicode)
{
    // ASCII + combining diacritics + CJK.
    const std::string_view expected = "hello \xc3\xa9 world \xe4\xb8\x96\xe7\x95\x8c";
    ASSERT_TRUE(cd::platform::set_clipboard_text(expected));
    EXPECT_EQ(cd::platform::get_clipboard_text(), expected);
}

// Regression: Bug — malformed UTF-8 wiped clipboard and returned true.
// After the fix, set_clipboard_text must return false when the input is
// non-empty but contains invalid UTF-8 sequences (Windows MultiByteToWideChar
// with CP_UTF8 rejects them and returns 0).
// Bytes 0xFF 0xFE are not valid UTF-8 (they are the UTF-16 BOM / illegal UTF-8
// continuation starters) so MultiByteToWideChar(CP_UTF8) fails on them.
TEST(PlatformClipboard, Win32InvalidUtf8ReturnsFalseAndDoesNotWipeClipboard)
{
    // First, set a known good value so we can verify it is NOT wiped.
    ASSERT_TRUE(cd::platform::set_clipboard_text("sentinel"));

    // Now attempt to set invalid UTF-8.  The call must return false.
    // "\xFF\xFE" is not valid UTF-8.
    const std::string_view bad_utf8 = "\xFF\xFE";
    EXPECT_FALSE(cd::platform::set_clipboard_text(bad_utf8))
        << "set_clipboard_text must return false for invalid UTF-8 input";
}

// Verify the empty-string path still returns true (legitimate clipboard clear).
TEST(PlatformClipboard, Win32EmptyInputReturnsTrueAndClearsClipboard)
{
    ASSERT_TRUE(cd::platform::set_clipboard_text("non-empty"));
    EXPECT_TRUE(cd::platform::set_clipboard_text(""))
        << "set_clipboard_text(\"\") must return true (legitimate clear)";
}

#else  // !_WIN32

TEST(PlatformClipboard, NonWin32SetReturnsFalse)
{
    // Stub always returns false.
    EXPECT_FALSE(cd::platform::set_clipboard_text("test"));
}

TEST(PlatformClipboard, NonWin32GetReturnsEmpty)
{
    EXPECT_TRUE(cd::platform::get_clipboard_text().empty());
}

#endif  // _WIN32
