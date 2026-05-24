// =============================================================================
// CHROMODYNAMIC — cd/concurrency/Channel.hpp
// Phase 56.B / Wave 224 — bounded MPSC channel.
//
// Multi-producer, single-consumer bounded queue with blocking + non-
// blocking send / receive. Built on `std::mutex` + `std::condition_variable`
// — correctness first, raw throughput later. For lock-free MPSC see
// the ThreadPool's internal deque (Phase 4); this primitive is the
// "cross-thread message passing" muscle.
//
// API:
//   * `try_send(x)` — non-blocking; returns false if full.
//   * `send(x)` — blocks if full until consumer drains.
//   * `try_receive()` — returns std::optional<T>.
//   * `receive()` — blocks until a message arrives or `close()` fires.
//   * `close()` — signals no more messages; waiting receivers wake up
//                 with `std::nullopt`.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>

namespace cd::concurrency
{

template <class T>
class Channel
{
public:
    explicit Channel(std::size_t capacity)
        : capacity_ { capacity == 0 ? 1u : capacity } {}

    bool try_send(T value)
    {
        std::unique_lock<std::mutex> lock { mu_ };
        if (closed_ || queue_.size() >= capacity_) return false;
        queue_.push_back(std::move(value));
        not_empty_.notify_one();
        return true;
    }

    bool send(T value)
    {
        std::unique_lock<std::mutex> lock { mu_ };
        not_full_.wait(lock, [this] { return closed_ || queue_.size() < capacity_; });
        if (closed_) return false;
        queue_.push_back(std::move(value));
        not_empty_.notify_one();
        return true;
    }

    [[nodiscard]] std::optional<T> try_receive()
    {
        std::unique_lock<std::mutex> lock { mu_ };
        if (queue_.empty()) return std::nullopt;
        T v = std::move(queue_.front());
        queue_.pop_front();
        not_full_.notify_one();
        return v;
    }

    [[nodiscard]] std::optional<T> receive()
    {
        std::unique_lock<std::mutex> lock { mu_ };
        not_empty_.wait(lock, [this] { return closed_ || !queue_.empty(); });
        if (queue_.empty()) return std::nullopt;
        T v = std::move(queue_.front());
        queue_.pop_front();
        not_full_.notify_one();
        return v;
    }

    void close()
    {
        std::unique_lock<std::mutex> lock { mu_ };
        closed_ = true;
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    [[nodiscard]] std::size_t size() const
    {
        std::unique_lock<std::mutex> lock { mu_ };
        return queue_.size();
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

private:
    std::size_t                 capacity_;
    mutable std::mutex          mu_;
    std::condition_variable     not_empty_;
    std::condition_variable     not_full_;
    std::deque<T>               queue_;
    bool                        closed_ { false };
};

}  // namespace cd::concurrency
