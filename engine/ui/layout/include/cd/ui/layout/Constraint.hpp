// =============================================================================
// CHROMODYNAMIC — cd/ui/layout/Constraint.hpp
//
// Phase 5 of ADR-20260530-ui-widget-library: Cassowary-style incremental
// constraint solver for the UI layout tier.
//
// Algorithm: Simplified Cassowary (Badros, Borning, Stuckey 2001, TOPLAS).
//   - Incremental simplex over a linear program.
//   - Stay constraints per variable (weak preference to keep current value).
//   - Edit variables allow live drag / runtime updates without full re-solve.
//   - Infeasibility on conflicting Required constraints is detected at
//     add_constraint time and reported via std::expected.
//
// Public types:
//   VariableId          — stable opaque integer handle
//   ConstraintRel       — Equal / LessEq / GreaterEq
//   Strength            — kRequired / kStrong / kMedium / kWeak
//   Term                — { variable_id, coefficient }
//   Constraint          — lhs terms = rhs (float) with rel + strength
//   ConstraintSolver    — the public façade
//
// Usage sketch:
//   ConstraintSolver s;
//   auto x = s.add_variable("x");
//   auto y = s.add_variable("y");
//   Constraint eq { .lhs = {{x, 1.0f}}, .rhs = 5.0f,
//                   .rel = ConstraintRel::kEqual,
//                   .strength = Strength::kRequired };
//   s.add_constraint(eq);
//   auto vals = s.solve();   // vals[x] == 5.0f
//
// Namespace: cd::ui::layout
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cd::ui::layout
{

// ---- Opaque handle -----------------------------------------------------------

/// Stable identifier for a solver variable. Returned by add_variable();
/// valid for the lifetime of the ConstraintSolver that issued it.
struct VariableId
{
    std::uint32_t value { 0xFFFFFFFFu };

    [[nodiscard]] constexpr bool is_valid() const noexcept
    {
        return value != 0xFFFFFFFFu;
    }
    [[nodiscard]] constexpr bool operator==(VariableId o) const noexcept
    {
        return value == o.value;
    }
    [[nodiscard]] constexpr bool operator!=(VariableId o) const noexcept
    {
        return value != o.value;
    }
};

inline constexpr VariableId kInvalidVariable {};

// ---- Enums -------------------------------------------------------------------

/// Relational operator for the constraint lhs rel rhs.
enum class ConstraintRel : std::uint8_t
{
    kEqual     = 0,   ///< lhs == rhs
    kLessEq    = 1,   ///< lhs <= rhs
    kGreaterEq = 2,   ///< lhs >= rhs
};

/// Cassowary strength hierarchy.
enum class Strength : std::uint8_t
{
    kRequired = 0,  ///< Must be satisfied — infeasibility is an error
    kStrong   = 1,  ///< High-priority soft constraint
    kMedium   = 2,
    kWeak     = 3,  ///< Stay constraints use this level
};

// ---- Constraint description --------------------------------------------------

/// One term in a linear expression: coefficient * variable.
struct Term
{
    VariableId variable_id {};
    float      coefficient { 1.0F };
};

/// A linear constraint:  sum(lhs terms) rel rhs
///
/// Examples:
///   x == 5          -> lhs={{x,1}}, rhs=5,  rel=kEqual
///   x + y <= 10     -> lhs={{x,1},{y,1}}, rhs=10, rel=kLessEq
///   2*x - y == 0    -> lhs={{x,2},{y,-1}}, rhs=0, rel=kEqual
struct Constraint
{
    std::vector<Term> lhs {};
    float             rhs     { 0.0F };
    ConstraintRel     rel     { ConstraintRel::kEqual };
    Strength          strength{ Strength::kRequired };
};

// ---- Error type --------------------------------------------------------------

enum class ConstraintError : std::uint8_t
{
    kInfeasible    = 0,  ///< Constraint conflicts with Required constraints
    kUnknownVar    = 1,  ///< A VariableId in the constraint is not registered
    kDuplicate     = 2,  ///< Exact same constraint object already added
};

// ---- ConstraintSolver --------------------------------------------------------

/// Cassowary-style incremental constraint solver.
///
/// Thread safety: NOT thread-safe — external locking required if shared.
class ConstraintSolver
{
public:
    ConstraintSolver();
    ~ConstraintSolver();

    ConstraintSolver(const ConstraintSolver&) = delete;
    ConstraintSolver& operator=(const ConstraintSolver&) = delete;
    ConstraintSolver(ConstraintSolver&&) noexcept;
    ConstraintSolver& operator=(ConstraintSolver&&) noexcept;

    // ---- Variable management ------------------------------------------------

    /// Register a new variable with an optional debug name.
    /// Returns a stable VariableId.  Initial value = 0.
    [[nodiscard]] VariableId add_variable(std::string_view name = {});

    /// Query current value of a variable (result from last solve()).
    /// Returns 0 for unknown/unregistered ids.
    [[nodiscard]] float variable_value(VariableId id) const noexcept;

    // ---- Constraint management ----------------------------------------------

    /// Add a constraint. Returns an error if the constraint cannot be
    /// satisfied (kInfeasible for Required, kUnknownVar, kDuplicate).
    /// Soft constraints (Strong/Medium/Weak) that conflict are accepted but
    /// may not be fully met after solve().
    [[nodiscard]] std::expected<void, ConstraintError>
    add_constraint(const Constraint& c);

    /// Remove a previously added constraint.  No-op if not found.
    void remove_constraint(const Constraint& c);

    // ---- Edit-variable mode -------------------------------------------------

    /// Suggest a new value for a variable at runtime (e.g. live drag).
    /// Internally converts the variable to an "edit variable" with a Required
    /// edit constraint; subsequent solve() re-resolves all dependents.
    /// Call edit_variable() repeatedly before solve() for multiple updates.
    void edit_variable(VariableId id, float suggested_value);

    // ---- Solve --------------------------------------------------------------

    /// Run the solver over all current constraints and return the resolved
    /// values for all registered variables.
    ///
    /// The returned map is keyed by VariableId::value (uint32_t) for simple
    /// iteration.  Use variable_value(id) for single-variable access.
    [[nodiscard]] std::unordered_map<std::uint32_t, float> solve();

private:
    // Forward-declared implementation object (PIMPL pattern keeps simplex
    // tableau details out of the public header and speeds up compilation).
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cd::ui::layout
