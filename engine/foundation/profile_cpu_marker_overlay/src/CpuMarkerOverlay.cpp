// =============================================================================
// CHROMODYNAMIC — cd/profile/cpu_marker_overlay/CpuMarkerOverlay.cpp
// =============================================================================
#include <cd/profile/cpu_marker_overlay/CpuMarkerOverlay.hpp>

#include <cd/ui/renderer/DrawBatcher.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace cd::profile::cpu_marker_overlay
{

// ---- helpers ----------------------------------------------------------------

namespace
{

[[nodiscard]] double now_ms() noexcept
{
    using clock = std::chrono::steady_clock;
    const auto ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now().time_since_epoch()).count();
    return static_cast<double>(ns) * 1.0e-6;
}

[[nodiscard]] std::uint32_t this_thread_id() noexcept
{
    // Portable: hash std::thread::id and truncate.
    const auto h = std::hash<std::thread::id>{}(std::this_thread::get_id());
    return static_cast<std::uint32_t>(h & 0xFFFF'FFFFu);
}

/// One in-flight (begun but not yet ended) marker.
struct InFlight
{
    std::string   name;
    double        start_ms { 0.0 };
    std::uint32_t thread_id { 0U };
    bool          active { false };
};

}  // namespace

// ---- Collector::Impl --------------------------------------------------------

struct Collector::Impl
{
    explicit Impl(std::size_t cap) : capacity(cap)
    {
        ring.reserve(cap);
        in_flight.reserve(64);
    }

    mutable std::mutex mtx;

    std::size_t capacity;

    // Completed samples stored as a ring; oldest entry is overwritten when full.
    std::vector<MarkerSample> ring;
    std::size_t               write_head { 0U };
    bool                      ring_full  { false };

    // In-flight table — indices 0..N are reused via a free-list.
    std::vector<InFlight>     in_flight;
    std::vector<std::uint32_t> free_list;

    // Allocate a slot in in_flight, returning its index.
    [[nodiscard]] std::uint32_t alloc_slot()
    {
        if (!free_list.empty())
        {
            const std::uint32_t idx = free_list.back();
            free_list.pop_back();
            return idx;
        }
        in_flight.push_back({});
        return static_cast<std::uint32_t>(in_flight.size() - 1U);
    }

    void commit(MarkerSample sample)
    {
        if (ring.size() < capacity)
        {
            ring.push_back(std::move(sample));
        }
        else
        {
            ring[write_head] = std::move(sample);
            ring_full        = true;
        }
        write_head = (write_head + 1U) % capacity;
    }
};

// ---- Collector --------------------------------------------------------------

Collector::Collector(std::size_t capacity)
    : impl_(std::make_unique<Impl>(capacity))
{
}

Collector::~Collector() = default;

MarkerHandle Collector::begin(std::string_view name)
{
    const double        t   = now_ms();
    const std::uint32_t tid = this_thread_id();

    std::scoped_lock lock(impl_->mtx);
    const std::uint32_t idx = impl_->alloc_slot();

    InFlight& slot   = impl_->in_flight[idx];
    slot.name        = std::string(name);
    slot.start_ms    = t;
    slot.thread_id   = tid;
    slot.active      = true;

    return MarkerHandle { idx };
}

void Collector::end(MarkerHandle handle)
{
    if (!handle.valid())
        return;

    const double finish = now_ms();

    std::scoped_lock lock(impl_->mtx);

    if (handle.index >= static_cast<std::uint32_t>(impl_->in_flight.size()))
        return;

    InFlight& slot = impl_->in_flight[handle.index];
    if (!slot.active)
        return;

    MarkerSample sample;
    sample.name        = std::move(slot.name);
    sample.start_ms    = slot.start_ms;
    sample.duration_ms = finish - slot.start_ms;
    sample.thread_id   = slot.thread_id;

    slot.active        = false;
    impl_->free_list.push_back(handle.index);

    impl_->commit(std::move(sample));
}

std::vector<MarkerSample> Collector::samples_since(double cutoff_ms) const
{
    std::scoped_lock lock(impl_->mtx);

    std::vector<MarkerSample> result;
    result.reserve(impl_->ring.size());

    for (const auto& s : impl_->ring)
    {
        if (s.start_ms >= cutoff_ms)
            result.push_back(s);
    }
    return result;
}

std::vector<MarkerAggregate> Collector::aggregate(double cutoff_ms) const
{
    std::scoped_lock lock(impl_->mtx);

    // Use a map from name -> index-into-result to preserve first-seen order
    // while accumulating stats in O(n).
    std::vector<MarkerAggregate> result;
    std::unordered_map<std::string, std::size_t> name_index;

    for (const auto& s : impl_->ring)
    {
        if (s.start_ms < cutoff_ms)
            continue;

        auto it = name_index.find(s.name);
        if (it == name_index.end())
        {
            name_index.emplace(s.name, result.size());
            MarkerAggregate ag;
            ag.name     = s.name;
            ag.count    = 1U;
            ag.total_ms = s.duration_ms;
            ag.avg_ms   = s.duration_ms;
            ag.max_ms   = s.duration_ms;
            result.emplace_back(std::move(ag));
        }
        else
        {
            MarkerAggregate& ag = result[it->second];
            ++ag.count;
            ag.total_ms += s.duration_ms;
            ag.max_ms    = std::max(ag.max_ms, s.duration_ms);
            ag.avg_ms    = ag.total_ms / static_cast<double>(ag.count);
        }
    }
    return result;
}

std::size_t Collector::sample_count() const noexcept
{
    std::scoped_lock lock(impl_->mtx);
    return impl_->ring.size();
}

void Collector::clear() noexcept
{
    std::scoped_lock lock(impl_->mtx);
    impl_->ring.clear();
    impl_->write_head = 0U;
    impl_->ring_full  = false;
}

// ---- Overlay ----------------------------------------------------------------

namespace
{

/// djb2 hash of a name — same function used by both draw() and compute_bars()
/// so results are consistent.
[[nodiscard]] std::uint32_t djb2_hash(std::string_view name) noexcept
{
    std::uint32_t h = 5381U;
    for (const char c : name)
        h = ((h << 5U) + h) + static_cast<std::uint32_t>(static_cast<unsigned char>(c));
    return h;
}

[[nodiscard]] cd::ui::renderer::Color colour_from_hash(std::uint32_t h) noexcept
{
    const auto r = static_cast<std::uint8_t>(h & 0xFFu);
    const auto g = static_cast<std::uint8_t>((h >> 8U) & 0xFFu);
    const auto b = static_cast<std::uint8_t>((h >> 16U) & 0xFFu);
    return cd::ui::renderer::Color { r, g, b, 200U };
}

/// Shared layout computation: fills `bars` with one entry per visible sample.
/// `thread_ids` is also populated in first-seen order.
void compute_layout(std::span<const MarkerSample> samples,
                    Rect                          bounds,
                    double                        time_range,
                    std::vector<BarRect>&         bars,
                    std::vector<std::uint32_t>&   thread_ids)
{
    if (samples.empty() || bounds.width <= 0.0F || bounds.height <= 0.0F)
        return;

    double min_start = samples[0].start_ms;
    for (const auto& s : samples)
        min_start = std::min(min_start, s.start_ms);

    for (const auto& s : samples)
    {
        if (std::ranges::find(thread_ids, s.thread_id) == thread_ids.end())
            thread_ids.push_back(s.thread_id);
    }

    const auto  lane_count  = static_cast<float>(thread_ids.empty() ? 1U : thread_ids.size());
    const float lane_height = bounds.height / lane_count;
    const float ppm         = bounds.width / static_cast<float>(time_range);

    for (const auto& s : samples)
    {
        const double rel_start = s.start_ms - min_start;
        if (rel_start + s.duration_ms < 0.0)
            continue;
        if (rel_start > time_range)
            continue;

        const auto it         = std::ranges::find(thread_ids, s.thread_id);
        const auto lane_index = static_cast<float>(std::distance(thread_ids.begin(), it));

        BarRect br;
        br.x           = bounds.x + static_cast<float>(rel_start) * ppm;
        br.y           = bounds.y + lane_index * lane_height;
        br.w           = std::max(1.0F, static_cast<float>(s.duration_ms) * ppm);
        br.h           = lane_height * 0.8F;
        br.colour_hash = djb2_hash(s.name);
        bars.emplace_back(br);
    }
}

}  // namespace

Overlay::Overlay(double window_ms)
    : window_ms_(window_ms)
{
}

std::vector<BarRect>
Overlay::compute_bars(std::span<const MarkerSample> samples,
                      Rect                          bounds) const
{
    if (samples.empty() || bounds.width <= 0.0F || bounds.height <= 0.0F)
        return {};

    const double time_range = (window_ms_ > 0.0) ? window_ms_ : 1.0;

    std::vector<BarRect>       bars;
    std::vector<std::uint32_t> thread_ids;
    compute_layout(samples, bounds, time_range, bars, thread_ids);
    return bars;
}

void Overlay::draw(cd::ui::renderer::DrawBatcher& batcher,
                   std::span<const MarkerSample>  samples,
                   Rect                           bounds) const
{
    if (samples.empty() || bounds.width <= 0.0F || bounds.height <= 0.0F)
        return;

    const double time_range = (window_ms_ > 0.0) ? window_ms_ : 1.0;

    std::vector<BarRect>       bars;
    std::vector<std::uint32_t> thread_ids;
    compute_layout(samples, bounds, time_range, bars, thread_ids);

    for (const auto& br : bars)
        batcher.quad(br.x, br.y, br.w, br.h, colour_from_hash(br.colour_hash));
}

}  // namespace cd::profile::cpu_marker_overlay
