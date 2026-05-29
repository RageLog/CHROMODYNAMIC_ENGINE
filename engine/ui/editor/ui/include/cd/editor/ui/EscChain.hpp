// =============================================================================
// CHROMODYNAMIC — cd/editor/ui/EscChain.hpp
// Production editor — ESC priority chain (Lessons §P3).
//
// Library-grade extraction of the ESC handler that hello_engine
// learned the hard way: cancel intent first, never quit. Consumers
// register handlers per priority level; the chain invokes them in
// order, stopping at the first one that returns true ("I consumed
// the ESC").
// =============================================================================
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

namespace cd::editor::ui
{

enum class EscPriority : std::uint8_t
{
    kActiveDrag    = 0,  ///< higher priority = lower number
    kModalDialog   = 1,
    kPalette       = 2,
    kSelection     = 3,
    kBackgroundFx  = 4,  ///< e.g. cancel a noisy animation preview
};

/// Handler returns true when it consumed the ESC (chain stops).
using EscHandler = std::function<bool()>;

class EscChain
{
public:
    void register_handler(EscPriority priority, EscHandler handler)
    {
        handlers_.push_back({ priority, std::move(handler) });
        std::sort(handlers_.begin(), handlers_.end(),
                  [](const Entry& a, const Entry& b) {
                      return static_cast<std::uint8_t>(a.priority) <
                             static_cast<std::uint8_t>(b.priority);
                  });
    }

    /// Returns true if any handler consumed the ESC (which it should
    /// always, since the production rule is "ESC never quits — quit
    /// is a menu / close-button action only").
    [[nodiscard]] bool handle() const
    {
        for (const auto& e : handlers_)
            if (e.handler && e.handler()) return true;
        return false;
    }

    [[nodiscard]] std::size_t handler_count() const noexcept
    {
        return handlers_.size();
    }

private:
    struct Entry { EscPriority priority; EscHandler handler; };
    std::vector<Entry> handlers_;
};

}  // namespace cd::editor::ui
