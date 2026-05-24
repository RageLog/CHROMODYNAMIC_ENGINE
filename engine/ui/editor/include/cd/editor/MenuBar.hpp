// =============================================================================
// CHROMODYNAMIC — cd/editor/MenuBar.hpp
// Phase 82.A / Wave 250 — editor menu-bar definition tree.
//
// Top-bar menus ("File", "Edit", "View", "Help") are nested trees of
// MenuItems. Each leaf carries a callback; submenus carry a list of
// children.
//
//   editor::MenuBar bar;
//   auto& file = bar.add_menu("File");
//   file.add_item("Open",    [] { open_dialog(); });
//   file.add_item("Save",    [] { save_scene(); });
//   file.add_separator();
//   file.add_item("Exit",    [] { quit(); });
//
// Renderer (ImGui) walks the tree and emits BeginMenu / MenuItem
// calls. This primitive is headless — pure data.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cd::editor
{

struct MenuItem
{
    enum class Kind : std::uint8_t { kAction, kSeparator, kSubmenu };

    std::string                          label;
    Kind                                 kind { Kind::kAction };
    std::function<void()>                action;
    std::vector<std::unique_ptr<MenuItem>> children;
};

class Menu
{
public:
    explicit Menu(MenuItem* node) : node_ { node } {}

    void add_item(std::string label, std::function<void()> action)
    {
        auto child = std::make_unique<MenuItem>();
        child->label = std::move(label);
        child->kind = MenuItem::Kind::kAction;
        child->action = std::move(action);
        node_->children.push_back(std::move(child));
    }

    void add_separator()
    {
        auto child = std::make_unique<MenuItem>();
        child->kind = MenuItem::Kind::kSeparator;
        node_->children.push_back(std::move(child));
    }

    Menu add_submenu(std::string label)
    {
        auto child = std::make_unique<MenuItem>();
        child->label = std::move(label);
        child->kind = MenuItem::Kind::kSubmenu;
        auto* raw = child.get();
        node_->children.push_back(std::move(child));
        return Menu { raw };
    }

private:
    MenuItem* node_;
};

class MenuBar
{
public:
    Menu add_menu(std::string label)
    {
        auto top = std::make_unique<MenuItem>();
        top->label = std::move(label);
        top->kind = MenuItem::Kind::kSubmenu;
        auto* raw = top.get();
        roots_.push_back(std::move(top));
        return Menu { raw };
    }

    [[nodiscard]] const std::vector<std::unique_ptr<MenuItem>>& menus() const noexcept
    {
        return roots_;
    }

    [[nodiscard]] std::size_t size() const noexcept { return roots_.size(); }

    void clear() noexcept { roots_.clear(); }

private:
    std::vector<std::unique_ptr<MenuItem>> roots_;
};

}  // namespace cd::editor
