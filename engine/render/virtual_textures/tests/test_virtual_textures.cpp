#include <cd/virtual_textures/VirtualTextures.hpp>

#include <gtest/gtest.h>

namespace
{

using cd::virtual_textures::PageId;
using cd::virtual_textures::PageTable;

TEST(VirtualTextures, EmptyTableLookupReturnsNull)
{
    PageTable t(4, 4);
    EXPECT_EQ(t.lookup({ 0, 0, 0 }), nullptr);
}

TEST(VirtualTextures, AllocateInsertsValidSlot)
{
    PageTable t(4, 4);
    const auto s = t.allocate({ 1, 2, 0 });
    EXPECT_EQ(s.valid, 1U);
    EXPECT_EQ(t.resident_count(), 1U);
    EXPECT_NE(t.lookup({ 1, 2, 0 }), nullptr);
}

TEST(VirtualTextures, AllocateSamePageReturnsSameSlot)
{
    PageTable t(4, 4);
    const auto a = t.allocate({ 1, 2, 0 });
    const auto b = t.allocate({ 1, 2, 0 });
    EXPECT_EQ(a.slot_x, b.slot_x);
    EXPECT_EQ(a.slot_y, b.slot_y);
    EXPECT_EQ(t.resident_count(), 1U);
}

TEST(VirtualTextures, EvictsWhenFull)
{
    PageTable t(2, 2);  // 4 slots
    for (std::uint16_t i = 0; i < 4; ++i)
        t.allocate({ i, 0, 0 });
    EXPECT_EQ(t.resident_count(), 4U);
    // 5th page forces an eviction; resident count stays at 4.
    t.allocate({ 99, 99, 0 });
    EXPECT_EQ(t.resident_count(), 4U);
}

// phase1053: the residents() debug view exposes exactly the resident
// set and nothing else -- count matches, every entry is valid, and
// an evicted page disappears from the view.
TEST(VirtualTextures, ResidentsViewTracksAllocationAndEviction)
{
    cd::virtual_textures::PageTable pt { 2, 1 };  // 2 slots
    cd::virtual_textures::PageId a {};
    a.x = 1; a.y = 2; a.mip = 0;
    cd::virtual_textures::PageId b {};
    b.x = 3; b.y = 4; b.mip = 1;
    cd::virtual_textures::PageId c {};
    c.x = 5; c.y = 6; c.mip = 2;

    (void)pt.allocate(a);
    (void)pt.allocate(b);

    const auto& view = pt.residents();
    EXPECT_EQ(view.size(), 2U);
    EXPECT_TRUE(view.contains(a));
    EXPECT_TRUE(view.contains(b));
    for (const auto& [pid, slot] : view)
        EXPECT_EQ(slot.valid, 1U);

    (void)pt.allocate(c);  // FIFO evicts `a`

    EXPECT_EQ(view.size(), 2U);
    EXPECT_FALSE(view.contains(a));
    EXPECT_TRUE(view.contains(b));
    EXPECT_TRUE(view.contains(c));
}

TEST(VirtualTextures, GlslFeedbackHelperNonEmpty)
{
    EXPECT_FALSE(cd::virtual_textures::kFeedbackGlsl.empty());
    EXPECT_NE(cd::virtual_textures::kFeedbackGlsl.find("cd_vt_request"),
              std::string_view::npos);
}

}  // namespace
