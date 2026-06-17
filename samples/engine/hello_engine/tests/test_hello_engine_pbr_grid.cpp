// =============================================================================
// CHROMODYNAMIC -- samples/engine/hello_engine/tests/test_hello_engine_pbr_grid.cpp
//
// phase827-pbr-grid-tests: lock the 4x4 PBR demo-grid layout so the
// phase799 "grid inside Sponza nave" relocation cannot silently drift
// back outside the atrium. Tests cover the metallic+roughness gradient
// math, the per-slot world-position computation, the in-atrium bounds
// guarantee (X∈[-15,+15], Y∈[0,+5], Z∈[-3,+3]), and the chrome-albedo
// tint constancy.
// =============================================================================

#include "../HelloPbrGrid.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>

namespace
{
using cd::hello_engine::build_pbr_demo_grid;
using cd::hello_engine::kPbrGridChromeAlbedo;
using cd::hello_engine::kPbrGridCols;
using cd::hello_engine::kPbrGridRows;
using cd::hello_engine::kPbrGridScale;
using cd::hello_engine::kPbrGridZ;
} // namespace

// ===========================================================================
// Grid cardinality + constancy.
// ===========================================================================

TEST(HelloEnginePbrGrid, GridContainsExactlySixteenSlots)
{
    const auto grid = build_pbr_demo_grid();
    EXPECT_EQ(grid.size(), 16U);
    EXPECT_EQ(kPbrGridCols * kPbrGridRows, 16);
}

TEST(HelloEnginePbrGrid, GridIsFourByFour)
{
    EXPECT_EQ(kPbrGridCols, 4);
    EXPECT_EQ(kPbrGridRows, 4);
}

TEST(HelloEnginePbrGrid, AllSlotsCarryTheChromeAlbedoTint)
{
    const auto grid = build_pbr_demo_grid();
    for (const auto& slot : grid)
    {
        EXPECT_FLOAT_EQ(slot.tint.x, kPbrGridChromeAlbedo.x);
        EXPECT_FLOAT_EQ(slot.tint.y, kPbrGridChromeAlbedo.y);
        EXPECT_FLOAT_EQ(slot.tint.z, kPbrGridChromeAlbedo.z);
    }
}

TEST(HelloEnginePbrGrid, AllSlotsHaveUniformScale)
{
    const auto grid = build_pbr_demo_grid();
    for (const auto& slot : grid)
    {
        EXPECT_FLOAT_EQ(slot.scale, kPbrGridScale);
    }
}

// ===========================================================================
// Metallic + roughness gradients.
// ===========================================================================

TEST(HelloEnginePbrGrid, MetallicGradientLeftColumnIsChromeRightIsDielectric)
{
    const auto grid = build_pbr_demo_grid();
    // Layout: idx = row * kPbrGridCols + col.
    // col=0 → metallic 1.0 (full chrome), col=3 → metallic 0.0 (dielectric).
    for (int row = 0; row < kPbrGridRows; ++row)
    {
        const auto col0 = grid[static_cast<std::size_t>(row * kPbrGridCols + 0)];
        const auto col3 = grid[static_cast<std::size_t>(row * kPbrGridCols + 3)];
        EXPECT_FLOAT_EQ(col0.metallic, 1.0F)
            << "row " << row << " col 0 should be full chrome";
        EXPECT_FLOAT_EQ(col3.metallic, 0.0F)
            << "row " << row << " col 3 should be dielectric";
    }
}

TEST(HelloEnginePbrGrid, RoughnessGradientTopRowIsMirrorBottomIsMatte)
{
    const auto grid = build_pbr_demo_grid();
    // Layout: idx = row * kPbrGridCols + col.
    // row=0 → roughness 0.04 (near-mirror), row=3 → roughness 1.0 (full matte).
    for (int col = 0; col < kPbrGridCols; ++col)
    {
        const auto row0 = grid[static_cast<std::size_t>(0 * kPbrGridCols + col)];
        const auto row3 = grid[static_cast<std::size_t>(3 * kPbrGridCols + col)];
        EXPECT_NEAR(row0.roughness, 0.04F, 1e-5F)
            << "col " << col << " row 0 should be near-mirror";
        EXPECT_NEAR(row3.roughness, 1.0F,  1e-5F)
            << "col " << col << " row 3 should be matte";
    }
}

TEST(HelloEnginePbrGrid, MetallicRoughnessMonotonicWithinRowsAndColumns)
{
    const auto grid = build_pbr_demo_grid();
    // Within a row, metallic is monotonically DECREASING with col.
    for (int row = 0; row < kPbrGridRows; ++row)
    {
        for (int col = 1; col < kPbrGridCols; ++col)
        {
            const auto prev = grid[static_cast<std::size_t>(
                row * kPbrGridCols + (col - 1))];
            const auto cur  = grid[static_cast<std::size_t>(
                row * kPbrGridCols + col)];
            EXPECT_GT(prev.metallic, cur.metallic)
                << "row " << row << " col " << col;
        }
    }
    // Within a column, roughness is monotonically INCREASING with row.
    for (int col = 0; col < kPbrGridCols; ++col)
    {
        for (int row = 1; row < kPbrGridRows; ++row)
        {
            const auto prev = grid[static_cast<std::size_t>(
                (row - 1) * kPbrGridCols + col)];
            const auto cur  = grid[static_cast<std::size_t>(
                row * kPbrGridCols + col)];
            EXPECT_LT(prev.roughness, cur.roughness)
                << "row " << row << " col " << col;
        }
    }
}

// ===========================================================================
// World placement bounds — phase799 invariant: every sphere fits the
// Sponza atrium (X∈[-15,+15], Y∈[0,+5], Z∈[-3,+3]).
// ===========================================================================

TEST(HelloEnginePbrGrid, AllSlotsFitInsideSponzaAtriumXBounds)
{
    const auto grid = build_pbr_demo_grid();
    for (const auto& slot : grid)
    {
        EXPECT_GE(slot.position.x, -15.0F);
        EXPECT_LE(slot.position.x,  15.0F);
    }
}

TEST(HelloEnginePbrGrid, AllSlotsFitInsideSponzaAtriumYBounds)
{
    const auto grid = build_pbr_demo_grid();
    for (const auto& slot : grid)
    {
        EXPECT_GE(slot.position.y, 0.0F);
        // The atrium roof sits at ~5 m; allow a bit of headroom for
        // the top-row sphere radius (kPbrGridScale = 0.42).
        EXPECT_LE(slot.position.y, 5.0F);
    }
}

TEST(HelloEnginePbrGrid, AllSlotsFitInsideSponzaAtriumZBounds)
{
    const auto grid = build_pbr_demo_grid();
    for (const auto& slot : grid)
    {
        // Phase799 puts kPbrGridZ = 0; locking the constant guarantees
        // every sphere sits mid-nave.
        EXPECT_EQ(slot.position.z, kPbrGridZ);
        EXPECT_GE(slot.position.z, -3.0F);
        EXPECT_LE(slot.position.z,  3.0F);
    }
}

TEST(HelloEnginePbrGrid, ZIsZeroAfterPhase799Relocation)
{
    // Phase799 root-cause guard: kPbrGridZ MUST be 0 (mid-nave). A
    // future refactor that pushes it back outside the atrium re-opens
    // the original "spheres in a different universe" bug.
    EXPECT_FLOAT_EQ(kPbrGridZ, 0.0F);
}

// ===========================================================================
// Slot naming.
// ===========================================================================

TEST(HelloEnginePbrGrid, SlotNamesFollowMrowColPattern)
{
    const auto grid = build_pbr_demo_grid();
    for (int row = 0; row < kPbrGridRows; ++row)
    {
        for (int col = 0; col < kPbrGridCols; ++col)
        {
            const auto& slot = grid[static_cast<std::size_t>(
                row * kPbrGridCols + col)];
            const std::string expected =
                std::string { "PBR M" } + std::to_string(col) + "R" + std::to_string(row);
            EXPECT_EQ(slot.name, expected) << "slot (row " << row << ", col " << col << ")";
        }
    }
}

// ===========================================================================
// X positions are symmetric around 0 — the grid is mirror-balanced
// about the nave centre-line.
// ===========================================================================

TEST(HelloEnginePbrGrid, XPositionsAreSymmetricAboutZero)
{
    const auto grid = build_pbr_demo_grid();
    for (int row = 0; row < kPbrGridRows; ++row)
    {
        const auto& col0 = grid[static_cast<std::size_t>(row * kPbrGridCols + 0)];
        const auto& col3 = grid[static_cast<std::size_t>(row * kPbrGridCols + 3)];
        const auto& col1 = grid[static_cast<std::size_t>(row * kPbrGridCols + 1)];
        const auto& col2 = grid[static_cast<std::size_t>(row * kPbrGridCols + 2)];
        EXPECT_NEAR(col0.position.x + col3.position.x, 0.0F, 1e-5F)
            << "row " << row << " outer columns should mirror about 0";
        EXPECT_NEAR(col1.position.x + col2.position.x, 0.0F, 1e-5F)
            << "row " << row << " inner columns should mirror about 0";
    }
}

// ===========================================================================
// phase986-pbr-texture-fix: lock the textured-column constant + its
// dielectric invariant. Regressions where the column index drifts off
// the dielectric edge (metallic=0) would cause the albedo texture to be
// occluded by the metallic F0 path -- breaking the visible-quality
// expectation of "the right column of spheres shows earth texture
// detail".
// ===========================================================================

TEST(HelloEnginePbrGrid, TexturedColumnConstantIsTheRightEdge)
{
    EXPECT_EQ(cd::hello_engine::kPbrGridTexturedColumn,
              cd::hello_engine::kPbrGridCols - 1);
}

TEST(HelloEnginePbrGrid, TexturedColumnIsFullyDielectric)
{
    const auto grid = build_pbr_demo_grid();
    for (int row = 0; row < kPbrGridRows; ++row)
    {
        const auto& slot = grid[static_cast<std::size_t>(
            row * kPbrGridCols + cd::hello_engine::kPbrGridTexturedColumn)];
        EXPECT_FLOAT_EQ(slot.metallic, 0.0F)
            << "Textured column row " << row
            << " must stay dielectric -- otherwise the earth_albedo "
            << "sample is multiplied by an F0 chrome path and the "
            << "user-visible texture detail collapses.";
    }
}
