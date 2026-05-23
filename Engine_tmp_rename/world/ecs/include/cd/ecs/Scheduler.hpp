// =============================================================================
// CHROMODYNAMIC — cd/ecs/Scheduler.hpp
//
// System scheduler over `cd::ecs::World`. Per ADR-20260522-ecs-storage-
// validation, the SOTA-recommended path is "declared reads/writes →
// dependency DAG → parallel dispatch on an external IJobDispatcher".
// This v1 implementation lands the dependency declaration + topological
// ordering + SEQUENTIAL run, leaving the parallel dispatcher hook for v2.
//
// API:
//   auto move    = SystemDesc::write<Pos>().read<Vel>().fn([](World& w){...});
//   auto collide = SystemDesc::write<Vel>().read<Pos>().fn([](World& w){...});
//   Scheduler s;
//   s.add(std::move(move));
//   s.add(std::move(collide));
//   s.tick(world);   // runs systems in conflict-respecting order
//
// Conflict rule: two systems conflict if A writes component T AND B reads
// OR writes T. Conflicting systems must run serially relative to each
// other; non-conflicting systems can run in any order (or in parallel
// once the v2 dispatcher arrives).
//
// Topological sort: registration order is preserved among non-conflicting
// systems (stable sort over the DAG produced by conflict edges). This
// makes test output deterministic — the user can reason about the order
// from the add() calls when no conflict forces a different ordering.
//
// Cycles: a write/write cycle between two systems is a bug at registration
// time. We detect the cycle in tick() and return a Result error rather
// than infinite-loop; the caller can fix system ordering or split the
// component into two narrower types.
// =============================================================================
#pragma once

#include <cd/concurrency/ThreadPool.hpp>
#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/ecs/World.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <future>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cd::ecs
{

namespace scheduler_errors
{
inline constexpr std::uint32_t kDomain = 0x0013;

enum class Code : std::uint32_t
{
    kOk = 0,
    kCycleDetected = 1,
    kInvalidArgument = 2,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace scheduler_errors

/// One scheduled system. Built via the fluent builder methods so the
/// user reads the declaration left-to-right: "name → reads → writes →
/// the callable".
class SystemDesc
{
public:
    using Fn = std::function<void(World&)>;

    explicit SystemDesc(std::string_view name = "system")
        : name_ { name }
    {
    }

    /// Declare a READ dependency on component T.
    template <class T>
    SystemDesc& reads()
    {
        reads_.insert(std::type_index { typeid(T) });
        return *this;
    }

    /// Declare a WRITE dependency on component T.
    template <class T>
    SystemDesc& writes()
    {
        writes_.insert(std::type_index { typeid(T) });
        return *this;
    }

    /// Attach the body. Required — adding a SystemDesc without a fn() is
    /// a no-op at tick time and almost certainly a bug.
    SystemDesc& fn(Fn f)
    {
        body_ = std::move(f);
        return *this;
    }

    [[nodiscard]] const std::string& name() const noexcept
    {
        return name_;
    }

    [[nodiscard]] const std::unordered_set<std::type_index>& read_set() const noexcept
    {
        return reads_;
    }

    [[nodiscard]] const std::unordered_set<std::type_index>& write_set() const noexcept
    {
        return writes_;
    }

    [[nodiscard]] const Fn& body() const noexcept
    {
        return body_;
    }

private:
    std::string name_;
    std::unordered_set<std::type_index> reads_;
    std::unordered_set<std::type_index> writes_;
    Fn body_;
};

/// Scheduler with two dispatch modes:
///   * tick(World&)         — sequential (registration-order-stable
///                            within each parallel-safe set).
///   * tick_parallel(World&)— per-stage parallel: a "stage" is the set
///                            of systems with no remaining dependencies
///                            (Kahn level). Stages run in order, systems
///                            within a stage run concurrently via
///                            `std::async(std::launch::async)`.
///
/// Both modes produce identical output assuming system bodies don't
/// touch global state outside their declared reads/writes — which is
/// the only safe pattern anyway. Parallel mode is opt-in; calling code
/// gets the speedup only when it asks.
class Scheduler
{
public:
    Scheduler() noexcept = default;

    /// Register a system. Order is preserved among non-conflicting systems
    /// (stable topological sort).
    void add(SystemDesc desc)
    {
        systems_.push_back(std::move(desc));
        sorted_ = false;
    }

    /// Number of registered systems.
    [[nodiscard]] std::size_t size() const noexcept
    {
        return systems_.size();
    }

    /// Run every system once on `world`, in dependency order. First call
    /// (or any call after add()) recomputes the ordering. Cycles return
    /// kCycleDetected and no system runs that frame.
    [[nodiscard]] cd::core::Result<void> tick(World& world)
    {
        if (!sorted_)
        {
            auto r = build_order_();
            if (!r.has_value())
                return std::unexpected(r.error());
        }
        for (auto idx : order_)
        {
            const auto& s = systems_[idx];
            if (s.body())
                s.body()(world);
        }
        return {};
    }

    /// Compute (read-only) ordering preview without running. Useful for
    /// tests and debug introspection.
    [[nodiscard]] cd::core::Result<std::vector<std::string>> preview_order()
    {
        if (!sorted_)
        {
            auto r = build_order_();
            if (!r.has_value())
                return std::unexpected(r.error());
        }
        std::vector<std::string> out;
        out.reserve(order_.size());
        for (auto idx : order_)
            out.push_back(systems_[idx].name());
        return out;
    }

    /// Run every system once on `world`, dispatching parallel-safe sets
    /// concurrently. Within each stage, systems run via std::async; each
    /// stage waits for all its jobs before the next stage starts.
    /// Single-system stages run inline (no thread overhead). Identical
    /// observable result to `tick()` for systems that respect their
    /// declared reads/writes.
    [[nodiscard]] cd::core::Result<void> tick_parallel(World& world)
    {
        if (!sorted_)
        {
            auto r = build_order_();
            if (!r.has_value())
                return std::unexpected(r.error());
        }
        for (const auto& stage : stages_)
        {
            if (stage.size() <= 1)
            {
                for (auto idx : stage)
                {
                    const auto& s = systems_[idx];
                    if (s.body())
                        s.body()(world);
                }
                continue;
            }
            std::vector<std::future<void>> futures;
            futures.reserve(stage.size());
            for (auto idx : stage)
            {
                const auto& s = systems_[idx];
                if (!s.body())
                    continue;
                futures.push_back(
                    std::async(
                        std::launch::async,
                        [&s, &world]()
                        {
                            s.body()(world);
                        }
                    )
                );
            }
            for (auto& f : futures)
                f.get();  // .get() rethrows exceptions (asserts in test).
        }
        return {};
    }

    /// Same observable behaviour as `tick_parallel(World&)` but dispatches
    /// each stage's jobs onto a shared `cd::concurrency::ThreadPool`
    /// instead of spawning a fresh thread per system via std::async.
    /// Significantly cheaper for engines that tick the scheduler many
    /// times per second — the thread-pool worker reuse eliminates
    /// per-tick thread creation cost.
    [[nodiscard]] cd::core::Result<void>
    tick_parallel(World& world, cd::concurrency::ThreadPool& pool)
    {
        if (!sorted_)
        {
            auto r = build_order_();
            if (!r.has_value())
                return std::unexpected(r.error());
        }
        for (const auto& stage : stages_)
        {
            if (stage.size() <= 1)
            {
                for (auto idx : stage)
                {
                    const auto& s = systems_[idx];
                    if (s.body())
                        s.body()(world);
                }
                continue;
            }
            std::vector<std::future<void>> futures;
            futures.reserve(stage.size());
            for (auto idx : stage)
            {
                const auto& s = systems_[idx];
                if (!s.body())
                    continue;
                futures.push_back(
                    pool.submit([&s, &world]() { s.body()(world); })
                );
            }
            for (auto& f : futures)
                f.get();  // rethrows exceptions to surface in test harness.
        }
        return {};
    }

    /// Read-only preview of the parallel stage layout. stages[i] holds
    /// the names of every system in the i-th concurrently-dispatchable
    /// batch.
    [[nodiscard]] cd::core::Result<std::vector<std::vector<std::string>>> preview_stages()
    {
        if (!sorted_)
        {
            auto r = build_order_();
            if (!r.has_value())
                return std::unexpected(r.error());
        }
        std::vector<std::vector<std::string>> out;
        out.reserve(stages_.size());
        for (const auto& st : stages_)
        {
            std::vector<std::string> names;
            names.reserve(st.size());
            for (auto idx : st)
                names.push_back(systems_[idx].name());
            out.push_back(std::move(names));
        }
        return out;
    }

private:
    [[nodiscard]] static bool conflicts_(const SystemDesc& a, const SystemDesc& b) noexcept
    {
        // A writes ∩ B reads
        for (const auto& t : a.write_set())
        {
            if (b.read_set().count(t) != 0 || b.write_set().count(t) != 0)
                return true;
        }
        // A reads ∩ B writes (commutative half)
        for (const auto& t : a.read_set())
        {
            if (b.write_set().count(t) != 0)
                return true;
        }
        return false;
    }

    cd::core::Result<void> build_order_()
    {
        const std::size_t n = systems_.size();
        // Edge list i → j means "i must run before j". For each ordered
        // pair (i, j) with i < j, if they conflict, i precedes j. This
        // preserves registration order as the tie-break per the docstring.
        std::vector<std::vector<std::size_t>> succ(n);
        std::vector<int> in_deg(n, 0);
        for (std::size_t i = 0; i < n; ++i)
        {
            for (std::size_t j = i + 1; j < n; ++j)
            {
                if (conflicts_(systems_[i], systems_[j]))
                {
                    succ[i].push_back(j);
                    ++in_deg[j];
                }
            }
        }

        // Kahn-style topological sort, picking the smallest available index
        // each step to preserve stability. We ALSO record the level at
        // which each system becomes ready — that's the parallel-stage
        // assignment used by `tick_parallel`.
        order_.clear();
        order_.reserve(n);
        stages_.clear();
        std::vector<int> remaining_in_deg = in_deg;
        std::vector<bool> processed(n, false);
        std::size_t total_processed = 0;

        while (total_processed < n)
        {
            std::vector<std::size_t> stage;
            for (std::size_t i = 0; i < n; ++i)
            {
                if (!processed[i] && remaining_in_deg[i] == 0)
                    stage.push_back(i);
            }
            if (stage.empty())
            {
                return std::unexpected(
                    scheduler_errors::make(scheduler_errors::Code::kCycleDetected, "scheduler: dependency cycle")
                );
            }
            std::sort(stage.begin(), stage.end());  // registration-order stable.
            for (auto u : stage)
            {
                processed[u] = true;
                ++total_processed;
                order_.push_back(u);
                for (auto v : succ[u])
                    --remaining_in_deg[v];
            }
            stages_.push_back(std::move(stage));
        }

        sorted_ = true;
        return {};
    }

    std::vector<SystemDesc> systems_;
    std::vector<std::size_t> order_;
    /// Kahn-level groups (parallel-safe sets). stages_[k] holds the
    /// system indices that become runnable at level k.
    std::vector<std::vector<std::size_t>> stages_;
    bool sorted_ { false };
};

}  // namespace cd::ecs
