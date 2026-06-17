// =============================================================================
// CHROMODYNAMIC -- cd::platform IWindow header-logic tests (band4 singletons).
//
// These tests exercise the HEADER-ONLY portions of <cd/platform/Window.hpp>
// that the OS-gated Win32 tests cannot isolate: the inline
// `pump_all_windows` aggregate drain (skip-null, skip-closed, all-closed
// terminal) and the default `IWindow::set_parent` contract. They use a pure
// fake IWindow so they run identically on every host (no native handle, no
// message pump), closing the previously-untested branch coverage for the
// platform-tier inline logic. See ADR-20260616-band4-singletons-scope
// §platform.
// =============================================================================
#include <cd/platform/Window.hpp>

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace
{

// Minimal in-memory IWindow that never touches the OS. `closed_` drives both
// should_close() and pump_events() so the header aggregate can be tested for
// every branch deterministically.
class FakeWindow final : public cd::platform::IWindow
{
public:
    explicit FakeWindow(bool emit_event) noexcept : emit_event_ { emit_event } {}

    [[nodiscard]] bool pump_events(std::vector<cd::platform::OSEvent>& out) override
    {
        ++pump_calls_;
        if (closed_)
        {
            return false;
        }
        if (emit_event_)
        {
            out.push_back({ cd::platform::OSEventKind::kFocusGained });
        }
        return true;
    }

    [[nodiscard]] bool should_close() const noexcept override { return closed_; }
    void request_close() noexcept override { closed_ = true; }

    [[nodiscard]] void* native_window_handle() const noexcept override { return nullptr; }
    [[nodiscard]] void* native_display_handle() const noexcept override { return nullptr; }
    [[nodiscard]] std::uint32_t width() const noexcept override { return 0; }
    [[nodiscard]] std::uint32_t height() const noexcept override { return 0; }
    void set_title(std::string_view) override {}

    [[nodiscard]] int pump_calls() const noexcept { return pump_calls_; }

private:
    bool emit_event_ { false };
    bool closed_ { false };
    int  pump_calls_ { 0 };
};

// ---------------------------------------------------------------------------
// pump_all_windows skips already-closed windows WITHOUT calling pump_events
// on them (the `if (w->should_close()) continue;` branch), and still reports
// alive while a live peer remains.
// ---------------------------------------------------------------------------
TEST(PlatformWindowIface, PumpAllSkipsClosedWindowsButReportsLivePeer)
{
    FakeWindow live { /*emit_event=*/true };
    FakeWindow dead { /*emit_event=*/true };
    dead.request_close();

    std::array<cd::platform::IWindow*, 2> wins { &live, &dead };
    std::vector<cd::platform::OSEvent> events;

    const bool any_alive = cd::platform::pump_all_windows(
        std::span<cd::platform::IWindow* const> { wins.data(), wins.size() }, events);

    EXPECT_TRUE(any_alive);
    EXPECT_EQ(live.pump_calls(), 1);   // live window pumped
    EXPECT_EQ(dead.pump_calls(), 0);   // closed window short-circuited
    ASSERT_EQ(events.size(), 1U);      // only the live window emitted
    EXPECT_EQ(events.front().kind, cd::platform::OSEventKind::kFocusGained);
}

// ---------------------------------------------------------------------------
// When every window is closed, the aggregate returns false (terminal: host
// may exit its loop).
// ---------------------------------------------------------------------------
TEST(PlatformWindowIface, PumpAllReturnsFalseWhenEveryWindowClosed)
{
    FakeWindow a { false };
    FakeWindow b { false };
    a.request_close();
    b.request_close();

    std::array<cd::platform::IWindow*, 2> wins { &a, &b };
    std::vector<cd::platform::OSEvent> events;

    EXPECT_FALSE(cd::platform::pump_all_windows(
        std::span<cd::platform::IWindow* const> { wins.data(), wins.size() }, events));
    EXPECT_TRUE(events.empty());
}

// ---------------------------------------------------------------------------
// Empty span is a no-op terminal (false) — guards the loop-never-enters path.
// ---------------------------------------------------------------------------
TEST(PlatformWindowIface, PumpAllEmptySpanReturnsFalse)
{
    std::vector<cd::platform::OSEvent> events;
    EXPECT_FALSE(cd::platform::pump_all_windows(
        std::span<cd::platform::IWindow* const> {}, events));
}

// ---------------------------------------------------------------------------
// The base IWindow::set_parent contract: a backend that does not implement
// parenting (the default) returns false. FakeWindow does not override it.
// ---------------------------------------------------------------------------
TEST(PlatformWindowIface, DefaultSetParentReturnsFalse)
{
    FakeWindow parent { false };
    FakeWindow child { false };
    EXPECT_FALSE(child.set_parent(&parent));
    EXPECT_FALSE(child.set_parent(nullptr));
}

// ---------------------------------------------------------------------------
// platform_errors::make round-trips the domain + code into an ErrorCode
// (the kNotImplemented branch the no-backend factory returns on unsupported
// targets).
// ---------------------------------------------------------------------------
TEST(PlatformWindowIface, ErrorCodeMakeCarriesDomainAndCode)
{
    const auto ec = cd::platform::platform_errors::make(
        cd::platform::platform_errors::Code::kNotImplemented, "no backend");
    EXPECT_EQ(ec.domain, cd::platform::platform_errors::kDomain);
    EXPECT_EQ(ec.code,
              static_cast<std::uint32_t>(cd::platform::platform_errors::Code::kNotImplemented));
}

}  // namespace
