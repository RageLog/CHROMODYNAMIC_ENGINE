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

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/ecs/World.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
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

/// Sequential scheduler. v2 will swap the inner `tick_` loop for a
/// parallel dispatcher; the user-facing API is identical so calling code
/// gets the upgrade for free.
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
        // each step to preserve stability.
        order_.clear();
        order_.reserve(n);
        std::vector<std::size_t> ready;
        for (std::size_t i = 0; i < n; ++i)
            if (in_deg[i] == 0)
                ready.push_back(i);

        while (!ready.empty())
        {
            // Stable pick: smallest registration index.
            std::sort(ready.begin(), ready.end());
            const auto u = ready.front();
            ready.erase(ready.begin());
            order_.push_back(u);
            for (auto v : succ[u])
            {
                if (--in_deg[v] == 0)
                    ready.push_back(v);
            }
        }

        if (order_.size() != n)
        {
            return std::unexpected(
                scheduler_errors::make(scheduler_errors::Code::kCycleDetected, "scheduler: dependency cycle")
            );
        }
        sorted_ = true;
        return {};
    }

    std::vector<SystemDesc> systems_;
    std::vector<std::size_t> order_;
    bool sorted_ { false };
};

}  // namespace cd::ecs
