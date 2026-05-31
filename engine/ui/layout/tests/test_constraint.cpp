// =============================================================================
// CHROMODYNAMIC — cd::ui::layout constraint solver tests
//
// Coverage: 8+ cases covering the Cassowary-style incremental simplex.
// Each test follows Arrange / Act / Assert with no sleep_for / no
// global state leaking between cases.
//
// Cases:
//   1. Single equality      : x == 5 → solve → x == 5
//   2. Inequality           : x >= 3, minimise → x == 3
//   3. Conflict             : x == 5 AND x == 10 → kInfeasible on second add
//   4. Strength             : Required vs Weak — Required wins
//   5. Add then remove      : restores original weak-stay solution
//   6. Three-variable chain : a + b == c, b == 2, c == 5 → a == 3
//   7. Edit variable        : x == 5 then edit x → 10 → re-solve gives 10
//   8. Empty system         : no variables → trivial solve (empty map)
// =============================================================================
#include <cd/ui/layout/Constraint.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

namespace
{
constexpr float kEps = 1e-4F;

[[nodiscard]] bool near(float a, float b) noexcept
{
    return std::abs(a - b) < kEps;
}
}  // namespace

using namespace cd::ui::layout;

// ---- Case 1: Single equality -----------------------------------------------

TEST(ConstraintSolver, SingleEquality_XEquals5)
{
    // Arrange
    ConstraintSolver s;
    VariableId x = s.add_variable("x");

    Constraint eq;
    eq.lhs      = { Term { x, 1.0F } };
    eq.rhs      = 5.0F;
    eq.rel      = ConstraintRel::kEqual;
    eq.strength = Strength::kRequired;

    // Act
    auto result = s.add_constraint(eq);
    auto vals   = s.solve();

    // Assert
    ASSERT_TRUE(result.has_value()) << "add_constraint must succeed for x == 5";
    EXPECT_TRUE(near(vals.at(x.value), 5.0F))
        << "Expected x == 5, got " << vals.at(x.value);
}

// ---- Case 2: Inequality minimisation ----------------------------------------

TEST(ConstraintSolver, Inequality_XGreaterEq3_Minimises_To3)
{
    // Arrange
    ConstraintSolver s;
    VariableId x = s.add_variable("x");

    // x >= 3  (Required)
    Constraint geq;
    geq.lhs      = { Term { x, 1.0F } };
    geq.rhs      = 3.0F;
    geq.rel      = ConstraintRel::kGreaterEq;
    geq.strength = Strength::kRequired;

    // Act
    auto result = s.add_constraint(geq);
    auto vals   = s.solve();

    // Assert
    ASSERT_TRUE(result.has_value());
    // Weak stay wants x near 0; required says x >= 3; solver minimises → x == 3.
    EXPECT_GE(vals.at(x.value), 3.0F - kEps)
        << "x must satisfy x >= 3, got " << vals.at(x.value);
    EXPECT_LE(vals.at(x.value), 3.0F + 0.01F)
        << "Minimising objective should push x to 3, got " << vals.at(x.value);
}

// ---- Case 3: Conflicting required constraints -------------------------------

TEST(ConstraintSolver, Conflict_XEquals5_And_XEquals10_IsInfeasible)
{
    // Arrange
    ConstraintSolver s;
    VariableId x = s.add_variable("x");

    Constraint c1;
    c1.lhs      = { Term { x, 1.0F } };
    c1.rhs      = 5.0F;
    c1.rel      = ConstraintRel::kEqual;
    c1.strength = Strength::kRequired;

    Constraint c2;
    c2.lhs      = { Term { x, 1.0F } };
    c2.rhs      = 10.0F;
    c2.rel      = ConstraintRel::kEqual;
    c2.strength = Strength::kRequired;

    // Act
    auto r1 = s.add_constraint(c1);
    auto r2 = s.add_constraint(c2);

    // Assert
    EXPECT_TRUE(r1.has_value()) << "First constraint (x == 5) must be accepted";
    EXPECT_FALSE(r2.has_value()) << "Second constraint (x == 10) must be rejected";
    if (!r2.has_value())
    {
        EXPECT_EQ(r2.error(), ConstraintError::kInfeasible);
    }
}

// ---- Case 4: Required wins over Weak on conflict ---------------------------

TEST(ConstraintSolver, Strength_RequiredWinsOverWeak)
{
    // Arrange
    ConstraintSolver s;
    VariableId x = s.add_variable("x");

    // Weak stay at 0 is injected by add_variable.
    // We add a Required equality x == 7 — it should win.
    Constraint req;
    req.lhs      = { Term { x, 1.0F } };
    req.rhs      = 7.0F;
    req.rel      = ConstraintRel::kEqual;
    req.strength = Strength::kRequired;

    // Act
    auto result = s.add_constraint(req);
    auto vals   = s.solve();

    // Assert
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(near(vals.at(x.value), 7.0F))
        << "Required x == 7 must override weak stay at 0; got " << vals.at(x.value);
}

// ---- Case 5: Add then remove restores original solution --------------------

TEST(ConstraintSolver, RemoveConstraint_RestoresOriginalSolution)
{
    // Arrange
    ConstraintSolver s;
    VariableId x = s.add_variable("x");

    // After add_variable, weak stay holds x near 0.
    auto before = s.solve();
    EXPECT_TRUE(near(before.at(x.value), 0.0F));

    Constraint pull;
    pull.lhs      = { Term { x, 1.0F } };
    pull.rhs      = 42.0F;
    pull.rel      = ConstraintRel::kEqual;
    pull.strength = Strength::kStrong;

    // Act — add Strong constraint, solve, then remove and re-solve.
    (void)s.add_constraint(pull);
    auto after_add = s.solve();
    s.remove_constraint(pull);
    auto after_remove = s.solve();

    // Assert
    // After adding: x pulled toward 42 by Strong constraint.
    EXPECT_GT(after_add.at(x.value), 0.0F)
        << "Strong constraint should move x away from 0";
    // After removing: weak stay brings x back toward 0.
    EXPECT_TRUE(near(after_remove.at(x.value), 0.0F))
        << "After removal, weak stay should restore x near 0; got "
        << after_remove.at(x.value);
}

// ---- Case 6: Three-variable chain ------------------------------------------
//   a + b == c  (Required)
//   b == 2      (Required)
//   c == 5      (Required)
//   => a == 3

TEST(ConstraintSolver, ThreeVariableChain_AplusB_Equals_C)
{
    // Arrange
    ConstraintSolver s;
    VariableId a = s.add_variable("a");
    VariableId b = s.add_variable("b");
    VariableId c = s.add_variable("c");

    // a + b == c  => a + b - c == 0
    Constraint chain;
    chain.lhs      = { Term { a, 1.0F }, Term { b, 1.0F }, Term { c, -1.0F } };
    chain.rhs      = 0.0F;
    chain.rel      = ConstraintRel::kEqual;
    chain.strength = Strength::kRequired;

    Constraint b_eq;
    b_eq.lhs      = { Term { b, 1.0F } };
    b_eq.rhs      = 2.0F;
    b_eq.rel      = ConstraintRel::kEqual;
    b_eq.strength = Strength::kRequired;

    Constraint c_eq;
    c_eq.lhs      = { Term { c, 1.0F } };
    c_eq.rhs      = 5.0F;
    c_eq.rel      = ConstraintRel::kEqual;
    c_eq.strength = Strength::kRequired;

    // Act
    auto r1 = s.add_constraint(chain);
    auto r2 = s.add_constraint(b_eq);
    auto r3 = s.add_constraint(c_eq);
    auto vals = s.solve();

    // Assert
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    ASSERT_TRUE(r3.has_value());
    EXPECT_TRUE(near(vals.at(b.value), 2.0F)) << "b == 2, got " << vals.at(b.value);
    EXPECT_TRUE(near(vals.at(c.value), 5.0F)) << "c == 5, got " << vals.at(c.value);
    EXPECT_TRUE(near(vals.at(a.value), 3.0F)) << "a == 3, got " << vals.at(a.value);
}

// ---- Case 7: Edit variable -------------------------------------------------
//   x == 5, solve → x == 5
//   edit x = 10, solve → x == 10

TEST(ConstraintSolver, EditVariable_ResolvesToNewValue)
{
    // Arrange
    ConstraintSolver s;
    VariableId x = s.add_variable("x");

    Constraint c;
    c.lhs      = { Term { x, 1.0F } };
    c.rhs      = 5.0F;
    c.rel      = ConstraintRel::kEqual;
    c.strength = Strength::kStrong;

    (void)s.add_constraint(c);
    auto before = s.solve();
    EXPECT_TRUE(near(before.at(x.value), 5.0F))
        << "Before edit: x should be 5, got " << before.at(x.value);

    // Act — inject edit: x = 10.
    s.edit_variable(x, 10.0F);
    auto after = s.solve();

    // Assert
    EXPECT_TRUE(near(after.at(x.value), 10.0F))
        << "After edit: x should be 10, got " << after.at(x.value);
}

// ---- Case 8: Empty system --------------------------------------------------

TEST(ConstraintSolver, EmptySystem_SolveReturnsEmptyMap)
{
    // Arrange — no variables, no constraints.
    ConstraintSolver s;

    // Act
    auto vals = s.solve();

    // Assert
    EXPECT_TRUE(vals.empty()) << "Empty system should produce empty result map";
}

// ---- Bonus: LessEq constraint ----------------------------------------------

TEST(ConstraintSolver, Inequality_XLessEq8_PulledBy_StrongTo10_ClampsAt8)
{
    // Arrange
    ConstraintSolver s;
    VariableId x = s.add_variable("x");

    // Required: x <= 8
    Constraint leq;
    leq.lhs      = { Term { x, 1.0F } };
    leq.rhs      = 8.0F;
    leq.rel      = ConstraintRel::kLessEq;
    leq.strength = Strength::kRequired;

    // Strong: x == 20 (should be clamped to 8 by the required constraint)
    Constraint pull;
    pull.lhs      = { Term { x, 1.0F } };
    pull.rhs      = 20.0F;
    pull.rel      = ConstraintRel::kEqual;
    pull.strength = Strength::kStrong;

    // Act
    ASSERT_TRUE(s.add_constraint(leq).has_value());
    ASSERT_TRUE(s.add_constraint(pull).has_value());
    auto vals = s.solve();

    // Assert: required bound must be respected.
    EXPECT_LE(vals.at(x.value), 8.0F + kEps)
        << "Required x <= 8 must be satisfied; got " << vals.at(x.value);
}
