// =============================================================================
// CHROMODYNAMIC — cd/concurrency/SpinLock.hpp
// ADR-005 §G + ADR-015 — sub-µs critical sections only.
//
// Test-and-test-and-set spinlock with calibrated PAUSE/YIELD backoff and an
// optional std::this_thread::yield() escalation after a configured spin budget.
// For longer critical sections, prefer std::mutex (futex-backed on Linux,
// SRWLOCK-backed on Windows since C++17).
// =============================================================================
#pragma once

#include <cd/concurrency/Atomics.hpp>
#include <cd/core/Defines.hpp>

#include <atomic>
#include <thread>

namespace cd::concurrency
{

class SpinLock
{
public:
    SpinLock() noexcept = default;
    ~SpinLock() = default;

    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;

    void lock() noexcept
    {
        // Fast path: try a single CAS. If we lose, fall through to TTAS+backoff.
        if (!flag_.exchange(true, acq_rel_order))
        {
            return;
        }

        constexpr int kSpinPauseBudget = 64;   // tight cache-friendly spins
        constexpr int kSpinYieldBudget = 128;  // escalate to OS scheduler
        int spins = 0;
        while (true)
        {
            // Test (load) before test-and-set to avoid cache-line ping-pong.
            while (flag_.load(acquire_order))
            {
                ++spins;
                if (spins < kSpinPauseBudget)
                {
                    cpu_pause();
                }
                else if (spins < kSpinPauseBudget + kSpinYieldBudget)
                {
                    std::this_thread::yield();
                }
                else
                {
                    // Long-running contention — fall back to sleep-style yield so we
                    // don't burn a whole core.
                    std::this_thread::sleep_for(std::chrono::microseconds { 1 });
                }
            }
            if (!flag_.exchange(true, acq_rel_order))
            {
                return;
            }
        }
    }

    [[nodiscard]] bool try_lock() noexcept
    {
        if (flag_.load(acquire_order))
        {
            return false;
        }
        return !flag_.exchange(true, acq_rel_order);
    }

    void unlock() noexcept
    {
        flag_.store(false, release_order);
    }

private:
    std::atomic<bool> flag_ { false };
};

/// RAII guard.
class SpinLockGuard
{
public:
    explicit SpinLockGuard(SpinLock& lock) noexcept
        : lock_ { &lock }
    {
        lock_->lock();
    }

    ~SpinLockGuard()
    {
        lock_->unlock();
    }

    SpinLockGuard(const SpinLockGuard&) = delete;
    SpinLockGuard& operator=(const SpinLockGuard&) = delete;

private:
    SpinLock* lock_;
};

}  // namespace cd::concurrency
