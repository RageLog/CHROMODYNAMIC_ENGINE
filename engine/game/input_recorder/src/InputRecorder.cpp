// =============================================================================
// CHROMODYNAMIC - cd/game/input_recorder/InputRecorder.cpp
// Phase 651 - cd::game::input_recorder (M10 W4B: HL2-style input record/replay)
//
// Implementation notes:
//
// BINARY FORMAT (version 1, little-endian):
//   Header (13 bytes):
//     [0..3]  magic    : "CDIR"  (4 × char, not null-terminated)
//     [4]     version  : 0x01
//     [5..12] count    : uint64_t, number of event records that follow
//   Body (count × 32 bytes):
//     Each InputEventRecord:
//       [0..7]   timestamp_ms : double   (IEEE 754 binary64)
//       [8..11]  kind         : uint32_t (InputEventKind underlying value)
//       [12..15] code         : uint32_t
//       [16..31] payload      : 4 × float
//
// All multi-byte fields are written in native byte order (little-endian on
// x86/x86-64/ARM64 LE). A future version flag can trigger byte-swapping on
// big-endian hosts; v1 targets the dominant little-endian platforms.
//
// Recorder:
//   * start_recording() clears events_ and sets active_ = true.
//   * record() appends only when active_; silent no-op otherwise.
//   * stop_recording() sets active_ = false; events_ is kept intact.
//   * save_to_file() opens a binary file in truncate mode, writes header
//     then body. On any error the file is closed (partially written files
//     are possible but the caller can detect via the false return value).
//
// Replayer:
//   * load_from_file() validates magic, version, and reads exactly
//     header.count records. Any size mismatch returns false.
//   * next_event() peeks at events_[cursor_]; if its timestamp_ms is
//     <= current_ms the event is returned and cursor_ advanced. Otherwise
//     nullopt is returned (no event is due yet, or all consumed).
//   * reset() sets cursor_ = 0.
// =============================================================================
#include <cd/game/input_recorder/InputRecorder.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <span>

namespace cd::game::input_recorder
{

// =============================================================================
// Internal constants
// =============================================================================

namespace
{

// File format magic and version byte.
constexpr std::array<char, 4> kMagic   = {'C', 'D', 'I', 'R'};
constexpr std::uint8_t        kVersion = 0x01;

// ---- tiny write/read helpers ------------------------------------------------

// Write `value` as raw bytes into `out`. Returns false if the stream goes bad.
template<typename T>
[[nodiscard]] bool write_pod(std::ofstream& out, const T& value) noexcept
{
    out.write(reinterpret_cast<const char*>(&value), static_cast<std::streamsize>(sizeof(T)));
    return out.good();
}

// Read `sizeof(T)` bytes from `in` into `value`. Returns false on failure.
template<typename T>
[[nodiscard]] bool read_pod(std::ifstream& in, T& value) noexcept
{
    in.read(reinterpret_cast<char*>(&value), static_cast<std::streamsize>(sizeof(T)));
    return in.good();
}

}  // anonymous namespace

// =============================================================================
// Recorder
// =============================================================================

void Recorder::start_recording()
{
    events_.clear();
    active_ = true;
}

void Recorder::record(const InputEvent& ev)
{
    if (!active_)
    {
        return;
    }
    events_.push_back(ev);
}

void Recorder::stop_recording()
{
    active_ = false;
}

bool Recorder::save_to_file(const std::filesystem::path& path) const
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        return false;
    }

    // --- header ---
    out.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
    if (!out.good()) { return false; }

    if (!write_pod(out, kVersion)) { return false; }

    const auto count = static_cast<std::uint64_t>(events_.size());
    if (!write_pod(out, count)) { return false; }

    // --- body: one record per event ---
    for (const auto& ev : events_)
    {
        if (!write_pod(out, ev.timestamp_ms))                              { return false; }
        const auto kind_u32 = static_cast<std::uint32_t>(ev.kind);
        if (!write_pod(out, kind_u32))                                     { return false; }
        if (!write_pod(out, ev.code))                                      { return false; }
        // payload: 4 × float = 16 bytes
        for (const float f : ev.payload)
        {
            if (!write_pod(out, f))                                        { return false; }
        }
    }

    out.flush();
    return out.good();
}

std::size_t Recorder::event_count() const noexcept
{
    return events_.size();
}

bool Recorder::is_recording() const noexcept
{
    return active_;
}

// =============================================================================
// Replayer
// =============================================================================

bool Replayer::load_from_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
    {
        return false;
    }

    // --- validate magic ---
    std::array<char, 4> magic{};
    in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!in.good() || magic != kMagic)
    {
        return false;
    }

    // --- validate version ---
    std::uint8_t version{};
    if (!read_pod(in, version) || version != kVersion)
    {
        return false;
    }

    // --- read count ---
    std::uint64_t count{};
    if (!read_pod(in, count))
    {
        return false;
    }

    // Sanity cap: reject absurdly large counts before allocating (4 M events).
    constexpr std::uint64_t kMaxEvents = 4'000'000ULL;
    if (count > kMaxEvents)
    {
        return false;
    }

    // --- read body ---
    events_.clear();
    events_.reserve(static_cast<std::size_t>(count));

    for (std::uint64_t i = 0; i < count; ++i)
    {
        InputEvent ev;

        if (!read_pod(in, ev.timestamp_ms)) { return false; }

        std::uint32_t kind_u32{};
        if (!read_pod(in, kind_u32))        { return false; }
        ev.kind = static_cast<InputEventKind>(kind_u32);

        if (!read_pod(in, ev.code))         { return false; }

        for (float& f : ev.payload)
        {
            if (!read_pod(in, f))           { return false; }
        }

        events_.push_back(ev);
    }

    // Verify we consumed exactly the expected number of bytes (no trailing
    // garbage that could indicate a corrupt or truncated file).
    // Peek one byte; if we haven't reached EOF the file is malformed.
    in.peek();
    if (!in.eof())
    {
        events_.clear();
        return false;
    }

    cursor_ = 0;
    return true;
}

std::span<const InputEvent> Replayer::all() const noexcept
{
    return std::span<const InputEvent>{events_};
}

std::optional<InputEvent> Replayer::next_event(double current_ms)
{
    if (cursor_ >= events_.size())
    {
        return std::nullopt;
    }

    const InputEvent& ev = events_[cursor_];
    if (ev.timestamp_ms > current_ms)
    {
        // Not due yet.
        return std::nullopt;
    }

    ++cursor_;
    return ev;
}

void Replayer::reset() noexcept
{
    cursor_ = 0;
}

bool Replayer::is_finished() const noexcept
{
    return cursor_ >= events_.size();
}

std::size_t Replayer::event_count() const noexcept
{
    return events_.size();
}

}  // namespace cd::game::input_recorder
