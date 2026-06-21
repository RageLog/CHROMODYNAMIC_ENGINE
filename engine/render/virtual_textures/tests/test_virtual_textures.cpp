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

// ===========================================================================
// COMPREHENSIVE DEPTH — charter-complete host tests.
// All tests pin ACTUAL behaviour of EXISTING code; ADD-ONLY; no API/math/GLSL
// change; golden BYTE-IDENTICAL.
// ===========================================================================

// ---- PageId: max field values as distinct pages ---------------------------

// Each field at its type maximum is a valid PageId and compares unequal to the
// origin and to sibling pages that differ only in that one field.
TEST(VirtualTextures, PageIdMaxFieldsAreDistinctPages)
{
    constexpr PageId origin   { 0,      0,      0    };
    constexpr PageId max_x    { 0xFFFF, 0,      0    };
    constexpr PageId max_y    { 0,      0xFFFF, 0    };
    constexpr PageId max_mip  { 0,      0,      0xFF };
    constexpr PageId all_max  { 0xFFFF, 0xFFFF, 0xFF };

    EXPECT_FALSE(origin == max_x);
    EXPECT_FALSE(origin == max_y);
    EXPECT_FALSE(origin == max_mip);
    EXPECT_FALSE(max_x  == max_y);
    EXPECT_FALSE(max_x  == max_mip);
    EXPECT_FALSE(max_y  == max_mip);
    EXPECT_FALSE(origin == all_max);
    // All-max equals itself.
    EXPECT_EQ(all_max, (PageId { 0xFFFF, 0xFFFF, 0xFF }));
}

// ---- PageIdHash: band non-overlap proof for uint16 x/y and uint8 mip ------

// y << 8 tops at bit 23; x << 24 bottoms at bit 24 → zero overlap.
// mip max 0xFF occupies bits[0..7]; y << 8 starts at bit 8 → zero overlap.
// Verify the mathematical boundary: the bit directly below x-band == bit 23.
TEST(VirtualTextures, HashBandBoundaryXStartsAtBit24)
{
    const PageIdHash h {};
    // y=0x8000 (bit 15 set) → y<<8 puts the value at bit 23.
    // x=1 → x<<24 puts value at bit 24. They are adjacent, not overlapping.
    const std::size_t h_y   = h(PageId { 0, 0x8000, 0 });
    const std::size_t h_x   = h(PageId { 1, 0,      0 });
    EXPECT_EQ(h_y, std::size_t { 0x8000 } << 8);   // bit 23
    EXPECT_EQ(h_x, std::size_t { 1 }      << 24);  // bit 24
    EXPECT_EQ(h_y & h_x, std::size_t { 0 });        // non-overlapping
}

// y<<8 is strictly below x<<24: the max-y hash cannot alias any nonzero-x hash.
TEST(VirtualTextures, HashMaxYDoesNotAliasMinX)
{
    const PageIdHash h {};
    const std::size_t max_y_hash = h(PageId { 0,    0xFFFF, 0 });
    const std::size_t min_x_hash = h(PageId { 1,    0,      0 });
    EXPECT_EQ(max_y_hash, std::size_t { 0xFFFF } << 8);   // 0xFFFF00
    EXPECT_EQ(min_x_hash, std::size_t { 1 }      << 24);  // 0x1000000
    EXPECT_NE(max_y_hash, min_x_hash);
}

// Hash of max mip (0xFF) fills exactly bits[0..7] with no bleed into band y.
TEST(VirtualTextures, HashMaxMipStaysInBits0To7)
{
    const PageIdHash h {};
    const std::size_t max_mip_hash = h(PageId { 0, 0, 0xFF });
    EXPECT_EQ(max_mip_hash, std::size_t { 0xFF });
    // Confirm it does not touch bit 8 (the first y-band bit).
    EXPECT_EQ(max_mip_hash & (std::size_t { 1 } << 8), std::size_t { 0 });
}

// ---- AtlasSlot: default-constructed slot has valid == 0 (not ~0u) ----------

// The comment in the header says "~0u = invalid" but the struct code uses
// `valid { 0 }` for empty and sets `valid = 1` on allocation. Pin actual code.
TEST(VirtualTextures, AtlasSlotDefaultValidIsZero)
{
    const AtlasSlot s {};
    EXPECT_EQ(s.valid,  0U);
    EXPECT_EQ(s.slot_x, 0U);
    EXPECT_EQ(s.slot_y, 0U);
}

// An allocated slot always has valid == 1 (not some other nonzero value).
TEST(VirtualTextures, AllocatedSlotValidIsExactlyOne)
{
    PageTable t(4, 4);
    const AtlasSlot s = t.allocate({ 0, 0, 0 });
    EXPECT_EQ(s.valid, 1U);
}

// ---- AtlasSlot: 1×1 atlas (single slot, second alloc evicts first) ---------

TEST(VirtualTextures, SingleSlotAtlasEvictsOnSecondAlloc)
{
    PageTable t(1, 1);  // total capacity = 1
    const AtlasSlot first = t.allocate({ 10, 20, 0 });
    EXPECT_EQ(first.valid,  1U);
    EXPECT_EQ(first.slot_x, 0U);
    EXPECT_EQ(first.slot_y, 0U);
    EXPECT_EQ(t.resident_count(), 1U);

    // Second alloc evicts the first (only one slot available).
    const AtlasSlot second = t.allocate({ 11, 22, 0 });
    EXPECT_EQ(second.valid,  1U);
    EXPECT_EQ(second.slot_x, 0U);  // same physical slot reused
    EXPECT_EQ(second.slot_y, 0U);
    EXPECT_EQ(t.resident_count(), 1U);

    EXPECT_EQ(t.lookup({ 10, 20, 0 }), nullptr);  // first evicted
    EXPECT_NE(t.lookup({ 11, 22, 0 }), nullptr);  // second resident
}

// ---- FIFO eviction chain (3 allocations on a 2-slot atlas) -----------------

// Sequence: alloc A, alloc B (full), alloc C (evicts A), alloc D (evicts B).
// After D: only C and D are resident.
TEST(VirtualTextures, FifoChainEvictsInOrder)
{
    PageTable t(2, 1);  // 2 slots
    const PageId A { 1, 0, 0 };
    const PageId B { 2, 0, 0 };
    const PageId C { 3, 0, 0 };
    const PageId D { 4, 0, 0 };

    (void)t.allocate(A);  // slot 0 → oldest
    (void)t.allocate(B);  // slot 1 → full
    (void)t.allocate(C);  // evicts A (oldest), reuses slot 0
    (void)t.allocate(D);  // evicts B (now oldest), reuses slot 1

    EXPECT_EQ(t.resident_count(), 2U);
    EXPECT_EQ(t.lookup(A), nullptr);  // A evicted
    EXPECT_EQ(t.lookup(B), nullptr);  // B evicted
    EXPECT_NE(t.lookup(C), nullptr);  // C resident
    EXPECT_NE(t.lookup(D), nullptr);  // D resident
}

// The page most recently allocated is the last to be evicted.
TEST(VirtualTextures, FifoLastAllocatedSurvivesLongest)
{
    PageTable t(1, 1);  // single slot
    for (std::uint16_t i = 0; i < 5; ++i)
        (void)t.allocate({ i, 0, 0 });
    // After 5 allocations on a 1-slot atlas, only the last one survives.
    EXPECT_NE(t.lookup({ 4, 0, 0 }), nullptr);
    for (std::uint16_t i = 0; i < 4; ++i)
        EXPECT_EQ(t.lookup({ i, 0, 0 }), nullptr);
}

// ---- Multi-mip: max mip (0xFF) page is independent -----------------------

TEST(VirtualTextures, MaxMipPageIsIndependentResident)
{
    PageTable t(4, 4);
    const AtlasSlot mip0   = t.allocate({ 7, 3, 0    });
    const AtlasSlot mip255 = t.allocate({ 7, 3, 0xFF });

    EXPECT_EQ(t.resident_count(), 2U);
    EXPECT_NE(t.lookup({ 7, 3, 0    }), nullptr);
    EXPECT_NE(t.lookup({ 7, 3, 0xFF }), nullptr);
    // They occupy different physical slots.
    const bool different = (mip0.slot_x != mip255.slot_x) ||
                           (mip0.slot_y != mip255.slot_y);
    EXPECT_TRUE(different);
}

// ---- Negative: lookup on un-allocated mip → nullptr ----------------------

// Page (x,y,mip=0) is resident but (x,y,mip=1) is NOT — they are independent.
TEST(VirtualTextures, LookupUnallocatedMipReturnsNull)
{
    PageTable t(4, 4);
    (void)t.allocate({ 3, 3, 0 });
    EXPECT_EQ(t.lookup({ 3, 3, 1 }), nullptr);   // mip=1 never allocated
    EXPECT_EQ(t.lookup({ 3, 3, 2 }), nullptr);   // mip=2 never allocated
    EXPECT_NE(t.lookup({ 3, 3, 0 }), nullptr);   // mip=0 is resident
}

// ---- Negative: distinct (x,y) pages are independent ----------------------

TEST(VirtualTextures, LookupAdjacentXReturnsNull)
{
    PageTable t(4, 4);
    (void)t.allocate({ 0, 0, 0 });
    EXPECT_EQ(t.lookup({ 1, 0, 0 }), nullptr);
    EXPECT_EQ(t.lookup({ 0, 1, 0 }), nullptr);
}

// ---- Capacity: exact-fill does NOT evict ---------------------------------

// Filling exactly to capacity must not trigger any eviction.
TEST(VirtualTextures, ExactCapacityFillNoEviction)
{
    constexpr std::uint16_t W = 3;
    constexpr std::uint16_t H = 2;  // 6 slots
    PageTable t(W, H);
    for (std::uint16_t i = 0; i < 6; ++i)
        (void)t.allocate({ i, 0, 0 });
    EXPECT_EQ(t.resident_count(), 6U);
    // All 6 pages must still be resident — no eviction happened.
    for (std::uint16_t i = 0; i < 6; ++i)
        EXPECT_NE(t.lookup({ i, 0, 0 }), nullptr) << "page " << i << " was unexpectedly evicted";
}

// ---- Slot coordinate formula: general slot index n → (n%w, n/w) ----------

// Verify the last slot in a 3×2 atlas has coordinates (2, 1).
TEST(VirtualTextures, LastSlotCoordinatesAreCorrect)
{
    PageTable t(3, 2);  // 6 slots; last index = 5 → (5%3=2, 5/3=1)
    AtlasSlot slots[6];
    for (std::uint16_t i = 0; i < 6; ++i)
        slots[i] = t.allocate({ i, 0, 0 });
    EXPECT_EQ(slots[5].slot_x, 2U);
    EXPECT_EQ(slots[5].slot_y, 1U);
}

// ---- residents() view is a live reference, not a snapshot copy ------------

// The reference returned by residents() reflects subsequent allocations in the
// same scope, demonstrating it is the live internal map.
TEST(VirtualTextures, ResidentsViewIsLiveNotSnapshot)
{
    PageTable t(4, 4);
    const auto& view = t.residents();
    EXPECT_EQ(view.size(), 0U);

    (void)t.allocate({ 1, 2, 0 });
    EXPECT_EQ(view.size(), 1U);  // live: view updated without re-calling residents()

    (void)t.allocate({ 3, 4, 1 });
    EXPECT_EQ(view.size(), 2U);
}

// ---- PageIdHash: zero-field pages hash to zero ---------------------------

// All three fields zero → hash = (0<<24) ^ (0<<8) ^ 0 = 0.
TEST(VirtualTextures, HashOriginIsZero)
{
    const PageIdHash h {};
    EXPECT_EQ(h(PageId { 0, 0, 0 }), std::size_t { 0 });
}

// ---- Large atlas: correct slot index for allocation past 256 pages --------

// Allocate 257 pages on a 16×16 (256-slot) atlas; the 257th triggers eviction.
// The 256th allocation (index 255) must land at slot (15, 15) before eviction.
TEST(VirtualTextures, LargeAtlasLastSlotBeforeEviction)
{
    PageTable t(16, 16);  // 256 slots
    AtlasSlot last {};
    for (std::uint16_t i = 0; i < 256; ++i)
        last = t.allocate({ i, 0, 0 });
    // Index 255: 255 % 16 = 15, 255 / 16 = 15.
    EXPECT_EQ(last.slot_x, 15U);
    EXPECT_EQ(last.slot_y, 15U);
    EXPECT_EQ(t.resident_count(), 256U);

    // 257th page triggers FIFO eviction; count stays at 256.
    (void)t.allocate({ 256, 0, 0 });
    EXPECT_EQ(t.resident_count(), 256U);
    EXPECT_EQ(t.lookup({ 0, 0, 0 }), nullptr);    // oldest evicted
    EXPECT_NE(t.lookup({ 256, 0, 0 }), nullptr);  // newest resident
}

// ---- GLSL: kFeedbackGlsl structural contract -------------------------------

// Pin the GLSL buffer layout (set/binding), the struct name, the count field,
// and the atomic append pattern in addition to the existing token checks.
TEST(VirtualTextures, GlslFeedbackStructuralContract)
{
    constexpr std::string_view g = cd::virtual_textures::kFeedbackGlsl;
    // Buffer layout tokens.
    EXPECT_NE(g.find("layout(set = 0, binding = 5) buffer Feedback"), std::string_view::npos);
    EXPECT_NE(g.find("uint count"),                                    std::string_view::npos);
    EXPECT_NE(g.find("FeedbackReq reqs[]"),                            std::string_view::npos);
    // Struct name and pkg field.
    EXPECT_NE(g.find("struct FeedbackReq"),                            std::string_view::npos);
    EXPECT_NE(g.find("uvec4 pkg"),                                     std::string_view::npos);
    // Function signature.
    EXPECT_NE(g.find("void cd_vt_request(uvec2 page_xy, uint mip, uint frame_id)"),
              std::string_view::npos);
    // Atomic slot acquisition.
    EXPECT_NE(g.find("uint slot = atomicAdd(F.count, 1u)"), std::string_view::npos);
    // Assignment of pkg field.
    EXPECT_NE(g.find("F.reqs[slot].pkg"),                              std::string_view::npos);
}

// ---- GLSL: kFeedbackGlsl is a string_view (no embedded nul, no heap alloc) -

// Confirm the GLSL is a plain string_view backed by a string literal: its
// data() is non-null, size() > 0, and it does not contain an embedded NUL
// before the end (which would indicate truncation bugs).
TEST(VirtualTextures, GlslFeedbackIsWellFormedStringView)
{
    constexpr std::string_view g = cd::virtual_textures::kFeedbackGlsl;
    EXPECT_NE(g.data(), nullptr);
    EXPECT_GT(g.size(), std::size_t { 0 });
    // No embedded NUL before end.
    const auto nul_pos = g.find('\0');
    EXPECT_EQ(nul_pos, std::string_view::npos);
}

}  // namespace
