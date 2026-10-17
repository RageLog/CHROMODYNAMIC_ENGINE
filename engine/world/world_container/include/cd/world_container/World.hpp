// =============================================================================
// CHROMODYNAMIC — cd/world_container/World.hpp
// gap #18 foundation — World = running container, owns the active
// Project. One World instance per process.
// =============================================================================
#pragma once

#include <cd/world_container/Project.hpp>

#include <memory>
#include <string>
#include <string_view>

namespace cd::world_container
{

class World
{
public:
    World() = default;

    void set_project(std::unique_ptr<Project> p) noexcept
    {
        project_ = std::move(p);
    }

    [[nodiscard]] Project*       project()       noexcept { return project_.get(); }
    [[nodiscard]] const Project* project() const noexcept { return project_.get(); }

    [[nodiscard]] std::string_view name() const noexcept { return name_; }
    void set_name(std::string n) { name_ = std::move(n); }

    [[nodiscard]] Level* active_level_ptr() noexcept
    {
        return project_ ? project_->level(project_->active_level()) : nullptr;
    }
    [[nodiscard]] const Level* active_level_ptr() const noexcept
    {
        return project_ ? project_->level(project_->active_level()) : nullptr;
    }

private:
    std::unique_ptr<Project> project_;
    std::string              name_ { "Untitled World" };
};

}  // namespace cd::world_container
