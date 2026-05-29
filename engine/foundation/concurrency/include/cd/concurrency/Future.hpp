// =============================================================================
// CHROMODYNAMIC — cd/concurrency/Future.hpp
// Phase 76.B / Wave 244 — lightweight result handle (Promise / Future pair).
//
// Mini-`std::future` over `T`: a shared state with a value slot, a
// "ready" flag, and a condition variable. The producer holds a Promise
// and calls `set(v)`; the consumer holds a Future and calls
// `wait()` / `get()` to retrieve the value.
//
// Compared to std::future, this primitive:
//   * never throws (status query separated from `get`).
//   * trivially shareable (multiple consumers can wait on the same
//     Future; the value is copyable).
//
// Use for "fire-and-wait" job results — asset cook → editor wait,
// async shader compile → renderer ready check.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>

namespace cd::concurrency
{

template <class T>
struct FutureState
{
    std::mutex              mu;
    std::condition_variable cv;
    std::optional<T>        value;
    std::atomic<bool>       ready { false };
};

template <class T>
class Future
{
public:
    explicit Future(std::shared_ptr<FutureState<T>> s) : state_ { std::move(s) } {}

    [[nodiscard]] bool is_ready() const noexcept
    {
        return state_->ready.load(std::memory_order_acquire);
    }

    void wait() const
    {
        std::unique_lock<std::mutex> lock { state_->mu };
        state_->cv.wait(lock, [this]
        {
            return state_->ready.load(std::memory_order_acquire);
        });
    }

    [[nodiscard]] std::optional<T> try_get() const
    {
        if (!is_ready()) return std::nullopt;
        return state_->value;
    }

    [[nodiscard]] T get() const
    {
        wait();
        return *state_->value;
    }

private:
    std::shared_ptr<FutureState<T>> state_;
};

template <class T>
class Promise
{
public:
    Promise() : state_ { std::make_shared<FutureState<T>>() } {}

    void set(T value) const
    {
        {
            std::scoped_lock lock { state_->mu };
            state_->value = std::move(value);
            state_->ready.store(true, std::memory_order_release);
        }
        state_->cv.notify_all();
    }

    [[nodiscard]] Future<T> future() const { return Future<T> { state_ }; }

private:
    std::shared_ptr<FutureState<T>> state_;
};

}  // namespace cd::concurrency
