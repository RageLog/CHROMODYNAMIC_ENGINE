// =============================================================================
// CHROMODYNAMIC — cd/ui/ContextMenu.hpp
// Phase 93.B / Wave 261 — right-click popup menu state.
//
// `ContextMenu` stores the (label, callback) list + screen position +
// visibility flag for a right-click popup. Renderer reads `is_open()`,
// `position()`, and `items()` to draw; caller invokes `open(x, y)` /
// `close()` from input events.
//
// Distinct from `MenuBar` (Phase 83, top-bar nested menus). This one
// is a flat list, opens at a transient screen position, closes on
// any click outside.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <functional>
#include <string>
#include <vector>

namespace cd::ui
{

struct ContextMenuItem
{
    std::string            label;
    std::function<void()>  action;
    bool                   enabled { true };
};

class ContextMenu
{
public:
    void add_item(std::string label, std::function<void()> action, bool enabled = true)
    {
        items_.push_back(ContextMenuItem { std::move(label), std::move(action), enabled });
    }

    void open(float x, float y) noexcept
    {
        x_ = x;
        y_ = y;
        open_ = true;
    }

    void close() noexcept { open_ = false; }

    bool invoke(std::size_t i)
    {
        if (i >= items_.size() || !items_[i].enabled) return false;
        if (items_[i].action) items_[i].action();
        close();
        return true;
    }

    [[nodiscard]] bool        is_open() const noexcept { return open_; }
    [[nodiscard]] float       x()       const noexcept { return x_; }
    [[nodiscard]] float       y()       const noexcept { return y_; }
    [[nodiscard]] std::size_t size()    const noexcept { return items_.size(); }
    [[nodiscard]] const std::vector<ContextMenuItem>& items() const noexcept { return items_; }

    void clear_items() noexcept { items_.clear(); close(); }

private:
    std::vector<ContextMenuItem> items_;
    float                        x_    { 0.0F };
    float                        y_    { 0.0F };
    bool                         open_ { false };
};

}  // namespace cd::ui
