// =============================================================================
// CHROMODYNAMIC — cd/editor/PropertyModel.hpp
// Inspector property model — typed field edit/commit/cancel lifecycle.
//
// The inspector renders a list of named properties (position.x, roughness,
// etc.). Each property has a runtime type, a current value, and an
// optional pending edit. The model decouples the typed data from the
// rendering layer (ImGui or DrawBatcher) and from the undo-redo stack.
//
// Typical usage:
//
//   PropertyModel model;
//   model.define("pos.x", 0.0F);           // register + init
//   model.define("roughness", 0.5F);
//
//   // Inspector frame loop:
//   float v = model.get<float>("pos.x").value_or(0.0F);
//   if (drag_changed) model.begin_edit("pos.x", new_value);
//   if (drag_released) {
//       auto result = model.commit("pos.x");
//       if (result.has_value())
//           history.push(make_translate(result->before, result->after));
//   }
//   if (esc_pressed) model.cancel("pos.x");
//
// Design notes:
//   * Headless and rendering-agnostic: no ImGui headers pulled in.
//   * Type-erased value storage (std::variant of the four common scalar
//     types + Vec3f). Extend the Value alias below for new types.
//   * "edit" tracks a pending value while the user drags. "commit"
//     finalises the pending value and returns a (before, after) pair.
//     "cancel" discards the pending value without changing the committed
//     value.
//   * All lookups return std::optional so OOB names are safe.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

namespace cd::editor
{

// ---------------------------------------------------------------------------
// Value type — the discriminated union of all property scalar types the
// inspector currently supports.  Extend here + in PropertyModel::set() /
// PropertyModel::get<T>() when new types are needed.
// ---------------------------------------------------------------------------
using PropertyValue = std::variant<
    bool,
    std::int64_t,
    double,
    float,
    cd::math::Vec3f>;

// ---------------------------------------------------------------------------
// CommitResult — returned by commit(); carries the before and after values
// so the caller can build an undo command without knowing the type.
// ---------------------------------------------------------------------------
struct CommitResult
{
    std::string    name;
    PropertyValue  before;
    PropertyValue  after;
};

// ---------------------------------------------------------------------------
// PropertyDef — per-property bookkeeping.
// ---------------------------------------------------------------------------
struct PropertyDef
{
    std::string    name;
    PropertyValue  committed;          ///< last-committed (canonical) value
    std::optional<PropertyValue> pending;  ///< in-flight edit (not yet committed)
};

// ---------------------------------------------------------------------------
// PropertyModel
// ---------------------------------------------------------------------------
class PropertyModel
{
public:
    // ---- Registration ------------------------------------------------------

    /// Register a property with an initial committed value.
    /// If the property already exists the initial value is IGNORED.
    template <class T>
    void define(std::string name, T initial)
    {
        auto [it, inserted] = defs_.try_emplace(name);
        if (inserted)
        {
            it->second.name      = name;
            it->second.committed = PropertyValue { initial };
        }
    }

    // ---- Committed value access --------------------------------------------

    /// Overwrite the committed value directly (e.g. after an undo/redo
    /// wires a command result back in). Clears any pending edit.
    template <class T>
    void set(const std::string& name, T value)
    {
        auto it = defs_.find(name);
        if (it == defs_.end()) return;
        it->second.committed = PropertyValue { value };
        it->second.pending.reset();
    }

    /// Read the committed value as type T. Returns nullopt when the
    /// property is unknown or holds a different type.
    template <class T>
    [[nodiscard]] std::optional<T> get(const std::string& name) const
    {
        auto it = defs_.find(name);
        if (it == defs_.end()) return std::nullopt;
        const auto* p = std::get_if<T>(&it->second.committed);
        return (p != nullptr) ? std::optional<T> { *p } : std::nullopt;
    }

    // ---- Pending-edit lifecycle --------------------------------------------

    /// Write a pending value while the user is still dragging. Does NOT
    /// commit. No-op for unknown property names.
    template <class T>
    void begin_edit(const std::string& name, T value)
    {
        auto it = defs_.find(name);
        if (it == defs_.end()) return;
        it->second.pending = PropertyValue { value };
    }

    /// Returns true when the property has an uncommitted pending edit.
    [[nodiscard]] bool has_pending(const std::string& name) const noexcept
    {
        auto it = defs_.find(name);
        if (it == defs_.end()) return false;
        return it->second.pending.has_value();
    }

    /// Read the pending value as type T (the live drag value, not
    /// necessarily the committed value). Returns nullopt when there is
    /// no pending edit or the property is unknown or differently typed.
    template <class T>
    [[nodiscard]] std::optional<T> get_pending(const std::string& name) const
    {
        auto it = defs_.find(name);
        if (it == defs_.end()) return std::nullopt;
        if (!it->second.pending.has_value()) return std::nullopt;
        const auto* p = std::get_if<T>(&*it->second.pending);
        return (p != nullptr) ? std::optional<T> { *p } : std::nullopt;
    }

    /// Finalise a pending edit. Returns CommitResult{before, after} so
    /// the caller can construct an undo command. Returns nullopt when:
    ///   * the property is unknown, or
    ///   * there is no pending edit (double-commit is a no-op).
    [[nodiscard]] std::optional<CommitResult> commit(const std::string& name)
    {
        auto it = defs_.find(name);
        if (it == defs_.end() || !it->second.pending.has_value())
            return std::nullopt;

        CommitResult result;
        result.name   = name;
        result.before = it->second.committed;
        result.after  = *it->second.pending;

        it->second.committed = *it->second.pending;
        it->second.pending.reset();
        return result;
    }

    /// Discard the pending edit without touching the committed value.
    /// No-op when unknown or nothing is pending.
    void cancel(const std::string& name) noexcept
    {
        auto it = defs_.find(name);
        if (it != defs_.end()) it->second.pending.reset();
    }

    // ---- Diagnostics -------------------------------------------------------

    [[nodiscard]] std::size_t size() const noexcept { return defs_.size(); }

    [[nodiscard]] bool contains(const std::string& name) const noexcept
    {
        return defs_.contains(name);
    }

    /// Raw access for iteration (display lists in the inspector panel).
    [[nodiscard]] const std::unordered_map<std::string, PropertyDef>& defs() const noexcept
    {
        return defs_;
    }

    /// Remove all definitions. Call when the selection changes.
    void clear() noexcept { defs_.clear(); }

private:
    std::unordered_map<std::string, PropertyDef> defs_;
};

}  // namespace cd::editor
