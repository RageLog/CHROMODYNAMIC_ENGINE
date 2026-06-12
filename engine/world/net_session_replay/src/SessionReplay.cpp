// =============================================================================
// CHROMODYNAMIC — cd/net/session_replay/SessionReplay.cpp
// Phase 600 — cd::net::session_replay Sprint-1 implementation
// =============================================================================
#include <cd/net/session_replay/SessionReplay.hpp>

#include <array>
#include <chrono>
#include <fstream>

namespace cd::net::session_replay
{

// ---------------------------------------------------------------------------
// Binary format constants
// ---------------------------------------------------------------------------

namespace
{

// Magic bytes: "SRPK" (Session Replay PacKet)
constexpr std::array<std::uint8_t, 4> kMagic { 0x53, 0x52, 0x50, 0x4B };
constexpr std::uint32_t               kVersion { 1 };

// ---------------------------------------------------------------------------
// Low-level I/O helpers (little-endian; the engine targets LE platforms only
// during Phase 1 — a bswap pass would be added for BE if needed).
// ---------------------------------------------------------------------------

template<typename T>
bool write_pod(std::ofstream& out, const T& value)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    out.write(reinterpret_cast<const char*>(&value), static_cast<std::streamsize>(sizeof(T)));
    return out.good();
}

template<typename T>
bool read_pod(std::ifstream& in, T& value)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    in.read(reinterpret_cast<char*>(&value), static_cast<std::streamsize>(sizeof(T)));
    return in.good();
}

}  // namespace

// ---------------------------------------------------------------------------
// Recorder
// ---------------------------------------------------------------------------

void Recorder::start_recording()
{
    records_.clear();
    start_time_ = Clock::now();
    recording_  = true;
}

void Recorder::record_packet(std::span<const std::uint8_t> payload,
                              std::uint32_t                  channel_id,
                              bool                           incoming)
{
    if (!recording_)
        return;

    using Ms = std::chrono::duration<double, std::milli>;
    const double ts_ms = std::chrono::duration_cast<Ms>(Clock::now() - start_time_).count();

    PacketRecord rec;
    rec.timestamp_ms = ts_ms;
    rec.channel_id   = channel_id;
    rec.incoming     = incoming;
    rec.payload.assign(payload.begin(), payload.end());
    records_.push_back(std::move(rec));
}

void Recorder::stop_recording() noexcept
{
    recording_ = false;
}

bool Recorder::save_to_file(const std::filesystem::path& out_path) const
{
    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        return false;

    // Header — magic
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    out.write(reinterpret_cast<const char*>(kMagic.data()),
              static_cast<std::streamsize>(kMagic.size()));
    if (!out.good())
        return false;

    // Header — version
    if (!write_pod(out, kVersion))
        return false;

    // Header — count
    const auto count = static_cast<std::uint32_t>(records_.size());
    if (!write_pod(out, count))
        return false;

    // Records
    for (const PacketRecord& rec : records_)
    {
        if (!write_pod(out, rec.timestamp_ms))
            return false;
        if (!write_pod(out, rec.channel_id))
            return false;
        const std::uint8_t incoming_byte = rec.incoming ? std::uint8_t{1} : std::uint8_t{0};
        if (!write_pod(out, incoming_byte))
            return false;
        const auto payload_size = static_cast<std::uint32_t>(rec.payload.size());
        if (!write_pod(out, payload_size))
            return false;
        if (payload_size > 0)
        {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            out.write(reinterpret_cast<const char*>(rec.payload.data()),
                      static_cast<std::streamsize>(payload_size));
            if (!out.good())
                return false;
        }
    }

    return out.good();
}

std::size_t Recorder::packet_count() const noexcept
{
    return records_.size();
}

bool Recorder::is_recording() const noexcept
{
    return recording_;
}

// ---------------------------------------------------------------------------
// Replayer
// ---------------------------------------------------------------------------

bool Replayer::load_from_file(const std::filesystem::path& in_path)
{
    std::ifstream in(in_path, std::ios::binary);
    if (!in.is_open())
        return false;

    // Validate magic bytes.
    std::array<std::uint8_t, 4> magic {};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    in.read(reinterpret_cast<char*>(magic.data()),
            static_cast<std::streamsize>(magic.size()));
    if (!in.good() || magic != kMagic)
        return false;

    // Validate version.
    std::uint32_t version { 0 };
    if (!read_pod(in, version) || version != kVersion)
        return false;

    // Record count.
    std::uint32_t count { 0 };
    if (!read_pod(in, count))
        return false;

    std::vector<PacketRecord> records;
    records.reserve(count);

    for (std::uint32_t i = 0; i < count; ++i)
    {
        PacketRecord rec;

        if (!read_pod(in, rec.timestamp_ms))
            return false;
        if (!read_pod(in, rec.channel_id))
            return false;

        std::uint8_t incoming_byte { 0 };
        if (!read_pod(in, incoming_byte))
            return false;
        rec.incoming = (incoming_byte != 0);

        std::uint32_t payload_size { 0 };
        if (!read_pod(in, payload_size))
            return false;

        if (payload_size > 0)
        {
            rec.payload.resize(payload_size);
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            in.read(reinterpret_cast<char*>(rec.payload.data()),
                    static_cast<std::streamsize>(payload_size));
            if (!in.good())
                return false;
        }

        records.push_back(std::move(rec));
    }

    records_ = std::move(records);
    cursor_  = 0;
    return true;
}

std::span<const PacketRecord> Replayer::all() const noexcept
{
    return { records_.data(), records_.size() };
}

std::optional<PacketRecord> Replayer::next_packet(double current_ms)
{
    if (cursor_ >= records_.size())
        return std::nullopt;

    const PacketRecord& rec = records_[cursor_];
    if (rec.timestamp_ms > current_ms)
        return std::nullopt;

    ++cursor_;
    return rec;
}

void Replayer::reset() noexcept
{
    cursor_ = 0;
}

}  // namespace cd::net::session_replay
