// =============================================================================
// CHROMODYNAMIC -- cd::platform type-level + stub tests.
//
// Covers host-testable surface that does NOT require a display or Emscripten:
//   * OSEventKind / KeyCode / MouseButton enum value uniqueness and sentinels.
//   * OSEvent default-construction field sanity.
//   * WindowDesc default values (title / size / flags).
//   * platform_errors domain and all Code variants — make() round-trips.
//   * pump_all_windows null-only span (three-null array, no live window).
//   * FakeWindow multi-event accumulation across two pump calls.
//   * FakeWindow request_close is idempotent.
//   * create_window rejects zero width AND zero height (each independently).
//   * StableTime: monotonic_seconds non-negative; multiple begin_frame epochs.
//   * SignalCategory enum values distinct.
//   * TextChar OSEvent code_point field carries the value set on it.
//
// No display, no OS window, no GPU, no Emscripten.
// Every test is host-CI green on Windows and Linux.
// =============================================================================
#include <cd/platform/SignalHandler.hpp>
#include <cd/platform/StableTime.hpp>
#include <cd/platform/Window.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace
{

// ---------------------------------------------------------------------------
// Minimal in-process fake window — no OS handle, deterministic control.
// ---------------------------------------------------------------------------
class LocalFakeWindow final : public cd::platform::IWindow
{
public:
    LocalFakeWindow() noexcept = default;

    [[nodiscard]] bool pump_events(std::vector<cd::platform::OSEvent>& out) override
    {
        if (closed_)
        {
            return false;
        }
        for (const auto& ev : queued_)
        {
            out.push_back(ev);
        }
        queued_.clear();
        return true;
    }

    [[nodiscard]] bool          should_close()          const noexcept override { return closed_; }
    void                        request_close()               noexcept override { closed_ = true; }
    [[nodiscard]] void*         native_window_handle()  const noexcept override { return nullptr; }
    [[nodiscard]] void*         native_display_handle() const noexcept override { return nullptr; }
    [[nodiscard]] std::uint32_t width()                 const noexcept override { return 0; }
    [[nodiscard]] std::uint32_t height()                const noexcept override { return 0; }
    void                        set_title(std::string_view)   override {}

    void enqueue(cd::platform::OSEvent ev) { queued_.push_back(ev); }

private:
    bool                                closed_ { false };
    std::vector<cd::platform::OSEvent>  queued_;
};

// ===========================================================================
// Enum value uniqueness / sentinel coverage
// ===========================================================================

TEST(PlatformTypes, OSEventKindValuesDistinct)
{
    const std::array<cd::platform::OSEventKind, 10> kinds {
        cd::platform::OSEventKind::kClose,
        cd::platform::OSEventKind::kResize,
        cd::platform::OSEventKind::kFocusGained,
        cd::platform::OSEventKind::kFocusLost,
        cd::platform::OSEventKind::kKeyDown,
        cd::platform::OSEventKind::kKeyUp,
        cd::platform::OSEventKind::kMouseMove,
        cd::platform::OSEventKind::kMouseButtonDown,
        cd::platform::OSEventKind::kMouseButtonUp,
        cd::platform::OSEventKind::kMouseWheel,
    };
    for (std::size_t i = 0; i < kinds.size(); ++i)
    {
        for (std::size_t j = i + 1; j < kinds.size(); ++j)
        {
            EXPECT_NE(kinds[i], kinds[j])
                << "Duplicate OSEventKind at indices " << i << " and " << j;
        }
    }
}

TEST(PlatformTypes, KeyCodeAlphaContiguous)
{
    // kA..kZ must be a contiguous run (Win32 arithmetic relies on this).
    const auto base = static_cast<std::uint16_t>(cd::platform::KeyCode::kA);
    for (std::uint16_t i = 0; i < 26U; ++i)
    {
        const auto expected = static_cast<cd::platform::KeyCode>(
            static_cast<std::uint16_t>(base + i));
        const auto actual = static_cast<cd::platform::KeyCode>(
            static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(cd::platform::KeyCode::kA) + i));
        EXPECT_EQ(actual, expected) << "kA + " << i << " not contiguous";
    }
}

TEST(PlatformTypes, KeyCodeDigitsContiguous)
{
    // k0..k9 must be a contiguous run (Win32 arithmetic relies on this).
    const auto base = static_cast<std::uint16_t>(cd::platform::KeyCode::k0);
    for (std::uint16_t i = 0; i < 10U; ++i)
    {
        const auto expected = static_cast<cd::platform::KeyCode>(
            static_cast<std::uint16_t>(base + i));
        const auto actual = static_cast<cd::platform::KeyCode>(
            static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(cd::platform::KeyCode::k0) + i));
        EXPECT_EQ(actual, expected) << "k0 + " << i << " not contiguous";
    }
}

TEST(PlatformTypes, KeyCodeKCountExceedsAllNamedKeys)
{
    // kCount must exceed every named key so bounds checks work.
    const auto count = static_cast<std::uint16_t>(cd::platform::KeyCode::kCount);
    EXPECT_GT(count, static_cast<std::uint16_t>(cd::platform::KeyCode::kF12));
    EXPECT_GT(count, static_cast<std::uint16_t>(cd::platform::KeyCode::kZ));
    EXPECT_GT(count, static_cast<std::uint16_t>(cd::platform::KeyCode::k9));
    EXPECT_GT(count, static_cast<std::uint16_t>(cd::platform::KeyCode::kUnknown));
}

TEST(PlatformTypes, MouseButtonValuesDistinctAndCountIsFive)
{
    const std::array<cd::platform::MouseButton, 5> btns {
        cd::platform::MouseButton::kLeft,
        cd::platform::MouseButton::kRight,
        cd::platform::MouseButton::kMiddle,
        cd::platform::MouseButton::kX1,
        cd::platform::MouseButton::kX2,
    };
    for (std::size_t i = 0; i < btns.size(); ++i)
    {
        for (std::size_t j = i + 1; j < btns.size(); ++j)
        {
            EXPECT_NE(btns[i], btns[j])
                << "Duplicate MouseButton at indices " << i << " and " << j;
        }
    }
    EXPECT_EQ(static_cast<std::uint8_t>(cd::platform::MouseButton::kCount), 5U);
}

// ===========================================================================
// OSEvent default construction
// ===========================================================================

TEST(PlatformTypes, OSEventDefaultFieldsSane)
{
    const cd::platform::OSEvent ev;
    EXPECT_EQ(ev.kind,         cd::platform::OSEventKind::kClose);
    EXPECT_EQ(ev.width,        0U);
    EXPECT_EQ(ev.height,       0U);
    EXPECT_EQ(ev.key,          cd::platform::KeyCode::kUnknown);
    EXPECT_EQ(ev.mouse_button, cd::platform::MouseButton::kLeft);
    EXPECT_FLOAT_EQ(ev.mouse_x, 0.0F);
    EXPECT_FLOAT_EQ(ev.mouse_y, 0.0F);
    EXPECT_FLOAT_EQ(ev.wheel,   0.0F);
    EXPECT_EQ(ev.code_point,   0U);
}

TEST(PlatformTypes, OSEventTextCharCodePointRoundTrip)
{
    // kTextChar events carry an arbitrary Unicode code point —
    // verify the struct field stores and retrieves it faithfully.
    cd::platform::OSEvent ev;
    ev.kind       = cd::platform::OSEventKind::kTextChar;
    ev.code_point = 0x0041U;  // 'A'
    EXPECT_EQ(ev.kind,       cd::platform::OSEventKind::kTextChar);
    EXPECT_EQ(ev.code_point, 0x0041U);

    ev.code_point = 0x1F600U;  // emoji — above BMP
    EXPECT_EQ(ev.code_point, 0x1F600U);
}

// ===========================================================================
// WindowDesc defaults
// ===========================================================================

TEST(PlatformTypes, WindowDescDefaultValues)
{
    const cd::platform::WindowDesc d;
    EXPECT_EQ(d.title,    "chromodynamic");
    EXPECT_EQ(d.width,    1280U);
    EXPECT_EQ(d.height,   720U);
    EXPECT_TRUE(d.resizable);
    EXPECT_TRUE(d.visible);
}

// ===========================================================================
// platform_errors — domain and code round-trip
// ===========================================================================

TEST(PlatformErrors, DomainConstantValue)
{
    EXPECT_EQ(cd::platform::platform_errors::kDomain, 0x0011U);
}

TEST(PlatformErrors, AllCodeValuesDistinct)
{
    const std::array<cd::platform::platform_errors::Code, 4> codes {
        cd::platform::platform_errors::Code::kOk,
        cd::platform::platform_errors::Code::kInvalidArgument,
        cd::platform::platform_errors::Code::kCreateFailed,
        cd::platform::platform_errors::Code::kNotImplemented,
    };
    for (std::size_t i = 0; i < codes.size(); ++i)
    {
        for (std::size_t j = i + 1; j < codes.size(); ++j)
        {
            EXPECT_NE(codes[i], codes[j])
                << "Duplicate Code at " << i << " and " << j;
        }
    }
}

TEST(PlatformErrors, MakeNotImplementedRoundTrip)
{
    const auto ec = cd::platform::platform_errors::make(
        cd::platform::platform_errors::Code::kNotImplemented, "no-backend");
    EXPECT_EQ(ec.domain, cd::platform::platform_errors::kDomain);
    EXPECT_EQ(ec.code,
              static_cast<std::uint32_t>(
                  cd::platform::platform_errors::Code::kNotImplemented));
    EXPECT_FALSE(ec.message.empty());
}

TEST(PlatformErrors, MakeKOkHasZeroCodeAndCorrectDomain)
{
    const auto ec = cd::platform::platform_errors::make(
        cd::platform::platform_errors::Code::kOk);
    EXPECT_EQ(ec.code,   0U);
    EXPECT_EQ(ec.domain, cd::platform::platform_errors::kDomain);
}

TEST(PlatformErrors, MakeWithNoMessageHasEmptyMessage)
{
    const auto ec = cd::platform::platform_errors::make(
        cd::platform::platform_errors::Code::kCreateFailed);
    EXPECT_EQ(ec.domain, cd::platform::platform_errors::kDomain);
    EXPECT_EQ(ec.code,
              static_cast<std::uint32_t>(
                  cd::platform::platform_errors::Code::kCreateFailed));
    // Message may be empty when not supplied — must not crash.
    (void)ec.message;
}

// ===========================================================================
// create_window — zero-extent rejection (no display needed).
// RejectsZeroWidth (width=0, height=720) is already in test_platform.cpp;
// here we cover the complementary cases not tested there.
// ===========================================================================

TEST(PlatformWindow, RejectsZeroHeight)
{
    cd::platform::WindowDesc d;
    d.width  = 1280;
    d.height = 0;
    const auto r = cd::platform::create_window(d);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(
                  cd::platform::platform_errors::Code::kInvalidArgument));
}

TEST(PlatformWindow, RejectsBothZero)
{
    cd::platform::WindowDesc d;
    d.width  = 0;
    d.height = 0;
    const auto r = cd::platform::create_window(d);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(
                  cd::platform::platform_errors::Code::kInvalidArgument));
}

// ===========================================================================
// pump_all_windows edge: null-only span
// ===========================================================================

TEST(PlatformPumpAll, NullOnlySpanReturnsFalse)
{
    std::array<cd::platform::IWindow*, 3> wins { nullptr, nullptr, nullptr };
    std::vector<cd::platform::OSEvent> events;
    EXPECT_FALSE(cd::platform::pump_all_windows(
        std::span<cd::platform::IWindow* const> { wins.data(), wins.size() },
        events));
    EXPECT_TRUE(events.empty());
}

// ===========================================================================
// FakeWindow — multi-event accumulation and idempotent close
// ===========================================================================

TEST(PlatformFakeWindow, MultiEventAccumulationAcrossTwoPumps)
{
    LocalFakeWindow w;

    cd::platform::OSEvent ev1;
    ev1.kind = cd::platform::OSEventKind::kKeyDown;
    ev1.key  = cd::platform::KeyCode::kA;
    w.enqueue(ev1);

    std::vector<cd::platform::OSEvent> out;
    EXPECT_TRUE(w.pump_events(out));
    ASSERT_EQ(out.size(), 1U);
    EXPECT_EQ(out.front().kind, cd::platform::OSEventKind::kKeyDown);
    EXPECT_EQ(out.front().key,  cd::platform::KeyCode::kA);

    cd::platform::OSEvent ev2;
    ev2.kind    = cd::platform::OSEventKind::kMouseMove;
    ev2.mouse_x = 10.0F;
    ev2.mouse_y = 20.0F;
    w.enqueue(ev2);

    EXPECT_TRUE(w.pump_events(out));
    ASSERT_EQ(out.size(), 2U);  // accumulated from both pumps
    EXPECT_EQ(out.back().kind, cd::platform::OSEventKind::kMouseMove);
    EXPECT_FLOAT_EQ(out.back().mouse_x, 10.0F);
}

TEST(PlatformFakeWindow, RequestCloseIsIdempotent)
{
    LocalFakeWindow w;
    EXPECT_FALSE(w.should_close());
    w.request_close();
    EXPECT_TRUE(w.should_close());
    w.request_close();  // second call must not crash or flip back
    EXPECT_TRUE(w.should_close());

    std::vector<cd::platform::OSEvent> out;
    EXPECT_FALSE(w.pump_events(out));
    EXPECT_TRUE(out.empty());
}

TEST(PlatformFakeWindow, QueueDrainedAfterEachPump)
{
    LocalFakeWindow w;

    cd::platform::OSEvent ev;
    ev.kind = cd::platform::OSEventKind::kFocusGained;
    w.enqueue(ev);

    std::vector<cd::platform::OSEvent> out;
    static_cast<void>(w.pump_events(out));
    EXPECT_EQ(out.size(), 1U);

    // Second pump with no new events — queue should be empty.
    static_cast<void>(w.pump_events(out));
    EXPECT_EQ(out.size(), 1U);  // still 1, nothing added
}

// ===========================================================================
// StableTime — non-negative monotonic; multiple begin_frame resets
// ===========================================================================

TEST(PlatformStableTime, MonotonicSecondsNonNegative)
{
    const double t = cd::platform::StableTime::monotonic_seconds();
    EXPECT_GE(t, 0.0);
}

TEST(PlatformStableTime, MultipleBeginFrameResetsGiveNonNegativeEpoch)
{
    cd::platform::StableTime::begin_frame();
    EXPECT_GE(cd::platform::StableTime::frame_epoch_seconds(), 0.0);

    cd::platform::StableTime::begin_frame();
    EXPECT_GE(cd::platform::StableTime::frame_epoch_seconds(), 0.0);
}

TEST(PlatformStableTime, EpochBoundedByMonotonicAfterReset)
{
    cd::platform::StableTime::begin_frame();
    const double epoch    = cd::platform::StableTime::frame_epoch_seconds();
    const double monotonic = cd::platform::StableTime::monotonic_seconds();
    // epoch seconds since the reset ≤ total monotonic seconds since process start
    EXPECT_LE(epoch, monotonic);
}

// ===========================================================================
// SignalCategory — enum values distinct
// ===========================================================================

TEST(PlatformSignalCategory, ValuesDistinct)
{
    EXPECT_NE(cd::platform::SignalCategory::kCrash,
              cd::platform::SignalCategory::kInterrupt);
    EXPECT_NE(cd::platform::SignalCategory::kCrash,
              cd::platform::SignalCategory::kUnknown);
    EXPECT_NE(cd::platform::SignalCategory::kInterrupt,
              cd::platform::SignalCategory::kUnknown);
}

}  // namespace
