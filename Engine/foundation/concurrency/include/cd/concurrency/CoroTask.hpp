// =============================================================================
// CHROMODYNAMIC — cd/concurrency/CoroTask.hpp
// ADR-015 + ADR-017 P1 (DfH common/coroutine/corotask.hpp salvage)
//
// C++20 coroutine task with optional return value. Two flavours:
//   - cd::concurrency::CoroTask        — void-returning detached root
//   - cd::concurrency::Task<T>         — value-returning awaitable
//
// Both promise types carry a std::stop_token so cooperative cancellation works
// out of the box (matches DfH ADR-017 P1 pattern; the awaiters in Awaiters.hpp
// detect this member via a concept).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <coroutine>
#include <exception>
#include <functional>
#include <stop_token>
#include <utility>
#include <variant>

namespace cd::concurrency
{

// --- CoroTask (void, detached) ----------------------------------------------

class CoroTask
{
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type
    {
        std::stop_token stop_token {};
        std::function<void(std::exception_ptr)> on_complete {};
        std::exception_ptr exception {};

        [[nodiscard]] CoroTask get_return_object() noexcept
        {
            return CoroTask { handle_type::from_promise(*this) };
        }

        [[nodiscard]] std::suspend_always initial_suspend() const noexcept
        {
            return {};
        }

        struct FinalAwaiter
        {
            [[nodiscard]] bool await_ready() const noexcept
            {
                return false;
            }

            template <class P>
            void await_suspend(std::coroutine_handle<P> h) const noexcept
            {
                if (h.promise().on_complete)
                {
                    h.promise().on_complete(h.promise().exception);
                }
            }

            void await_resume() const noexcept
            {
            }
        };

        [[nodiscard]] FinalAwaiter final_suspend() const noexcept
        {
            return {};
        }

        void return_void() const noexcept
        {
        }

        void unhandled_exception() noexcept
        {
            exception = std::current_exception();
        }
    };

    CoroTask() noexcept = default;

    explicit CoroTask(handle_type h) noexcept
        : handle_ { h }
    {
    }

    CoroTask(CoroTask&& other) noexcept
        : handle_ { std::exchange(other.handle_, {}) }
    {
    }

    CoroTask& operator=(CoroTask&& other) noexcept
    {
        if (this != &other)
        {
            destroy();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    CoroTask(const CoroTask&) = delete;
    CoroTask& operator=(const CoroTask&) = delete;

    ~CoroTask()
    {
        destroy();
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return static_cast<bool>(handle_);
    }

    explicit operator bool() const noexcept
    {
        return valid();
    }

    [[nodiscard]] handle_type release() noexcept
    {
        return std::exchange(handle_, {});
    }

    [[nodiscard]] handle_type get() const noexcept
    {
        return handle_;
    }

private:
    void destroy() noexcept
    {
        if (handle_)
        {
            handle_.destroy();
            handle_ = {};
        }
    }

    handle_type handle_ {};
};

// --- Task<T> (value-returning awaitable) ------------------------------------
// Awaiting it suspends the caller until the inner coroutine completes; the
// resulting value is propagated (or exception rethrown).

template <class T>
class Task
{
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type
    {
        std::stop_token stop_token {};
        std::coroutine_handle<> continuation {};
        std::variant<std::monostate, T, std::exception_ptr> result {};

        [[nodiscard]] Task get_return_object() noexcept
        {
            return Task { handle_type::from_promise(*this) };
        }

        [[nodiscard]] std::suspend_always initial_suspend() const noexcept
        {
            return {};
        }

        struct FinalAwaiter
        {
            [[nodiscard]] bool await_ready() const noexcept
            {
                return false;
            }

            template <class P>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<P> h) const noexcept
            {
                auto& promise = h.promise();
                if (promise.continuation)
                {
                    return promise.continuation;
                }
                return std::noop_coroutine();
            }

            void await_resume() const noexcept
            {
            }
        };

        [[nodiscard]] FinalAwaiter final_suspend() const noexcept
        {
            return {};
        }

        template <class U>
        void return_value(U&& v)
        {
            result.template emplace<1>(std::forward<U>(v));
        }

        void unhandled_exception() noexcept
        {
            result.template emplace<2>(std::current_exception());
        }
    };

    Task() noexcept = default;

    explicit Task(handle_type h) noexcept
        : handle_ { h }
    {
    }

    Task(Task&& other) noexcept
        : handle_ { std::exchange(other.handle_, {}) }
    {
    }

    Task& operator=(Task&& other) noexcept
    {
        if (this != &other)
        {
            destroy();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    ~Task()
    {
        destroy();
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return static_cast<bool>(handle_);
    }

    explicit operator bool() const noexcept
    {
        return valid();
    }

    // Co_await support: chain into another coroutine.
    struct Awaiter
    {
        handle_type child;

        [[nodiscard]] bool await_ready() const noexcept
        {
            return child.done();
        }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> parent) noexcept
        {
            child.promise().continuation = parent;
            return child;
        }

        T await_resume()
        {
            auto& res = child.promise().result;
            if (res.index() == 2)
            {
                std::rethrow_exception(std::get<2>(res));
            }
            return std::move(std::get<1>(res));
        }
    };

    [[nodiscard]] Awaiter operator co_await() && noexcept
    {
        return Awaiter { handle_ };
    }

    /// Drive the coroutine synchronously to completion (caller blocks).
    /// Useful in test code and from non-coroutine contexts.
    T get()
    {
        if (!handle_.done())
        {
            handle_.resume();
        }
        auto& res = handle_.promise().result;
        if (res.index() == 2)
        {
            std::rethrow_exception(std::get<2>(res));
        }
        return std::move(std::get<1>(res));
    }

    [[nodiscard]] handle_type get_handle() const noexcept
    {
        return handle_;
    }

private:
    void destroy() noexcept
    {
        if (handle_)
        {
            handle_.destroy();
            handle_ = {};
        }
    }

    handle_type handle_ {};
};

}  // namespace cd::concurrency
