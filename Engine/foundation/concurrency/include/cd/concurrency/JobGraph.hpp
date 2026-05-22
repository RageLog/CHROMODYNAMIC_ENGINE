// =============================================================================
// CHROMODYNAMIC — cd/concurrency/JobGraph.hpp
// ADR-015 — DAG primitive for task graph execution.
//
// Each JobNode has an `id`, a `Task` body, and a list of `dependencies` (other
// node ids). `run(pool)` performs topological execution: nodes whose deps are
// satisfied are submitted to `pool`; the call blocks until all nodes complete.
// Cycle detection is performed once at run-time (returns false).
// =============================================================================
#pragma once

#include <cd/concurrency/ThreadPool.hpp>
#include <cd/core/Defines.hpp>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace cd::concurrency
{

using JobId = std::uint32_t;
inline constexpr JobId kInvalidJobId = ~JobId { 0 };

class JobGraph
{
public:
    /// Add a node. `body` is invoked once when all its dependencies have run.
    /// Returns the node's id.
    JobId
    add(std::function<void()> body, std::vector<JobId> dependencies = {}, TaskPriority priority = TaskPriority::Normal)
    {
        Node n;
        n.body = std::move(body);
        n.dependencies = std::move(dependencies);
        n.priority = priority;
        nodes_.push_back(std::move(n));
        return static_cast<JobId>(nodes_.size() - 1);
    }

    /// Execute the graph on `pool`. Returns true on success, false if a cycle
    /// or invalid dep is detected. Blocks until completion.
    [[nodiscard]] bool run(ThreadPool& pool)
    {
        const std::size_t n = nodes_.size();
        failed_nodes_.store(0, std::memory_order_release);
        if (n == 0)
        {
            return true;
        }
        // JobId is uint32_t; reject any graph whose size cannot be represented
        // exactly so the static_cast<JobId>(n) in loops below never truncates.
        if (n > static_cast<std::size_t>(kInvalidJobId))
        {
            return false;
        }
        // Compute reverse adjacency + in-degrees with validation.
        std::vector<std::vector<JobId>> rev(n);
        std::vector<std::atomic<std::size_t>> in_degree(n);
        for (JobId i = 0; i < static_cast<JobId>(n); ++i)
        {
            in_degree[i].store(nodes_[i].dependencies.size(), std::memory_order_relaxed);
            for (auto dep : nodes_[i].dependencies)
            {
                if (dep >= n)
                {
                    return false;
                }
                rev[dep].push_back(i);
            }
        }
        if (has_cycle())
        {
            return false;
        }

        std::atomic<std::size_t> remaining { n };
        std::mutex done_mutex;
        std::condition_variable done_cond;

        // Recursive submit lambda — when a node completes, decrement successors'
        // in-degree and submit those that hit 0.
        std::function<void(JobId)> submit_node;
        submit_node = [&, this](JobId id)
        {
            pool.submit_detached_with_priority(
                nodes_[id].priority,
                [&, this, id]
                {
                    try
                    {
                        if (nodes_[id].body)
                            nodes_[id].body();
                    }
                    catch (...)
                    {
                        // The graph cannot propagate an exception to a caller (each node
                        // runs on a detached pool task). Nodes that need to surface errors
                        // must capture them inside their body via std::promise or
                        // cd::core::Result. We record the count for diagnostics so a stuck
                        // graph isn't silently masked.
                        failed_nodes_.fetch_add(1, std::memory_order_relaxed);
                    }
                    for (auto succ : rev[id])
                    {
                        if (in_degree[succ].fetch_sub(1, std::memory_order_acq_rel) == 1)
                        {
                            submit_node(succ);
                        }
                    }
                    if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1)
                    {
                        std::lock_guard guard { done_mutex };
                        done_cond.notify_all();
                    }
                }
            );
        };

        // Kick off all initially-ready nodes.
        for (JobId i = 0; i < static_cast<JobId>(n); ++i)
        {
            if (in_degree[i].load(std::memory_order_relaxed) == 0)
            {
                submit_node(i);
            }
        }

        // Wait for the whole graph to drain.
        std::unique_lock guard { done_mutex };
        done_cond.wait(
            guard,
            [&]
            {
                return remaining.load(std::memory_order_acquire) == 0;
            }
        );
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return nodes_.size();
    }

    void clear() noexcept
    {
        nodes_.clear();
        failed_nodes_.store(0, std::memory_order_relaxed);
    }

    /// Number of node bodies that exited via an uncaught exception during the
    /// most recent (or in-progress) run. Reset by clear() and by the next run().
    [[nodiscard]] std::uint64_t failed_nodes() const noexcept
    {
        return failed_nodes_.load(std::memory_order_acquire);
    }

private:
    struct Node
    {
        std::function<void()> body;
        std::vector<JobId> dependencies;
        TaskPriority priority { TaskPriority::Normal };
    };

    std::atomic<std::uint64_t> failed_nodes_ { 0 };

    [[nodiscard]] bool has_cycle() const
    {
        const std::size_t n = nodes_.size();
        std::vector<int> color(n, 0);  // 0=white, 1=gray, 2=black
        std::function<bool(JobId)> dfs = [&](JobId u) -> bool
        {
            color[u] = 1;
            for (auto v : nodes_[u].dependencies)
            {
                if (v >= n)
                    return true;
                if (color[v] == 1)
                    return true;
                if (color[v] == 0 && dfs(v))
                    return true;
            }
            color[u] = 2;
            return false;
        };
        for (JobId i = 0; i < static_cast<JobId>(n); ++i)
        {
            if (color[i] == 0 && dfs(i))
                return true;
        }
        return false;
    }

    std::vector<Node> nodes_;
};

}  // namespace cd::concurrency
