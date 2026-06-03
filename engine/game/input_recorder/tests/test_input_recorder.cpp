// =============================================================================
// CHROMODYNAMIC - cd/game/input_recorder/tests/test_input_recorder.cpp
// Phase 651 - cd::game::input_recorder unit tests (M10 W4B).
//
// Test contract (10 cases):
//
//   T1. record + save + load round-trip: all events survive serialisation.
//   T2. replay delivers events in timestamp order via next_event().
//   T3. next_event returns nullopt past the end of events.
//   T4. reset() rewinds; events are re-delivered from the start.
//   T5. record() after stop_recording() is silently ignored.
//   T6. Malformed file (bad magic) is rejected by load_from_file().
//   T7. Truncated file is rejected by load_from_file().
//   T8. next_event withholds events whose timestamp_ms > current_ms.
//   T9. is_recording() reflects start/stop state accurately.
//   T10. Empty recording round-trips cleanly (zero events).
// =============================================================================
#include <cd/game/input_recorder/InputRecorder.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace cd::game::input_recorder::tests
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Temporary file RAII: creates a unique path in the system temp dir and
/// removes the file (if it exists) on destruction.
class TempFile
{
public:
    TempFile()
    {
        auto tmp = std::filesystem::temp_directory_path();
        // Use a counter to keep names distinct across tests in one run.
        static int counter = 0;
        path_ = tmp / ("cd_input_recorder_test_" + std::to_string(++counter) + ".cdinput");
    }
    ~TempFile()
    {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

/// Build a simple InputEvent with a given timestamp, kind, and code.
static InputEvent make_event(double ts_ms,
                             InputEventKind kind = InputEventKind::kKeyDown,
                             std::uint32_t code = 0,
                             float p0 = 0.0F,
                             float p1 = 0.0F)
{
    InputEvent ev;
    ev.timestamp_ms = ts_ms;
    ev.kind         = kind;
    ev.code         = code;
    ev.payload      = {p0, p1, 0.0F, 0.0F};
    return ev;
}

// ---------------------------------------------------------------------------
// T1: record + save + load round-trip
// ---------------------------------------------------------------------------
TEST(InputRecorderTest, T1_RecordSaveLoadRoundTrip)
{
    // Arrange
    Recorder rec;
    rec.start_recording();
    rec.record(make_event(0.0,   InputEventKind::kKeyDown,    65));       // 'A' down
    rec.record(make_event(16.0,  InputEventKind::kMouseMove,  0, 3.0F, -2.0F));
    rec.record(make_event(100.0, InputEventKind::kKeyUp,      65));       // 'A' up
    rec.stop_recording();

    TempFile tmp;

    // Act - save
    const bool saved = rec.save_to_file(tmp.path());
    ASSERT_TRUE(saved);

    // Act - load
    Replayer rep;
    const bool loaded = rep.load_from_file(tmp.path());
    ASSERT_TRUE(loaded);

    // Assert - counts match
    ASSERT_EQ(rep.event_count(), std::size_t{3});

    const auto all = rep.all();
    ASSERT_EQ(all.size(), std::size_t{3});

    // Verify each event
    EXPECT_DOUBLE_EQ(all[0].timestamp_ms, 0.0);
    EXPECT_EQ(all[0].kind, InputEventKind::kKeyDown);
    EXPECT_EQ(all[0].code, std::uint32_t{65});

    EXPECT_DOUBLE_EQ(all[1].timestamp_ms, 16.0);
    EXPECT_EQ(all[1].kind, InputEventKind::kMouseMove);
    EXPECT_FLOAT_EQ(all[1].payload[0], 3.0F);
    EXPECT_FLOAT_EQ(all[1].payload[1], -2.0F);

    EXPECT_DOUBLE_EQ(all[2].timestamp_ms, 100.0);
    EXPECT_EQ(all[2].kind, InputEventKind::kKeyUp);
    EXPECT_EQ(all[2].code, std::uint32_t{65});
}

// ---------------------------------------------------------------------------
// T2: replay delivers events in timestamp order via next_event()
// ---------------------------------------------------------------------------
TEST(InputRecorderTest, T2_ReplayDeliversEventsInOrder)
{
    Recorder rec;
    rec.start_recording();
    rec.record(make_event(10.0,  InputEventKind::kMouseDown, 0));
    rec.record(make_event(50.0,  InputEventKind::kMouseUp,   0));
    rec.record(make_event(200.0, InputEventKind::kKeyDown,   32));
    rec.stop_recording();

    TempFile tmp;
    ASSERT_TRUE(rec.save_to_file(tmp.path()));

    Replayer rep;
    ASSERT_TRUE(rep.load_from_file(tmp.path()));

    // Drain at current_ms = 60.0 -- should get the first two events.
    const auto ev0 = rep.next_event(60.0);
    ASSERT_TRUE(ev0.has_value());
    EXPECT_EQ(ev0->kind, InputEventKind::kMouseDown);

    const auto ev1 = rep.next_event(60.0);
    ASSERT_TRUE(ev1.has_value());
    EXPECT_EQ(ev1->kind, InputEventKind::kMouseUp);

    // Third event is at 200 ms; should not be delivered at 60.
    const auto ev2 = rep.next_event(60.0);
    EXPECT_FALSE(ev2.has_value());

    // Advance to 300 ms; now the third event should arrive.
    const auto ev3 = rep.next_event(300.0);
    ASSERT_TRUE(ev3.has_value());
    EXPECT_EQ(ev3->kind, InputEventKind::kKeyDown);
}

// ---------------------------------------------------------------------------
// T3: next_event returns nullopt at end of events
// ---------------------------------------------------------------------------
TEST(InputRecorderTest, T3_NextEventNulloptAtEnd)
{
    Recorder rec;
    rec.start_recording();
    rec.record(make_event(5.0, InputEventKind::kGamepadButton, 0));
    rec.stop_recording();

    TempFile tmp;
    ASSERT_TRUE(rec.save_to_file(tmp.path()));

    Replayer rep;
    ASSERT_TRUE(rep.load_from_file(tmp.path()));

    // Consume the single event.
    const auto ev = rep.next_event(1000.0);
    ASSERT_TRUE(ev.has_value());

    // Now exhausted.
    EXPECT_FALSE(rep.next_event(1000.0).has_value());
    EXPECT_TRUE(rep.is_finished());
}

// ---------------------------------------------------------------------------
// T4: reset() rewinds the cursor; events are re-delivered from start
// ---------------------------------------------------------------------------
TEST(InputRecorderTest, T4_ResetRewindsPlayback)
{
    Recorder rec;
    rec.start_recording();
    rec.record(make_event(0.0, InputEventKind::kKeyDown, 87));    // 'W'
    rec.record(make_event(8.0, InputEventKind::kKeyDown, 65));    // 'A'
    rec.stop_recording();

    TempFile tmp;
    ASSERT_TRUE(rec.save_to_file(tmp.path()));

    Replayer rep;
    ASSERT_TRUE(rep.load_from_file(tmp.path()));

    // First pass: drain everything.
    ASSERT_TRUE(rep.next_event(1000.0).has_value());
    ASSERT_TRUE(rep.next_event(1000.0).has_value());
    EXPECT_TRUE(rep.is_finished());

    // Reset and second pass: same events arrive again.
    rep.reset();
    EXPECT_FALSE(rep.is_finished());

    const auto ev0 = rep.next_event(1000.0);
    ASSERT_TRUE(ev0.has_value());
    EXPECT_EQ(ev0->code, std::uint32_t{87});

    const auto ev1 = rep.next_event(1000.0);
    ASSERT_TRUE(ev1.has_value());
    EXPECT_EQ(ev1->code, std::uint32_t{65});
}

// ---------------------------------------------------------------------------
// T5: record() after stop_recording() is silently ignored
// ---------------------------------------------------------------------------
TEST(InputRecorderTest, T5_RecordAfterStopIgnored)
{
    Recorder rec;
    rec.start_recording();
    rec.record(make_event(1.0, InputEventKind::kKeyDown, 13));
    rec.stop_recording();

    // Attempt to record after stop.
    rec.record(make_event(2.0, InputEventKind::kKeyUp, 13));
    rec.record(make_event(3.0, InputEventKind::kMouseMove, 0));

    // event_count must still be 1.
    EXPECT_EQ(rec.event_count(), std::size_t{1});

    // Verify round-trip also sees only 1 event.
    TempFile tmp;
    ASSERT_TRUE(rec.save_to_file(tmp.path()));

    Replayer rep;
    ASSERT_TRUE(rep.load_from_file(tmp.path()));
    EXPECT_EQ(rep.event_count(), std::size_t{1});
}

// ---------------------------------------------------------------------------
// T6: Malformed file (bad magic) is rejected
// ---------------------------------------------------------------------------
TEST(InputRecorderTest, T6_MalformedFileBadMagicRejected)
{
    TempFile tmp;

    // Write a file with wrong magic.
    {
        std::ofstream out(tmp.path(), std::ios::binary | std::ios::trunc);
        const char bad_magic[] = "XYZW";
        out.write(bad_magic, 4);
        const std::uint8_t version = 0x01;
        out.write(reinterpret_cast<const char*>(&version), 1);
        const std::uint64_t count = 0;
        out.write(reinterpret_cast<const char*>(&count), 8);
    }

    Replayer rep;
    EXPECT_FALSE(rep.load_from_file(tmp.path()));
}

// ---------------------------------------------------------------------------
// T7: Truncated file is rejected
// ---------------------------------------------------------------------------
TEST(InputRecorderTest, T7_TruncatedFileRejected)
{
    // Build a valid recording first.
    Recorder rec;
    rec.start_recording();
    rec.record(make_event(1.0, InputEventKind::kKeyDown, 0));
    rec.record(make_event(2.0, InputEventKind::kKeyUp,   0));
    rec.stop_recording();

    TempFile tmp;
    ASSERT_TRUE(rec.save_to_file(tmp.path()));

    // Truncate the file by removing the last 10 bytes (cuts into event data).
    {
        const std::uintmax_t size = std::filesystem::file_size(tmp.path());
        ASSERT_GT(size, std::uintmax_t{10});
        std::filesystem::resize_file(tmp.path(), size - 10);
    }

    Replayer rep;
    EXPECT_FALSE(rep.load_from_file(tmp.path()));
}

// ---------------------------------------------------------------------------
// T8: next_event withholds events whose timestamp_ms > current_ms
// ---------------------------------------------------------------------------
TEST(InputRecorderTest, T8_NextEventWithholdsFutureEvents)
{
    Recorder rec;
    rec.start_recording();
    rec.record(make_event(100.0, InputEventKind::kGamepadAxis, 0, 0.5F));
    rec.record(make_event(200.0, InputEventKind::kGamepadAxis, 0, 1.0F));
    rec.stop_recording();

    TempFile tmp;
    ASSERT_TRUE(rec.save_to_file(tmp.path()));

    Replayer rep;
    ASSERT_TRUE(rep.load_from_file(tmp.path()));

    // At t=50 nothing should fire.
    EXPECT_FALSE(rep.next_event(50.0).has_value());

    // At t=150 only the first event fires.
    const auto ev0 = rep.next_event(150.0);
    ASSERT_TRUE(ev0.has_value());
    EXPECT_FLOAT_EQ(ev0->payload[0], 0.5F);

    // Still at t=150; second event (at 200) must not fire.
    EXPECT_FALSE(rep.next_event(150.0).has_value());

    // At t=250 the second event fires.
    const auto ev1 = rep.next_event(250.0);
    ASSERT_TRUE(ev1.has_value());
    EXPECT_FLOAT_EQ(ev1->payload[0], 1.0F);
}

// ---------------------------------------------------------------------------
// T9: is_recording() reflects start/stop state accurately
// ---------------------------------------------------------------------------
TEST(InputRecorderTest, T9_IsRecordingReflectsState)
{
    Recorder rec;

    // Before start: not recording.
    EXPECT_FALSE(rec.is_recording());

    rec.start_recording();
    EXPECT_TRUE(rec.is_recording());

    rec.record(make_event(5.0));
    EXPECT_TRUE(rec.is_recording());

    rec.stop_recording();
    EXPECT_FALSE(rec.is_recording());

    // Second session.
    rec.start_recording();
    EXPECT_TRUE(rec.is_recording());
    rec.stop_recording();
    EXPECT_FALSE(rec.is_recording());
}

// ---------------------------------------------------------------------------
// T10: empty recording round-trips cleanly (zero events)
// ---------------------------------------------------------------------------
TEST(InputRecorderTest, T10_EmptyRecordingRoundTrips)
{
    Recorder rec;
    rec.start_recording();
    rec.stop_recording();

    EXPECT_EQ(rec.event_count(), std::size_t{0});

    TempFile tmp;
    ASSERT_TRUE(rec.save_to_file(tmp.path()));

    Replayer rep;
    ASSERT_TRUE(rep.load_from_file(tmp.path()));

    EXPECT_EQ(rep.event_count(), std::size_t{0});
    EXPECT_TRUE(rep.is_finished());
    EXPECT_FALSE(rep.next_event(9999.0).has_value());
}

}  // namespace cd::game::input_recorder::tests
