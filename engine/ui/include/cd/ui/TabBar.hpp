// =============================================================================
// CHROMODYNAMIC — cd/ui/TabBar.hpp
// Phase 85.A / Wave 253 — tab-bar selection state.
//
// Headless tab-bar: an ordered list of `Tab { id, label }` + the
// currently active index. The renderer reads the list, the caller
// calls `set_active(id)` on click, `next()` / `prev()` on keyboard
// shortcut.
//
// `id` is a caller-defined uint32 (often hash of the scene file name
// or some stable token); `label` is the display string.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace cd::ui
{

struct Tab
{
    std::uint32_t id { 0 };
    std::string   label;
};

class TabBar
{
public:
    void add(std::uint32_t id, std::string label)
    {
        tabs_.push_back(Tab { id, std::move(label) });
        if (tabs_.size() == 1) active_ = 0;
    }

    bool close(std::uint32_t id)
    {
        for (auto it = tabs_.begin(); it != tabs_.end(); ++it)
        {
            if (it->id == id)
            {
                const auto idx = static_cast<std::size_t>(it - tabs_.begin());
                tabs_.erase(it);
                if (tabs_.empty()) { active_ = 0; return true; }
                if (idx <= active_ && active_ > 0) --active_;
                if (active_ >= tabs_.size()) active_ = tabs_.size() - 1;
                return true;
            }
        }
        return false;
    }

    bool set_active(std::uint32_t id) noexcept
    {
        for (std::size_t i = 0; i < tabs_.size(); ++i)
        {
            if (tabs_[i].id == id) { active_ = i; return true; }
        }
        return false;
    }

    void next() noexcept
    {
        if (!tabs_.empty()) active_ = (active_ + 1) % tabs_.size();
    }

    void prev() noexcept
    {
        if (!tabs_.empty()) active_ = (active_ + tabs_.size() - 1) % tabs_.size();
    }

    [[nodiscard]] std::size_t  size()          const noexcept { return tabs_.size(); }
    [[nodiscard]] std::size_t  active_index()  const noexcept { return active_; }
    [[nodiscard]] const Tab*   active()        const noexcept
    {
        return (active_ < tabs_.size()) ? &tabs_[active_] : nullptr;
    }
    [[nodiscard]] const std::vector<Tab>& tabs() const noexcept { return tabs_; }

    void clear() noexcept { tabs_.clear(); active_ = 0; }

private:
    std::vector<Tab> tabs_;
    std::size_t      active_ { 0 };
};

}  // namespace cd::ui
