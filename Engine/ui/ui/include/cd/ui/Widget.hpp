// =============================================================================
// CHROMODYNAMIC — cd/ui/Widget.hpp
// Phase 5 / Sprint S5.1 — retained-mode widget tree.
//
// `Widget` is the abstract base of the UI tree. Concrete widgets (Panel,
// Label, Button) inherit it. Children form a vector — RAII; the parent
// owns them via `std::unique_ptr`.
//
// Hit testing is a top-down depth-first walk: a click at (x, y) consults
// the topmost (last-added) child that contains the point. Layout is
// manual in this MVP (each widget gets an explicit `Rect`); flex / grid
// layout will plug in via a future Layout component without changing the
// widget surface.
//
// Rendering is decoupled: widgets expose their visible draw quads via
// `collect_draw_commands()` and a frontend (editor or app) consumes the
// resulting list. cd::ui does NOT depend on cd::rhi or cd::render.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace cd::ui
{

// ---- Geometry --------------------------------------------------------------

struct Rect
{
    float x { 0.0F }, y { 0.0F }, w { 0.0F }, h { 0.0F };

    [[nodiscard]] bool contains(float px, float py) const noexcept
    {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
};

struct Color
{
    float r { 1.0F }, g { 1.0F }, b { 1.0F }, a { 1.0F };
};

// ---- Draw command (renderer-agnostic) -------------------------------------

enum class DrawKind : std::uint8_t
{
    kRect,
    kText
};

struct DrawCommand
{
    DrawKind kind { DrawKind::kRect };
    Rect rect {};
    Color color {};
    /// For kText only. Borrowed view into the widget's own storage; valid
    /// until the widget itself is destroyed or its text mutated.
    std::string_view text {};
};

// ---- Widget base ---------------------------------------------------------

using WidgetId = std::uint32_t;

class Widget
{
public:
    Widget() noexcept = default;
    virtual ~Widget() = default;
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;
    Widget(Widget&&) = delete;
    Widget& operator=(Widget&&) = delete;

    // ---- Identity / layout ---------------------------------------------

    [[nodiscard]] WidgetId id() const noexcept
    {
        return id_;
    }

    void set_id(WidgetId v) noexcept
    {
        id_ = v;
    }

    [[nodiscard]] const Rect& bounds() const noexcept
    {
        return bounds_;
    }

    void set_bounds(Rect r) noexcept
    {
        bounds_ = r;
    }

    [[nodiscard]] bool visible() const noexcept
    {
        return visible_;
    }

    void set_visible(bool v) noexcept
    {
        visible_ = v;
    }

    // ---- Tree management -----------------------------------------------

    /// Adopt a child. Returns a non-owning observer pointer for chaining.
    template <class W, class... Args>
    W* add_child(Args&&... args)
    {
        auto child = std::make_unique<W>(std::forward<Args>(args)...);
        auto* raw = child.get();
        children_.push_back(std::move(child));
        return raw;
    }

    [[nodiscard]] std::span<const std::unique_ptr<Widget>> children() const noexcept
    {
        return { children_.data(), children_.size() };
    }

    [[nodiscard]] std::size_t child_count() const noexcept
    {
        return children_.size();
    }

    /// Detach and destroy every child. Useful for retained-mode widgets
    /// that rebuild their visual tree on `refresh()` (SceneTreeView etc.).
    void clear_children() noexcept
    {
        children_.clear();
    }

    /// Detach a specific child by raw pointer. Returns true if a match was
    /// found and removed; false otherwise. The destroyed unique_ptr drops
    /// the widget, which recursively destroys its own subtree.
    bool remove_child(Widget* w) noexcept
    {
        for (auto it = children_.begin(); it != children_.end(); ++it)
        {
            if (it->get() == w)
            {
                children_.erase(it);
                return true;
            }
        }
        return false;
    }

    // ---- Hit testing / events ------------------------------------------

    /// Top-down hit test. `(px, py)` is in *this widget's parent's*
    /// coordinate frame (i.e. the bounds rect uses the same space). The
    /// recursion translates the query into each child's parent-space
    /// before descending, so the user only ever supplies one consistent
    /// frame at the top of the tree. Returns the deepest visible widget
    /// under the point or `nullptr` when no widget contains it.
    [[nodiscard]] Widget* hit_test(float px, float py) noexcept;

    /// Notify the widget that a click happened at (px, py). Coordinates
    /// follow the same convention as `hit_test`.
    void dispatch_click(float px, float py) noexcept;

    // ---- Drawing -------------------------------------------------------

    /// Walk the tree and append draw commands. Output rects are in the
    /// *root* widget's coordinate frame (the frontend treats them as
    /// screen-space — call this on the root and the renderer can blit
    /// the resulting list verbatim).
    void collect_draw_commands(std::vector<DrawCommand>& out) const
    {
        collect_draw_commands_(out, 0.0F, 0.0F);
    }

protected:
    /// Concrete widgets emit their own quads here. `(ox, oy)` is the
    /// absolute origin of *this widget's parent* in the root frame; the
    /// widget composes `(ox + bounds.x, oy + bounds.y)` to land at its
    /// own absolute origin. The default is a no-op (Panel-as-container
    /// draws nothing on its own).
    virtual void emit_draw_(std::vector<DrawCommand>& /*out*/, float /*ox*/, float /*oy*/) const
    {
    }

    /// Concrete click handler. Default no-op; Button overrides to fire its
    /// callback.
    virtual void on_click_() noexcept
    {
    }

private:
    /// Recursive draw collection. `(ox, oy)` is the absolute origin of
    /// this widget's *parent* in the root frame; the widget composes its
    /// own absolute origin from this plus `bounds_.x/.y`.
    void collect_draw_commands_(std::vector<DrawCommand>& out, float ox, float oy) const;

    WidgetId id_ { 0 };
    Rect bounds_ {};
    bool visible_ { true };
    std::vector<std::unique_ptr<Widget>> children_ {};
};

// ---- Concrete widgets ----------------------------------------------------

class Panel : public Widget
{
public:
    Panel() noexcept = default;

    explicit Panel(Color background) noexcept
        : background_ { background }
    {
    }

    void set_background(Color c) noexcept
    {
        background_ = c;
    }

    [[nodiscard]] Color background() const noexcept
    {
        return background_;
    }

protected:
    void emit_draw_(std::vector<DrawCommand>& out, float ox, float oy) const override
    {
        DrawCommand cmd {};
        cmd.kind = DrawKind::kRect;
        cmd.rect = { ox + bounds().x, oy + bounds().y, bounds().w, bounds().h };
        cmd.color = background_;
        out.push_back(cmd);
    }

private:
    Color background_ { 0.15F, 0.15F, 0.15F, 1.0F };
};

class Label : public Widget
{
public:
    Label() noexcept = default;

    explicit Label(std::string text, Color color = { 1.0F, 1.0F, 1.0F, 1.0F }) noexcept
        : text_ { std::move(text) }
        , color_ { color }
    {
    }

    void set_text(std::string t) noexcept
    {
        text_ = std::move(t);
    }

    [[nodiscard]] const std::string& text() const noexcept
    {
        return text_;
    }

    void set_color(Color c) noexcept
    {
        color_ = c;
    }

    [[nodiscard]] Color color() const noexcept
    {
        return color_;
    }

protected:
    void emit_draw_(std::vector<DrawCommand>& out, float ox, float oy) const override
    {
        DrawCommand cmd {};
        cmd.kind = DrawKind::kText;
        cmd.rect = { ox + bounds().x, oy + bounds().y, bounds().w, bounds().h };
        cmd.color = color_;
        cmd.text = text_;
        out.push_back(cmd);
    }

private:
    std::string text_ {};
    Color color_ { 1.0F, 1.0F, 1.0F, 1.0F };
};

class Button : public Widget
{
public:
    using OnClickFn = std::function<void()>;

    Button() noexcept = default;

    explicit Button(std::string label) noexcept
        : label_ { std::move(label) }
    {
    }

    void set_label(std::string l) noexcept
    {
        label_ = std::move(l);
    }

    [[nodiscard]] const std::string& label() const noexcept
    {
        return label_;
    }

    void set_on_click(OnClickFn fn) noexcept
    {
        on_click_fn_ = std::move(fn);
    }

    [[nodiscard]] std::size_t click_count() const noexcept
    {
        return click_count_;
    }

    void set_background(Color c) noexcept
    {
        background_ = c;
    }

    void set_text_color(Color c) noexcept
    {
        text_color_ = c;
    }

protected:
    void emit_draw_(std::vector<DrawCommand>& out, float ox, float oy) const override
    {
        const Rect abs { ox + bounds().x, oy + bounds().y, bounds().w, bounds().h };
        DrawCommand bg {};
        bg.kind = DrawKind::kRect;
        bg.rect = abs;
        bg.color = background_;
        out.push_back(bg);
        if (!label_.empty())
        {
            DrawCommand txt {};
            txt.kind = DrawKind::kText;
            txt.rect = abs;
            txt.color = text_color_;
            txt.text = label_;
            out.push_back(txt);
        }
    }

    void on_click_() noexcept override
    {
        ++click_count_;
        if (on_click_fn_)
            on_click_fn_();
    }

private:
    std::string label_ {};
    Color background_ { 0.25F, 0.25F, 0.35F, 1.0F };
    Color text_color_ { 1.0F, 1.0F, 1.0F, 1.0F };
    OnClickFn on_click_fn_ {};
    std::size_t click_count_ { 0 };
};

}  // namespace cd::ui
