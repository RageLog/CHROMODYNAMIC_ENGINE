// =============================================================================
// CHROMODYNAMIC — cd/editor/CommandPalette.hpp
// Phase 31.A / Wave 199 — fuzzy command palette state.
//
// Headless model for a VS Code / Sublime style command palette. The
// palette owns a sorted set of `Entry { id, label }` and exposes:
//
//   * register_command(id, label)     — additive registry.
//   * filter(query)                   — returns visible entries
//                                        scored by case-insensitive
//                                        subsequence match.
//   * invoke(index)                   — fires the stored callback
//                                        for the filtered list's
//                                        entry at `index`.
//
// Scoring is the classic FZF subsequence: every query character must
// appear in the label in order; consecutive matches are weighted
// higher; matches at the start of a word boost score. We do not
// implement Smith-Waterman — fast typing on hundreds of commands
// matters more than perfect ranking.
//
// Rendering is the caller's job (ImGui, terminal UI, custom). The
// palette is rendering-agnostic so it can drive the editor *or* a
// CLI tool with the same registry.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace cd::editor
{

struct PaletteEntry
{
    std::uint32_t id { 0 };
    std::string   label;
    std::function<void()> action;
};

class CommandPalette
{
public:
    /// Register a command. `id` must be unique across the palette;
    /// re-registering an existing id overwrites label + action.
    void register_command(std::uint32_t id, std::string label, std::function<void()> action)
    {
        for (auto& e : entries_)
        {
            if (e.id == id)
            {
                e.label = std::move(label);
                e.action = std::move(action);
                return;
            }
        }
        entries_.push_back(PaletteEntry { id, std::move(label), std::move(action) });
    }

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    /// Returns indices into `entries_` for matches against `query`,
    /// ordered by descending score. Empty `query` returns every entry
    /// in registration order.
    [[nodiscard]] std::vector<std::size_t> filter(const std::string& query) const
    {
        std::vector<std::pair<int, std::size_t>> scored;
        if (query.empty())
        {
            std::vector<std::size_t> all;
            all.reserve(entries_.size());
            for (std::size_t i = 0; i < entries_.size(); ++i) all.push_back(i);
            return all;
        }
        for (std::size_t i = 0; i < entries_.size(); ++i)
        {
            const int s = score(entries_[i].label, query);
            if (s > 0) scored.emplace_back(s, i);
        }
        // Descending score; stable on index for determinism.
        std::sort(scored.begin(), scored.end(),
                  [](const auto& a, const auto& b)
                  {
                      if (a.first != b.first) return a.first > b.first;
                      return a.second < b.second;
                  });
        std::vector<std::size_t> out;
        out.reserve(scored.size());
        for (auto& p : scored) out.push_back(p.second);
        return out;
    }

    /// Invoke entry at flat `palette_index` (NOT a filter index — use
    /// `filter()[k]` to translate). Returns true if invoked.
    bool invoke(std::size_t palette_index) const
    {
        if (palette_index >= entries_.size()) return false;
        const auto& e = entries_[palette_index];
        if (!e.action) return false;
        e.action();
        return true;
    }

    [[nodiscard]] const PaletteEntry& at(std::size_t i) const noexcept { return entries_[i]; }

private:
    [[nodiscard]] static int score(const std::string& label, const std::string& query) noexcept
    {
        int s = 0;
        std::size_t qi = 0;
        bool prev_match = false;
        bool boundary = true;  // start-of-word boost on next match
        for (std::size_t li = 0; li < label.size() && qi < query.size(); ++li)
        {
            const char lc = to_lower(label[li]);
            const char qc = to_lower(query[qi]);
            if (lc == qc)
            {
                s += 1;
                if (prev_match) s += 2;
                if (boundary) s += 3;
                prev_match = true;
                ++qi;
            }
            else
            {
                prev_match = false;
            }
            boundary = (label[li] == ' ' || label[li] == '_' || label[li] == '-');
        }
        return (qi == query.size()) ? s : 0;
    }

    [[nodiscard]] static char to_lower(char c) noexcept
    {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

    std::vector<PaletteEntry> entries_;
};

}  // namespace cd::editor
