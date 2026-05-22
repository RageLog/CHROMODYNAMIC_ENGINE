// =============================================================================
// CHROMODYNAMIC — cd/profile/Scope.cpp
// =============================================================================
#include <cd/profile/Scope.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>

namespace cd::profile
{

namespace
{

/// The no-op sink that ships installed at startup. Removes the need for a
/// nullable `current_sink()` and keeps the Scope dtor branch-free in the
/// "no profiling configured" case.
class NullSink final : public ISink
{
public:
    void submit(const Sample&) noexcept override
    {
        // Intentionally empty — instrumentation cost when profiling is off
        // is one atomic load + one virtual call + this empty function.
    }
};

NullSink& null_sink_instance() noexcept
{
    static NullSink instance;
    return instance;
}

std::atomic<ISink*>& sink_slot() noexcept
{
    static std::atomic<ISink*> slot { &null_sink_instance() };
    return slot;
}

}  // namespace

ISink* current_sink() noexcept
{
    return sink_slot().load(std::memory_order_acquire);
}

ISink* set_sink(ISink* next) noexcept
{
    if (next == nullptr)
        next = &null_sink_instance();
    return sink_slot().exchange(next, std::memory_order_acq_rel);
}

namespace detail
{

std::uint64_t now_ns() noexcept
{
    // steady_clock is the only choice that guarantees monotonic forward
    // motion across daylight-saving / NTP adjustments. system_clock can
    // jump backwards and break sample durations.
    using clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now().time_since_epoch()).count()
    );
}

std::uint64_t current_thread_hash() noexcept
{
    // std::thread::id is opaque; std::hash gives us a stable 64-bit
    // bucket per OS thread without dragging in pthreads / Win32.
    return std::hash<std::thread::id> {}(std::this_thread::get_id());
}

}  // namespace detail

}  // namespace cd::profile
