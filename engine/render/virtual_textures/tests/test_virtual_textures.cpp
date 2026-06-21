#include <cd/virtual_textures/VirtualTextures.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace
{

using cd::virtual_textures::AtlasSlot;
using cd::virtual_textures::PageId;
using cd::virtual_textures::PageIdHash;
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

// ===========================================================================
// ADD-ONLY depth (floor-raise, host-only, no device). Every case below pins
// ACTUAL behaviour of the EXISTING types/math: PageId equality, PageIdHash
// bit-band layout (the host mirror of the feedback uvec4 pack), AtlasSlot
// linear addressing (n%w, n/w), FIFO eviction order, multi-mip distinctness,
// and the GLSL feedback contract. No public API, bit layout, or GLSL string
// is changed -- golden-safe / byte-identical.
// ===========================================================================

// ---- PageId equality / field identity -------------------------------------

// Equality is all-three-fields (x AND y AND mip). Flip any one -> not equal.
TEST(VirtualTextures, PageIdEqualityIsPerField)
{
    const PageId base { 7, 9, 3 };
    EXPECT_EQ(base, (PageId { 7, 9, 3 }));
    EXPECT_FALSE(base == (PageId { 8, 9, 3 }));  // x differs
    EXPECT_FALSE(base == (PageId { 7, 8, 3 }));  // y differs
    EXPECT_FALSE(base == (PageId { 7, 9, 2 }));  // mip differs
}

// Default-constructed PageId is the {0,0,0} origin page.
TEST(VirtualTextures, PageIdDefaultIsOrigin)
{
    const PageId d {};
    EXPECT_EQ(d.x, 0U);
    EXPECT_EQ(d.y, 0U);
    EXPECT_EQ(d.mip, 0U);
    EXPECT_EQ(d, (PageId { 0, 0, 0 }));
}

// ---- PageIdHash bit-band layout (mirrors the feedback uvec4 packing) ------

// Each field lands in a disjoint byte band: mip in bits[0..7],
// y in bits[8..23], x in bits[24..39]. Pin the exact shifts.
TEST(VirtualTextures, HashPlacesFieldsInDisjointBands)
{
    const PageIdHash h {};
    EXPECT_EQ(h(PageId { 1, 0, 0 }), std::size_t { 1 } << 24);  // x band
    EXPECT_EQ(h(PageId { 0, 1, 0 }), std::size_t { 1 } <<  8);  // y band
    EXPECT_EQ(h(PageId { 0, 0, 1 }), std::size_t { 1 });        // mip band
    EXPECT_EQ(h(PageId { 0, 0, 0 }), std::size_t { 0 });        // origin -> 0
}

// Max field values fill every band with no overlap -> 0xFF_FFFF_FFFF.
TEST(VirtualTextures, HashMaxFieldsFillAllBands)
{
    const PageIdHash h {};
    const std::size_t expected = (std::size_t { 0xFFFF } << 24) ^
                                 (std::size_t { 0xFFFF } <<  8) ^
                                  std::size_t { 0xFF };
    EXPECT_EQ(h(PageId { 0xFFFF, 0xFFFF, 0xFF }), expected);
    EXPECT_EQ(expected, std::size_t { 0xFFFFFFFFFFULL });
}

// Equal PageIds hash equal; the bands are disjoint so distinct pages that
// only vary in one field produce distinct hashes (no aliasing here).
TEST(VirtualTextures, HashConsistentAndDiscriminatesFields)
{
    const PageIdHash h {};
    EXPECT_EQ(h(PageId { 5, 6, 1 }), h(PageId { 5, 6, 1 }));
    EXPECT_NE(h(PageId { 5, 6, 1 }), h(PageId { 6, 5, 1 }));  // x<->y swap
    EXPECT_NE(h(PageId { 5, 6, 1 }), h(PageId { 5, 6, 2 }));  // mip step
}

// ---- AtlasSlot linear addressing (n % w, n / w row-major) -----------------

// On a 4-wide atlas the first row fills slot_x 0..3 at slot_y 0, then the
// 5th allocation wraps to the next row (0,1). Pins n%w / n/w addressing.
TEST(VirtualTextures, AtlasSlotsFillRowMajor)
{
    // Allocate sequentially so physical indices are deterministic 0..4.
    PageTable t(4, 4);
    AtlasSlot got[5];
    for (std::uint16_t i = 0; i < 5; ++i)
        got[i] = t.allocate({ i, 0, 0 });
    EXPECT_EQ(got[0].slot_x, 0U); EXPECT_EQ(got[0].slot_y, 0U);  // idx 0
    EXPECT_EQ(got[1].slot_x, 1U); EXPECT_EQ(got[1].slot_y, 0U);  // idx 1
    EXPECT_EQ(got[3].slot_x, 3U); EXPECT_EQ(got[3].slot_y, 0U);  // end of row 0
    EXPECT_EQ(got[4].slot_x, 0U); EXPECT_EQ(got[4].slot_y, 1U);  // wrapped row 1
}

// A non-square atlas (3 wide, 2 tall) still addresses row-major by width.
TEST(VirtualTextures, AtlasSlotsNonSquareUsesWidth)
{
    PageTable t(3, 2);  // 6 slots, width 3
    AtlasSlot got[4];
    for (std::uint16_t i = 0; i < 4; ++i)
        got[i] = t.allocate({ i, 0, 0 });
    EXPECT_EQ(got[2].slot_x, 2U); EXPECT_EQ(got[2].slot_y, 0U);  // end of row 0
    EXPECT_EQ(got[3].slot_x, 0U); EXPECT_EQ(got[3].slot_y, 1U);  // start row 1
}

// Allocated slots are marked resident; lookup returns the same coordinates.
TEST(VirtualTextures, LookupReturnsAllocatedSlotCoordinates)
{
    PageTable t(4, 4);
    const AtlasSlot a = t.allocate({ 2, 1, 0 });
    const AtlasSlot* p = t.lookup({ 2, 1, 0 });
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->slot_x, a.slot_x);
    EXPECT_EQ(p->slot_y, a.slot_y);
    EXPECT_EQ(p->valid, 1U);
}

// ---- FIFO eviction order (oldest page reclaimed first) --------------------

// With a 2-slot atlas, allocating a 3rd page evicts the FIRST inserted page
// (FIFO), and the evicted page reuses the victim's physical slot.
TEST(VirtualTextures, FifoEvictsOldestAndReusesSlot)
{
    PageTable t(2, 1);  // 2 slots, width 2
    const AtlasSlot sa = t.allocate({ 1, 0, 0 });  // oldest
    (void)t.allocate({ 2, 0, 0 });
    const AtlasSlot sc = t.allocate({ 3, 0, 0 });  // evicts {1,0,0}

    EXPECT_EQ(t.lookup({ 1, 0, 0 }), nullptr);      // oldest gone
    EXPECT_NE(t.lookup({ 2, 0, 0 }), nullptr);      // survivor stays
    ASSERT_NE(t.lookup({ 3, 0, 0 }), nullptr);
    EXPECT_EQ(t.resident_count(), 2U);
    // The new page took over the evicted victim's physical slot.
    EXPECT_EQ(sc.slot_x, sa.slot_x);
    EXPECT_EQ(sc.slot_y, sa.slot_y);
    EXPECT_EQ(sc.valid, 1U);
}

// Re-allocating a still-resident page is a no-op hit: count unchanged, no
// eviction, same slot back (mirrors AllocateSamePageReturnsSameSlot but with
// the table near full so the early-return path is exercised under pressure).
TEST(VirtualTextures, ReallocateResidentDoesNotEvict)
{
    PageTable t(2, 1);  // 2 slots
    const AtlasSlot a = t.allocate({ 1, 0, 0 });
    (void)t.allocate({ 2, 0, 0 });  // table now full
    const AtlasSlot again = t.allocate({ 1, 0, 0 });  // resident hit, no evict
    EXPECT_EQ(t.resident_count(), 2U);
    EXPECT_NE(t.lookup({ 2, 0, 0 }), nullptr);  // survivor not evicted
    EXPECT_EQ(again.slot_x, a.slot_x);
    EXPECT_EQ(again.slot_y, a.slot_y);
}

// ---- Multi-mip residency (same x/y, different mip = distinct pages) -------

// Pages that share x/y but differ in mip are independent residents and take
// independent atlas slots (the mip field participates in identity + hash).
TEST(VirtualTextures, DistinctMipsAreIndependentPages)
{
    PageTable t(4, 4);
    const AtlasSlot m0 = t.allocate({ 5, 5, 0 });
    const AtlasSlot m1 = t.allocate({ 5, 5, 1 });
    EXPECT_EQ(t.resident_count(), 2U);
    EXPECT_NE(t.lookup({ 5, 5, 0 }), nullptr);
    EXPECT_NE(t.lookup({ 5, 5, 1 }), nullptr);
    // Different physical slots (sequential indices 0 and 1).
    const bool different_slot =
        (m0.slot_x != m1.slot_x) || (m0.slot_y != m1.slot_y);
    EXPECT_TRUE(different_slot);
}

// ---- Capacity / degenerate atlas ------------------------------------------

// resident_count never exceeds total slot capacity, even when over-subscribed.
TEST(VirtualTextures, ResidentCountCappedAtCapacity)
{
    PageTable t(2, 2);  // 4 slots
    for (std::uint16_t i = 0; i < 16; ++i)
        t.allocate({ i, 0, 0 });
    EXPECT_EQ(t.resident_count(), 4U);
}

// A freshly built table is empty regardless of atlas dimensions.
TEST(VirtualTextures, FreshTableIsEmpty)
{
    PageTable t(8, 4);
    EXPECT_EQ(t.resident_count(), 0U);
    EXPECT_EQ(t.lookup({ 0, 0, 0 }), nullptr);
    EXPECT_TRUE(t.residents().empty());
}

// ---- GLSL feedback contract (host pack mirrors the shader uvec4) ----------

// The embedded GLSL must encode the request exactly as uvec4(page_xy, mip,
// frame_id) and append it atomically -- this is the cross-language contract
// the host PageIdHash band layout mirrors. Pin the load-bearing tokens.
TEST(VirtualTextures, GlslFeedbackContractTokens)
{
    constexpr std::string_view g = cd::virtual_textures::kFeedbackGlsl;
    EXPECT_NE(g.find("uvec4(page_xy, mip, frame_id)"), std::string_view::npos);
    EXPECT_NE(g.find("atomicAdd(F.count, 1u)"),        std::string_view::npos);
    EXPECT_NE(g.find("FeedbackReq reqs[]"),            std::string_view::npos);
    EXPECT_NE(g.find("binding = 5"),                   std::string_view::npos);
}

}  // namespace
