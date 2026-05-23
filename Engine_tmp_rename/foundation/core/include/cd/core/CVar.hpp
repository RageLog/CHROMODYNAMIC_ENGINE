// =============================================================================
// CHROMODYNAMIC — cd/core/CVar.hpp
// ADR-017 P2 (DfH common/parameter/parameterregistry.hpp salvage, distilled)
//
// Runtime-tunable, typed configuration variables ("CVars"). Subsystems
// register their tunables once; tools / overlays / config files mutate them
// at runtime. Each CVar carries a unique string key, a value of bounded type,
// and an on-change callback list.
//
// Concurrency contract: every Get/Set is internally synchronized (shared_mutex).
// Hot-path reads should cache the snapshot when high call rate matters.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace cd::core
{

/// Permitted CVar value types.
using CVarValue = std::variant<bool, std::int64_t, double, std::string>;

class CVarRegistry
{
public:
    using ChangeCallback = std::function<void(std::string_view key, const CVarValue& new_value)>;
    using CallbackId = std::uint64_t;

    CVarRegistry() noexcept = default;
    ~CVarRegistry() = default;
    CVarRegistry(const CVarRegistry&) = delete;
    CVarRegistry& operator=(const CVarRegistry&) = delete;
    CVarRegistry(CVarRegistry&&) = delete;
    CVarRegistry& operator=(CVarRegistry&&) = delete;

    /// Register a new CVar or replace its current value.
    void set(std::string key, CVarValue value)
    {
        std::vector<std::pair<CallbackId, ChangeCallback>> callbacks_snapshot;
        {
            std::unique_lock guard { mutex_ };
            auto& slot = vars_[key];
            slot.value = std::move(value);
            callbacks_snapshot = slot.callbacks;  // copy under lock
        }
        // Fire callbacks outside the lock to avoid re-entrancy deadlocks.
        auto it = vars_.find(key);
        if (it != vars_.end())
        {
            for (auto& [_, cb] : callbacks_snapshot)
            {
                if (cb)
                    cb(key, it->second.value);
            }
        }
    }

    /// Get the current value of a CVar, or nullopt if not registered.
    [[nodiscard]] std::optional<CVarValue> get(std::string_view key) const
    {
        std::shared_lock guard { mutex_ };
        auto it = vars_.find(std::string { key });
        if (it == vars_.end())
        {
            return std::nullopt;
        }
        return it->second.value;
    }

    template <class T>
    [[nodiscard]] std::optional<T> get_as(std::string_view key) const
    {
        auto v = get(key);
        if (!v)
            return std::nullopt;
        if (auto* p = std::get_if<T>(&*v))
            return *p;
        return std::nullopt;
    }

    [[nodiscard]] bool contains(std::string_view key) const
    {
        std::shared_lock guard { mutex_ };
        return vars_.find(std::string { key }) != vars_.end();
    }

    void erase(std::string_view key)
    {
        std::unique_lock guard { mutex_ };
        vars_.erase(std::string { key });
    }

    [[nodiscard]] std::size_t size() const
    {
        std::shared_lock guard { mutex_ };
        return vars_.size();
    }

    /// Take a consistent snapshot of all (key, value) pairs. Order is unspecified
    /// because the registry is hash-based. Callers that need a stable order
    /// (e.g. persistence) should sort before writing.
    [[nodiscard]] std::vector<std::pair<std::string, CVarValue>> snapshot() const
    {
        std::shared_lock guard { mutex_ };
        std::vector<std::pair<std::string, CVarValue>> out;
        out.reserve(vars_.size());
        for (const auto& [k, e] : vars_)
            out.emplace_back(k, e.value);
        return out;
    }

    /// Subscribe to value changes for `key`. Returns a callback id you can pass
    /// to `unsubscribe()`. The callback runs on the thread that called `set()`.
    CallbackId subscribe(std::string_view key, ChangeCallback cb)
    {
        std::unique_lock guard { mutex_ };
        auto id = ++next_callback_id_;
        vars_[std::string { key }].callbacks.emplace_back(id, std::move(cb));
        return id;
    }

    void unsubscribe(std::string_view key, CallbackId id)
    {
        std::unique_lock guard { mutex_ };
        auto it = vars_.find(std::string { key });
        if (it == vars_.end())
            return;
        auto& cbs = it->second.callbacks;
        std::erase_if(
            cbs,
            [id](const auto& p)
            {
                return p.first == id;
            }
        );
    }

private:
    struct Entry
    {
        CVarValue value;
        std::vector<std::pair<CallbackId, ChangeCallback>> callbacks;
    };

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, Entry> vars_;
    CallbackId next_callback_id_ { 0 };
};

}  // namespace cd::core
