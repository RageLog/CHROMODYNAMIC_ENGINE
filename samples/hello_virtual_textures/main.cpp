// =============================================================================
// CHROMODYNAMIC — samples/hello_virtual_textures
//
// CPU smoke for cd::virtual_textures (Mittring 2008 / Hollander 2013).
// Allocates a tiny 4×4-slot atlas page table, requests 20 distinct
// pages (more than capacity), verifies FIFO eviction kicks in + the
// table reports the correct resident count after each phase.
// =============================================================================
#include <cd/core/Version.hpp>
#include <cd/virtual_textures/VirtualTextures.hpp>

#include <cstdio>

int main()
{
    std::printf("CHROMODYNAMIC %u.%u.%u — hello_virtual_textures\n",
                static_cast<unsigned>(cd::core::kEngineVersion.major),
                static_cast<unsigned>(cd::core::kEngineVersion.minor),
                static_cast<unsigned>(cd::core::kEngineVersion.patch));

    namespace vt = cd::virtual_textures;

    vt::PageTable pt { 4, 4 };  // 16 slots total
    std::printf("  atlas: 4x4 = 16 slots, FIFO eviction\n");

    // Phase 1: request 16 unique pages — all fit.
    for (std::uint16_t i = 0; i < 16; ++i)
    {
        const auto p = vt::PageId { i, 0, 0 };
        (void)pt.allocate(p);
    }
    if (pt.resident_count() != 16)
    {
        std::printf("FAIL — phase 1 expected 16 resident, got %zu\n",
                    pt.resident_count());
        return 1;
    }
    std::printf("  phase 1: 16 unique pages → resident_count = %zu  OK\n",
                pt.resident_count());

    // Phase 2: request 4 NEW pages — must evict the first 4.
    for (std::uint16_t i = 0; i < 4; ++i)
    {
        const auto p = vt::PageId { i, 1, 0 };
        (void)pt.allocate(p);
    }
    if (pt.resident_count() != 16)
    {
        std::printf("FAIL — phase 2 expected 16 resident (post-evict), got %zu\n",
                    pt.resident_count());
        return 2;
    }
    // The first 4 of phase 1 should have been evicted.
    if (pt.lookup({ 0, 0, 0 }) != nullptr ||
        pt.lookup({ 1, 0, 0 }) != nullptr ||
        pt.lookup({ 2, 0, 0 }) != nullptr ||
        pt.lookup({ 3, 0, 0 }) != nullptr)
    {
        std::printf("FAIL — early pages should have been evicted\n");
        return 3;
    }
    // But the 5th-16th of phase 1 + the 4 new ones should still be resident.
    if (pt.lookup({ 4, 0, 0 }) == nullptr ||
        pt.lookup({ 0, 1, 0 }) == nullptr)
    {
        std::printf("FAIL — recent pages should still be resident\n");
        return 4;
    }
    std::printf("  phase 2: 4 evictions completed, recent pages survive  OK\n");

    // Phase 3: lookup non-resident returns null.
    if (pt.lookup({ 99, 99, 0 }) != nullptr)
    {
        std::printf("FAIL — lookup of unallocated page returned non-null\n");
        return 5;
    }
    std::printf("  phase 3: lookup of unallocated PageId returns null  OK\n");

    std::printf("[hello_virtual_textures] PARITY OK\n");
    return 0;
}
