// =============================================================================
// CHROMODYNAMIC — cd/input/Bindings.hpp
// Phase 71.A / Wave 239 — action → input source binding map.
//
// `ActionBindings` maps **action names** (uint32 hash) to one or more
// physical input sources (KeyCode + optional Modifier). Game code asks
// "is the Jump action down?" and the binding map answers based on the
// current InputState.
//
// Bind multiple keys to the same action ("MoveLeft" ← KeyCode::kA OR
// KeyCode::kLeft). Hash-based action name keeps the runtime fast +
// serializable.
//
// Pairs with `KeyChord` (Phase 45, exact-modifier match for editor
// shortcuts). Bindings is for sustained game-loop checks (held keys);
// KeyChord is for event-on-press.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/input/Input.hpp>

#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cd::input
{

[[nodiscard]] constexpr std::uint32_t action_hash(std::string_view name) noexcept
{
    std::uint32_t h = 0x811C9DC5u;
    for (char c : name)
    {
        h ^= static_cast<std::uint32_t>(static_cast<unsigned char>(c));
        h *= 0x01000193u;
    }
    return h;
}

class ActionBindings
{
public:
    void bind(std::string_view action, KeyCode key)
    {
        bindings_[action_hash(action)].push_back(key);
    }

    void unbind_all(std::string_view action)
    {
        bindings_.erase(action_hash(action));
    }

    /// True iff *any* key bound to `action` is currently held in `s`.
    [[nodiscard]] bool is_down(std::string_view action, const InputState& s) const noexcept
    {
        auto it = bindings_.find(action_hash(action));
        if (it == bindings_.end()) return false;
        for (KeyCode k : it->second)
            if (s.is_key_down(k)) return true;
        return false;
    }

    [[nodiscard]] std::size_t action_count() const noexcept { return bindings_.size(); }

    void clear() noexcept { bindings_.clear(); }

private:
    std::unordered_map<std::uint32_t, std::vector<KeyCode>> bindings_;
};

}  // namespace cd::input
