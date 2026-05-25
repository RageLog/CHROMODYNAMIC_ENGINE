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

TEST(VirtualTextures, GlslFeedbackHelperNonEmpty)
{
    EXPECT_FALSE(cd::virtual_textures::kFeedbackGlsl.empty());
    EXPECT_NE(cd::virtual_textures::kFeedbackGlsl.find("cd_vt_request"),
              std::string_view::npos);
}

}  // namespace
