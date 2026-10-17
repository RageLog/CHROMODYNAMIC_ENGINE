// =============================================================================
// CHROMODYNAMIC — cd/world_container/Project.hpp
// gap #18 foundation — Project = user's editable artefact, owns
// settings + asset catalog + level list.
// =============================================================================
#pragma once

#include <cd/world_container/Level.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cd::world_container
{

/// Render / audio / network defaults. Each field can be overridden
/// per Level (renderer params) and per Layer (postfx) — that
/// override chain is the editor's job to walk; this struct just
/// defines the canonical defaults.
struct ProjectSettings
{
    bool         enable_csm        { true };
    bool         enable_rt_shadows { true };
    bool         enable_bloom      { false };
    bool         enable_gtao       { false };
    bool         enable_ssr        { false };
    std::uint8_t tonemap_op        { 2 };       // 2 = Hable (matches hello_engine default)
    float        master_volume     { 1.0F };
    std::string  default_transport { "udp" };
};

class Project
{
public:
    Project() = default;
    explicit Project(std::string name) : name_(std::move(name)) {}

    [[nodiscard]] std::string_view name() const noexcept { return name_; }
    void set_name(std::string n) { name_ = std::move(n); }

    [[nodiscard]] ProjectSettings&       settings()       noexcept { return settings_; }
    [[nodiscard]] const ProjectSettings& settings() const noexcept { return settings_; }

    [[nodiscard]] std::size_t level_count() const noexcept { return levels_.size(); }

    Level*       add_level(std::string name)
    {
        levels_.push_back(std::make_unique<Level>(std::move(name)));
        return levels_.back().get();
    }

    bool         remove_level(std::size_t i)
    {
        if (i >= levels_.size()) return false;
        levels_.erase(levels_.begin() + static_cast<std::ptrdiff_t>(i));
        if (levels_.empty())
        {
            active_level_ = 0;
        }
        else if (i < active_level_)
        {
            --active_level_;
        }
        else if (active_level_ >= levels_.size())
        {
            active_level_ = levels_.size() - 1;
        }
        return true;
    }

    Level*       level(std::size_t i)       noexcept { return i < levels_.size() ? levels_[i].get() : nullptr; }
    const Level* level(std::size_t i) const noexcept { return i < levels_.size() ? levels_[i].get() : nullptr; }

    [[nodiscard]] Level* find_level(std::string_view n) noexcept
    {
        for (auto& l : levels_)
            if (l && l->name() == n) return l.get();
        return nullptr;
    }

    [[nodiscard]] const Level* find_level(std::string_view n) const noexcept
    {
        for (const auto& l : levels_)
            if (l && l->name() == n) return l.get();
        return nullptr;
    }

    [[nodiscard]] std::size_t active_level() const noexcept { return active_level_; }
    void set_active_level(std::size_t i) noexcept
    {
        if (i < levels_.size()) active_level_ = i;
    }
    bool set_active_level(std::string_view n) noexcept
    {
        for (std::size_t i = 0; i < levels_.size(); ++i)
        {
            if (levels_[i] && levels_[i]->name() == n)
            {
                active_level_ = i;
                return true;
            }
        }
        return false;
    }

    /// Returns distinct logical world names across all levels in order of appearance.
    [[nodiscard]] std::vector<std::string> world_names() const
    {
        std::vector<std::string> out;
        for (const auto& l : levels_)
        {
            if (!l) continue;
            const std::string_view wn = l->world_name();
            bool found = false;
            for (const auto& existing : out)
            {
                if (existing == wn) { found = true; break; }
            }
            if (!found) out.emplace_back(wn);
        }
        return out;
    }

    /// Returns indices of all levels belonging to `world_name`.
    [[nodiscard]] std::vector<std::size_t> levels_in_world(std::string_view world_name) const
    {
        std::vector<std::size_t> out;
        for (std::size_t i = 0; i < levels_.size(); ++i)
        {
            if (levels_[i] && levels_[i]->world_name() == world_name)
                out.push_back(i);
        }
        return out;
    }

private:
    std::string                          name_         { "Untitled Project" };
    ProjectSettings                      settings_     {};
    std::vector<std::unique_ptr<Level>>  levels_;
    std::size_t                          active_level_ { 0 };
};

}  // namespace cd::world_container
