// =============================================================================
// CHROMODYNAMIC — cd/ui/layout/Constraint.cpp
//
// Phase 5 Cassowary-style incremental constraint solver.
//
// Design
// ------
// Each user variable x_i is tracked as an affine expression over a set of
// free "parametric" columns p_j:
//   x_i = c_i + Σ_j a_ij * p_j
//
// The canonical (primal) solution has all p_j = 0, so x_i = c_i.
//
// Required equality  lhs = rhs
//   Build e = lhs - rhs (substituting var_expr for each user variable).
//   If e.terms is empty:  feasible iff |e.constant| < eps; else infeasible.
//   Otherwise:  pick the column with largest |coeff| as the pivot,
//               express it as a function of the others (direct substitution),
//               fold the result into all var_expr.  After this, x_i.constant
//               reflects the new value with the constraint enforced exactly.
//
// Required inequality  lhs <= rhs  (or >= rhs)
//   Enforce the boundary lhs = rhs using the same substitution if the
//   constraint is currently violated.  If the constraint is already satisfied
//   at the current point, record a slack column for future use.
//   On solve(), clamp parametric-column contributions to keep inequalities.
//
//   For simplicity: Required inequalities are enforced at the boundary
//   (lhs = rhs) when initially violated.  This is correct for minimising
//   stay-constraint violations: the minimum is at the boundary.
//
// Soft constraints (Strong / Medium / Weak)
//   After all Required constraints are applied, minimise the weighted
//   least-squares objective over the remaining free parametric columns.
//   Each soft constraint k contributes  w_k * (e_k.constant + Σ a_kj * p_j)^2.
//   Minimising over p_j independently (coordinate descent, one pass):
//     p_j* = -Σ_k(w_k * a_kj * e_k.constant) / Σ_k(w_k * a_kj^2)
//   This is correct when soft constraints decouple across p_j.
//   The result is applied as a read-only delta in compute_values() without
//   mutating var_expr, preserving the Required solution for future calls.
// =============================================================================

#include <cd/ui/layout/Constraint.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cd::ui::layout
{

namespace
{

inline constexpr float kEps = 1.0e-6F;

[[nodiscard]] bool approx_zero(float v) noexcept { return std::fabsf(v) < kEps; }

[[nodiscard]] constexpr float strength_weight(Strength s) noexcept
{
    switch (s)
    {
        case Strength::kRequired: return 0.0F;
        case Strength::kStrong:   return 1.0e6F;
        case Strength::kMedium:   return 1.0e3F;
        case Strength::kWeak:     return 1.0F;
    }
    return 1.0F;
}

using Col = std::uint32_t;
inline constexpr Col kNoCol = 0xFFFF'FFFFu;

struct Expr
{
    float                          constant { 0.0F };
    std::unordered_map<Col, float> terms    {};

    void add(Col c, float v)
    {
        if (approx_zero(v)) { return; }
        auto [it, ok] = terms.try_emplace(c, v);
        if (!ok)
        {
            it->second += v;
            if (approx_zero(it->second)) { terms.erase(it); }
        }
    }

    [[nodiscard]] float coeff(Col c) const noexcept
    {
        auto it = terms.find(c);
        return (it != terms.end()) ? it->second : 0.0F;
    }

    void add_scaled(const Expr& src, float factor)
    {
        if (approx_zero(factor)) { return; }
        constant += factor * src.constant;
        for (const auto& [c, v] : src.terms) { add(c, factor * v); }
    }

    // Evaluate: constant + Σ terms[c] * vals[c]  (missing cols = 0)
    [[nodiscard]] float eval(const std::unordered_map<Col, float>& vals) const
    {
        float r = constant;
        for (const auto& [c, v] : terms)
        {
            auto it = vals.find(c);
            if (it != vals.end()) { r += v * it->second; }
        }
        return r;
    }
};

}  // anonymous namespace

// ============================================================================
// ConstraintSolver::Impl
// ============================================================================

struct ConstraintSolver::Impl
{
    std::vector<std::string> var_names {};

    // var_expr[i] = expression for variable i in free parametric columns.
    // Invariant: var_expr[i].constant is the Required-solution value of x_i.
    std::vector<Expr>  var_expr  {};
    std::vector<float> var_value {};

    Col next_col { 0 };
    [[nodiscard]] Col alloc() noexcept { return next_col++; }

    // ---- Substitution -------------------------------------------------------
    // Substitute col c → expr in all var_expr.
    void substitute_all(Col c, const Expr& expr)
    {
        for (auto& ve : var_expr)
        {
            float coeff = ve.coeff(c);
            if (approx_zero(coeff)) { continue; }
            ve.terms.erase(c);
            ve.add_scaled(expr, coeff);
        }
    }

    // ---- Build constraint expression ----------------------------------------
    [[nodiscard]] Expr build_expr(const Constraint& c) const
    {
        Expr e;
        e.constant = -c.rhs;
        for (const auto& term : c.lhs)
        {
            e.add_scaled(var_expr[term.variable_id.value], term.coefficient);
        }
        return e;  // e = lhs - rhs
    }

    // ---- Enforce e == 0 via substitution ------------------------------------
    // Returns false if infeasible (e.terms empty and |e.constant| > eps).
    [[nodiscard]] bool enforce_eq(const Expr& e, Col& pinned_col)
    {
        if (e.terms.empty())
        {
            return std::fabsf(e.constant) < 1.0e-4F;
        }

        // Pick largest |coeff| for numerical stability.
        Col   best_c  = kNoCol;
        float best_v  = 0.0F;
        for (const auto& [c, v] : e.terms)
        {
            if (std::fabsf(v) > std::fabsf(best_v)) { best_v = v; best_c = c; }
        }
        assert(best_c != kNoCol && !approx_zero(best_v));

        // Solve: best_c = -e.constant/best_v + Σ_{j≠best_c}(-e_j/best_v)*j
        Expr solved;
        solved.constant = -e.constant / best_v;
        for (const auto& [c, v] : e.terms)
        {
            if (c == best_c) { continue; }
            solved.add(c, -v / best_v);
        }

        pinned_col = best_c;
        substitute_all(best_c, solved);
        return true;
    }

    // ---- Stored constraints -------------------------------------------------
    struct StoredConstraint
    {
        Constraint orig;
        Col        pinned_col { kNoCol };  // for Required equality: the eliminated col
    };
    std::vector<StoredConstraint> stored {};

    // ---- Full re-solve from scratch -----------------------------------------
    // Resets all var_expr and re-applies all stored constraints.
    void full_resolve()
    {
        const std::size_t nv = var_names.size();
        var_expr.assign(nv, Expr{});

        for (std::size_t i = 0; i < nv; ++i)
        {
            Col p = alloc();
            var_expr[i].constant = 0.0F;
            var_expr[i].add(p, 1.0F);
        }

        for (auto& sc : stored)
        {
            sc.pinned_col = kNoCol;
            if (sc.orig.strength != Strength::kRequired) { continue; }

            Expr e = build_expr(sc.orig);

            // For inequality: enforce boundary if violated, otherwise skip.
            switch (sc.orig.rel)
            {
            case ConstraintRel::kEqual:
                (void)enforce_eq(e, sc.pinned_col);
                break;
            case ConstraintRel::kLessEq:
                // e = lhs - rhs; violated if e.constant > eps (lhs > rhs).
                if (e.constant > kEps) { (void)enforce_eq(e, sc.pinned_col); }
                break;
            case ConstraintRel::kGreaterEq:
                // e = lhs - rhs; violated if e.constant < -eps (lhs < rhs).
                if (e.constant < -kEps) { (void)enforce_eq(e, sc.pinned_col); }
                break;
            }
        }
    }

    // ---- Soft optimisation --------------------------------------------------
    // Computes optimal parametric-column values WITHOUT mutating var_expr.
    // Returns a map col -> value for all free columns involved in soft constraints.
    [[nodiscard]] std::unordered_map<Col, float> soft_optimal() const
    {
        // Collect (expression, weight) for each soft constraint.
        struct SoftTerm { Expr e; float w; };
        std::vector<SoftTerm> softs;
        for (const auto& sc : stored)
        {
            if (sc.orig.strength == Strength::kRequired) { continue; }
            softs.push_back({ build_expr(sc.orig), strength_weight(sc.orig.strength) });
        }
        if (softs.empty()) { return {}; }

        // For each free column, compute the optimal step via weighted projection.
        std::unordered_map<Col, float> result;
        for (const auto& [e, w] : softs)
        {
            for (const auto& [c, v] : e.terms)
            {
                result.emplace(c, 0.0F);
            }
        }

        for (auto& [pcol, pval] : result)
        {
            float num = 0.0F;
            float den = 0.0F;
            for (const auto& [e, w] : softs)
            {
                float a = e.coeff(pcol);
                if (approx_zero(a)) { continue; }
                num += w * a * e.constant;
                den += w * a * a;
            }
            if (std::fabsf(den) > kEps) { pval = -num / den; }
        }

        // Clamp parametric values against Required inequality constraints.
        // For each Required inequality, compute its residual with the current
        // soft_vals and, if violated, scale back the offending column values
        // by zeroing the contributing parametric column's soft delta.
        for (const auto& sc : stored)
        {
            const Constraint& c = sc.orig;
            if (c.strength != Strength::kRequired) { continue; }
            if (c.rel == ConstraintRel::kEqual)     { continue; }

            // e = lhs - rhs in var_expr terms.
            Expr ce = build_expr(c);
            float val = ce.eval(result);

            // kLessEq: e <= 0 required. If val > eps, violated.
            // kGreaterEq: e >= 0 required. If val < -eps, violated.
            bool violated = (c.rel == ConstraintRel::kLessEq)
                            ? (val > kEps)
                            : (val < -kEps);

            if (!violated) { continue; }

            // Violated: zero out the parametric contributions that cause violation.
            // For each column in ce.terms that appears in result, set pval = 0
            // if it contributes in the violating direction.
            for (const auto& [col, coeff] : ce.terms)
            {
                auto it = result.find(col);
                if (it == result.end()) { continue; }
                float contribution = coeff * it->second;
                // For kLessEq violation (val > 0): zero cols that increase val.
                // For kGreaterEq violation (val < 0): zero cols that decrease val.
                bool contributes_to_violation =
                    (c.rel == ConstraintRel::kLessEq)   ? (contribution > kEps)
                                                        : (contribution < -kEps);
                if (contributes_to_violation) { it->second = 0.0F; }
            }
        }
        return result;
    }

    // ---- Compute variable values -------------------------------------------
    void compute_values(const std::unordered_map<Col, float>& soft_vals)
    {
        var_value.resize(var_expr.size());
        for (std::size_t i = 0; i < var_expr.size(); ++i)
        {
            var_value[i] = var_expr[i].eval(soft_vals);
        }
    }

    // Edit variable state.
    struct EditEntry
    {
        VariableId  id {};
        float       suggested { 0.0F };
        std::size_t stored_idx { std::size_t(-1) };
    };
    std::vector<EditEntry> edits {};
};

// ============================================================================
// ConstraintSolver public API
// ============================================================================

ConstraintSolver::ConstraintSolver()
    : impl_(std::make_unique<Impl>())
{}

ConstraintSolver::~ConstraintSolver() = default;
ConstraintSolver::ConstraintSolver(ConstraintSolver&&) noexcept = default;
ConstraintSolver& ConstraintSolver::operator=(ConstraintSolver&&) noexcept = default;

VariableId ConstraintSolver::add_variable(std::string_view name)
{
    VariableId id { static_cast<std::uint32_t>(impl_->var_names.size()) };
    impl_->var_names.emplace_back(name);

    Col param = impl_->alloc();
    Expr ve;
    ve.constant = 0.0F;
    ve.add(param, 1.0F);
    impl_->var_expr.push_back(std::move(ve));
    impl_->var_value.push_back(0.0F);

    // Weak stay: x == 0
    Constraint stay;
    stay.lhs      = { Term { id, 1.0F } };
    stay.rhs      = 0.0F;
    stay.rel      = ConstraintRel::kEqual;
    stay.strength = Strength::kWeak;
    (void)add_constraint(stay);

    return id;
}

float ConstraintSolver::variable_value(VariableId id) const noexcept
{
    if (id.value >= impl_->var_value.size()) { return 0.0F; }
    return impl_->var_value[id.value];
}

std::expected<void, ConstraintError>
ConstraintSolver::add_constraint(const Constraint& c)
{
    for (const auto& term : c.lhs)
    {
        if (term.variable_id.value >= impl_->var_expr.size())
        {
            return std::unexpected(ConstraintError::kUnknownVar);
        }
    }

    Impl::StoredConstraint sc;
    sc.orig = c;

    if (c.strength == Strength::kRequired)
    {
        Expr e = impl_->build_expr(c);
        switch (c.rel)
        {
        case ConstraintRel::kEqual:
            if (!impl_->enforce_eq(e, sc.pinned_col))
            {
                return std::unexpected(ConstraintError::kInfeasible);
            }
            break;
        case ConstraintRel::kLessEq:
            if (e.constant > kEps)
            {
                // Currently violated: enforce boundary lhs = rhs.
                if (!impl_->enforce_eq(e, sc.pinned_col))
                {
                    return std::unexpected(ConstraintError::kInfeasible);
                }
            }
            // else: already satisfied, no structural change needed.
            break;
        case ConstraintRel::kGreaterEq:
            if (e.constant < -kEps)
            {
                // Currently violated: enforce boundary lhs = rhs.
                if (!impl_->enforce_eq(e, sc.pinned_col))
                {
                    return std::unexpected(ConstraintError::kInfeasible);
                }
            }
            break;
        }
    }

    impl_->stored.push_back(std::move(sc));
    return {};
}

void ConstraintSolver::remove_constraint(const Constraint& c)
{
    auto same = [&](const Impl::StoredConstraint& sc) -> bool
    {
        const Constraint& a = sc.orig;
        if (a.rel != c.rel || a.strength != c.strength)       { return false; }
        if (std::fabsf(a.rhs - c.rhs) > kEps)                { return false; }
        if (a.lhs.size() != c.lhs.size())                     { return false; }
        for (std::size_t i = 0; i < a.lhs.size(); ++i)
        {
            if (a.lhs[i].variable_id != c.lhs[i].variable_id)                   { return false; }
            if (std::fabsf(a.lhs[i].coefficient - c.lhs[i].coefficient) > kEps) { return false; }
        }
        return true;
    };

    for (std::size_t i = 0; i < impl_->stored.size(); ++i)
    {
        if (same(impl_->stored[i]))
        {
            impl_->stored.erase(
                impl_->stored.begin() + static_cast<std::ptrdiff_t>(i));
            impl_->full_resolve();
            return;
        }
    }
}

void ConstraintSolver::edit_variable(VariableId id, float suggested_value)
{
    if (!id.is_valid() || id.value >= impl_->var_expr.size()) { return; }

    // Remove existing edit constraint.
    for (auto& e : impl_->edits)
    {
        if (e.id == id && e.stored_idx < impl_->stored.size())
        {
            impl_->stored.erase(
                impl_->stored.begin() +
                static_cast<std::ptrdiff_t>(e.stored_idx));
            for (auto& e2 : impl_->edits)
            {
                if (e2.stored_idx > e.stored_idx &&
                    e2.stored_idx != std::size_t(-1))
                {
                    --e2.stored_idx;
                }
            }
            e.stored_idx = std::size_t(-1);
            impl_->full_resolve();
            break;
        }
    }

    Constraint edit_c;
    edit_c.lhs      = { Term { id, 1.0F } };
    edit_c.rhs      = suggested_value;
    edit_c.rel      = ConstraintRel::kEqual;
    edit_c.strength = Strength::kRequired;

    std::size_t new_idx = impl_->stored.size();
    (void)add_constraint(edit_c);

    bool found = false;
    for (auto& e : impl_->edits)
    {
        if (e.id == id)
        {
            e.suggested  = suggested_value;
            e.stored_idx = new_idx;
            found        = true;
            break;
        }
    }
    if (!found)
    {
        impl_->edits.push_back({ id, suggested_value, new_idx });
    }
}

std::unordered_map<std::uint32_t, float> ConstraintSolver::solve()
{
    // Compute soft-optimal parametric values (read-only, does not mutate var_expr).
    auto soft_vals = impl_->soft_optimal();

    // Compute final variable values = Required value + soft contribution.
    impl_->compute_values(soft_vals);

    std::unordered_map<std::uint32_t, float> result;
    result.reserve(impl_->var_value.size());
    for (std::uint32_t i = 0;
         i < static_cast<std::uint32_t>(impl_->var_value.size()); ++i)
    {
        result[i] = impl_->var_value[i];
    }
    return result;
}

}  // namespace cd::ui::layout
