// =============================================================================
// CHROMODYNAMIC — cd/editor/PreferencesStore.hpp
// Phase 88.A / Wave 256 — in-memory typed key-value editor prefs.
//
// Editor preferences (font_size, theme, last_scene, recent_files,
// docking layout) are typed key → value pairs. PreferencesStore is
// the headless container with `set / get / has / remove` for the
// four primitive types we need: bool, int, float, string.
//
// Disk persistence is the caller's job (JSON / binary blob via
// BinaryStream). This primitive only owns the in-memory map.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

namespace cd::editor
{

class PreferencesStore
{
public:
    using Value = std::variant<bool, std::int64_t, double, std::string>;

    void set(std::string key, Value v) { values_[std::move(key)] = std::move(v); }

    [[nodiscard]] bool has(const std::string& key) const noexcept
    {
        return values_.find(key) != values_.end();
    }

    [[nodiscard]] std::optional<bool> get_bool(const std::string& key) const
    {
        auto it = values_.find(key);
        if (it == values_.end()) return std::nullopt;
        if (const auto* p = std::get_if<bool>(&it->second)) return *p;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::int64_t> get_int(const std::string& key) const
    {
        auto it = values_.find(key);
        if (it == values_.end()) return std::nullopt;
        if (const auto* p = std::get_if<std::int64_t>(&it->second)) return *p;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<double> get_double(const std::string& key) const
    {
        auto it = values_.find(key);
        if (it == values_.end()) return std::nullopt;
        if (const auto* p = std::get_if<double>(&it->second)) return *p;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> get_string(const std::string& key) const
    {
        auto it = values_.find(key);
        if (it == values_.end()) return std::nullopt;
        if (const auto* p = std::get_if<std::string>(&it->second)) return *p;
        return std::nullopt;
    }

    void remove(const std::string& key) noexcept { values_.erase(key); }

    [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }

    void clear() noexcept { values_.clear(); }

private:
    std::unordered_map<std::string, Value> values_;
};

}  // namespace cd::editor
