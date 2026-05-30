// =============================================================================
// CHROMODYNAMIC — tests/test_save.cpp
// Phase 470 — cd::game::save::SaveSystem unit tests.
//
// Each test gets a unique scratch storage root under the gtest temp dir so we
// can run in parallel without slot collisions and leave nothing behind on the
// developer's real user-data path.
// =============================================================================
#include <cd/game/save/Save.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{

using cd::game::save::SaveFormat;
using cd::game::save::SaveSlot;
using cd::game::save::SaveSystem;
using cd::game::save::is_valid_slot_id;
using cd::game::save::default_storage_root;
using cd::game::save::save_errors::Code;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
namespace
{
std::atomic<std::uint64_t> g_root_seq { 0 };

std::filesystem::path make_unique_root(std::string_view tag)
{
    const auto seq = g_root_seq.fetch_add(1, std::memory_order_relaxed);
    const auto ts  = std::chrono::steady_clock::now().time_since_epoch().count();
    auto dir = std::filesystem::temp_directory_path() /
               (std::string("cd_save_test_") + std::string(tag) +
                "_" + std::to_string(seq) + "_" + std::to_string(ts));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

std::vector<std::byte> bytes_from(std::string_view s)
{
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

std::vector<std::byte> random_bytes(std::size_t n, std::uint32_t seed)
{
    std::vector<std::byte> out(n);
    std::mt19937 rng(seed);
    for (std::size_t i = 0; i < n; ++i)
    {
        out[i] = static_cast<std::byte>(rng() & 0xFFU);
    }
    return out;
}
}  // namespace

// -----------------------------------------------------------------------------
// 1) Save then load roundtrip preserves bytes verbatim.
// -----------------------------------------------------------------------------
TEST(SaveSystem, SaveLoadRoundtrip)
{
    auto root = make_unique_root("roundtrip");
    SaveSystem sys(root);

    const auto blob = bytes_from("Hello, save!");
    auto r = sys.save("slot_01", blob, SaveFormat::kBinary, "Test slot");
    ASSERT_TRUE(r.has_value()) << "save failed";

    auto loaded = sys.load("slot_01");
    ASSERT_TRUE(loaded.has_value()) << "load failed";
    EXPECT_EQ(loaded->size(), blob.size());
    EXPECT_EQ(std::memcmp(loaded->data(), blob.data(), blob.size()), 0);
}

// -----------------------------------------------------------------------------
// 2) Multiple slots are independent.
// -----------------------------------------------------------------------------
TEST(SaveSystem, MultipleSlotsIndependent)
{
    auto root = make_unique_root("multi");
    SaveSystem sys(root);

    const auto blob_a = bytes_from("alpha");
    const auto blob_b = bytes_from("beta-content-longer");
    const auto blob_c = bytes_from("");  // empty body permitted

    ASSERT_TRUE(sys.save("a", blob_a, SaveFormat::kBinary).has_value());
    ASSERT_TRUE(sys.save("b", blob_b, SaveFormat::kJson).has_value());
    ASSERT_TRUE(sys.save("c", blob_c, SaveFormat::kBinary).has_value());

    auto la = sys.load("a");
    auto lb = sys.load("b");
    auto lc = sys.load("c");
    ASSERT_TRUE(la.has_value());
    ASSERT_TRUE(lb.has_value());
    ASSERT_TRUE(lc.has_value());
    EXPECT_EQ(la->size(), blob_a.size());
    EXPECT_EQ(lb->size(), blob_b.size());
    EXPECT_EQ(lc->size(), blob_c.size());
    EXPECT_EQ(std::memcmp(la->data(), blob_a.data(), blob_a.size()), 0);
    EXPECT_EQ(std::memcmp(lb->data(), blob_b.data(), blob_b.size()), 0);
}

// -----------------------------------------------------------------------------
// 3) delete_slot removes the slot from list and load returns kSlotNotFound.
// -----------------------------------------------------------------------------
TEST(SaveSystem, DeleteRemovesSlotFromList)
{
    auto root = make_unique_root("delete");
    SaveSystem sys(root);

    ASSERT_TRUE(sys.save("a", bytes_from("aaa"), SaveFormat::kBinary).has_value());
    ASSERT_TRUE(sys.save("b", bytes_from("bbb"), SaveFormat::kBinary).has_value());

    EXPECT_EQ(sys.list_slots().size(), 2U);

    auto del = sys.delete_slot("a");
    ASSERT_TRUE(del.has_value());

    const auto slots = sys.list_slots();
    EXPECT_EQ(slots.size(), 1U);
    EXPECT_EQ(slots.front().id, "b");

    auto loaded = sys.load("a");
    ASSERT_FALSE(loaded.has_value());
    EXPECT_EQ(loaded.error().domain, 0x4753U);
    EXPECT_EQ(loaded.error().code, static_cast<std::uint32_t>(Code::kSlotNotFound));
}

// -----------------------------------------------------------------------------
// 4) list_slots is sorted by timestamp (most-recent first).
// -----------------------------------------------------------------------------
TEST(SaveSystem, ListSortedByTimestampDescending)
{
    auto root = make_unique_root("sort");
    SaveSystem sys(root);

    // Write three slots with controlled timestamps by patching meta.json.
    ASSERT_TRUE(sys.save("oldest",   bytes_from("o"), SaveFormat::kBinary).has_value());
    ASSERT_TRUE(sys.save("middle",   bytes_from("m"), SaveFormat::kBinary).has_value());
    ASSERT_TRUE(sys.save("newest",   bytes_from("n"), SaveFormat::kBinary).has_value());

    auto patch_ts = [&](const std::string& slot, std::int64_t ts) {
        auto p = sys.slot_directory(slot) / "meta.json";
        std::ifstream f(p);
        std::string txt((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
        f.close();
        // Replace timestamp value (rewrite-and-rename to keep atomicity).
        const auto pos = txt.find("\"timestamp\": ");
        ASSERT_NE(pos, std::string::npos);
        const auto end = txt.find_first_of(",\n", pos);
        txt.replace(pos, end - pos,
                    "\"timestamp\": " + std::to_string(ts));
        std::ofstream o(p, std::ios::trunc);
        o << txt;
    };
    patch_ts("oldest", 1000);
    patch_ts("middle", 2000);
    patch_ts("newest", 3000);

    const auto slots = sys.list_slots();
    ASSERT_EQ(slots.size(), 3U);
    EXPECT_EQ(slots[0].id, "newest");
    EXPECT_EQ(slots[1].id, "middle");
    EXPECT_EQ(slots[2].id, "oldest");
    EXPECT_GT(slots[0].timestamp, slots[1].timestamp);
    EXPECT_GT(slots[1].timestamp, slots[2].timestamp);
}

// -----------------------------------------------------------------------------
// 5) Atomic write: simulate "kill mid-write" by leaving a stale .tmp file and
//    verify the previously-saved body remains intact and loadable.
//
//    The implementation always writes to body.<ext>.tmp first and renames; if
//    a crash interrupts after the tmp is written but before the rename, the
//    previous body file must still be the one observed. We simulate this by
//    1) saving v1, 2) manually dropping a half-written tmp file next to the
//    body, 3) re-loading and confirming v1 is intact.
// -----------------------------------------------------------------------------
TEST(SaveSystem, AtomicWriteNoCorruptionMidWrite)
{
    auto root = make_unique_root("atomic");
    SaveSystem sys(root);

    const auto v1 = bytes_from("version-one-body-data");
    ASSERT_TRUE(sys.save("slot", v1, SaveFormat::kBinary).has_value());

    // Drop a half-written tmp body next to the real body — simulates the
    // crash window between "tmp file written" and "rename succeeded".
    const auto tmp = sys.slot_directory("slot") / "body.bin.tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        const std::string junk = "PARTIAL_GARBAGE_THAT_MUST_NOT_BE_LOADED";
        f.write(junk.data(), static_cast<std::streamsize>(junk.size()));
    }
    ASSERT_TRUE(std::filesystem::exists(tmp));

    auto loaded = sys.load("slot");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->size(), v1.size());
    EXPECT_EQ(std::memcmp(loaded->data(), v1.data(), v1.size()), 0);
}

// -----------------------------------------------------------------------------
// 6) JSON format roundtrip preserves bytes (binary-safe even for kJson —
//    the library does not transcode, only labels the extension).
// -----------------------------------------------------------------------------
TEST(SaveSystem, JsonFormatRoundtripPreservesBytes)
{
    auto root = make_unique_root("json");
    SaveSystem sys(root);

    const auto blob = bytes_from("{\"hp\":100,\"xp\":4242}");
    ASSERT_TRUE(sys.save("slot", blob, SaveFormat::kJson).has_value());

    auto loaded = sys.load("slot", SaveFormat::kJson);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->size(), blob.size());
    EXPECT_EQ(std::memcmp(loaded->data(), blob.data(), blob.size()), 0);

    const auto slots = sys.list_slots();
    ASSERT_EQ(slots.size(), 1U);
    EXPECT_EQ(slots.front().format, SaveFormat::kJson);
}

// -----------------------------------------------------------------------------
// 7) Binary format roundtrip preserves bytes for non-text (random) payload.
// -----------------------------------------------------------------------------
TEST(SaveSystem, BinaryFormatRoundtripPreservesBytes)
{
    auto root = make_unique_root("binary");
    SaveSystem sys(root);

    const auto blob = random_bytes(8192, /*seed=*/0xC0FFEEU);
    ASSERT_TRUE(sys.save("slot", blob, SaveFormat::kBinary).has_value());

    auto loaded = sys.load("slot", SaveFormat::kBinary);
    ASSERT_TRUE(loaded.has_value());
    ASSERT_EQ(loaded->size(), blob.size());
    EXPECT_EQ(std::memcmp(loaded->data(), blob.data(), blob.size()), 0);
}

// -----------------------------------------------------------------------------
// 8) Missing slot returns kSlotNotFound (not kInvalidSlotId, not kIoFailed).
// -----------------------------------------------------------------------------
TEST(SaveSystem, MissingSlotReturnsNotFound)
{
    auto root = make_unique_root("missing");
    SaveSystem sys(root);

    auto loaded = sys.load("ghost");
    ASSERT_FALSE(loaded.has_value());
    EXPECT_EQ(loaded.error().domain, 0x4753U);
    EXPECT_EQ(loaded.error().code, static_cast<std::uint32_t>(Code::kSlotNotFound));

    auto del = sys.delete_slot("ghost");
    ASSERT_FALSE(del.has_value());
    EXPECT_EQ(del.error().code, static_cast<std::uint32_t>(Code::kSlotNotFound));

    EXPECT_FALSE(sys.slot_exists("ghost"));
}

// -----------------------------------------------------------------------------
// 9) Storage root is configurable — setting a new root makes the original
//    slots invisible, then setting the old root back makes them visible again.
// -----------------------------------------------------------------------------
TEST(SaveSystem, StorageRootConfigurable)
{
    auto root_a = make_unique_root("root_a");
    auto root_b = make_unique_root("root_b");
    SaveSystem sys(root_a);

    ASSERT_TRUE(sys.save("only_a", bytes_from("AAAA"), SaveFormat::kBinary).has_value());
    EXPECT_EQ(sys.list_slots().size(), 1U);

    const auto previous = sys.set_storage_root(root_b);
    EXPECT_EQ(previous, root_a);
    EXPECT_EQ(sys.list_slots().size(), 0U);
    EXPECT_FALSE(sys.slot_exists("only_a"));

    ASSERT_TRUE(sys.save("only_b", bytes_from("BBBB"), SaveFormat::kBinary).has_value());
    EXPECT_EQ(sys.list_slots().size(), 1U);

    sys.set_storage_root(root_a);
    EXPECT_EQ(sys.list_slots().size(), 1U);
    EXPECT_TRUE(sys.slot_exists("only_a"));
    EXPECT_FALSE(sys.slot_exists("only_b"));
}

// -----------------------------------------------------------------------------
// 10) Invalid slot id rejected — for save / load / delete and standalone
//     validation API.
// -----------------------------------------------------------------------------
TEST(SaveSystem, InvalidSlotIdRejected)
{
    auto root = make_unique_root("invalid");
    SaveSystem sys(root);

    // Backing storage for the long-id case keeps the string_view valid.
    const std::string too_long(65, 'a');
    const std::vector<std::string_view> bad = {
        std::string_view{""},                 // empty
        std::string_view{"../escape"},        // path traversal
        std::string_view{"with space"},       // whitespace
        std::string_view{"with/slash"},       // separator
        std::string_view{"with\\backslash"},  // separator
        std::string_view{"tab\there"},        // control char
        std::string_view{".leading"},         // leading dot
        std::string_view{"trailing."},        // trailing dot
        std::string_view{"CON"},              // reserved on Windows
        std::string_view{"com1"},             // reserved on Windows
        std::string_view{too_long},           // > 64 chars
    };
    for (auto b : bad)
    {
        EXPECT_FALSE(is_valid_slot_id(b)) << "expected invalid: '" << b << "'";
        auto r = sys.save(b, bytes_from("x"), SaveFormat::kBinary);
        ASSERT_FALSE(r.has_value()) << "save accepted bad id: '" << b << "'";
        EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kInvalidSlotId));
        auto l = sys.load(b);
        ASSERT_FALSE(l.has_value());
        EXPECT_EQ(l.error().code, static_cast<std::uint32_t>(Code::kInvalidSlotId));
        auto d = sys.delete_slot(b);
        ASSERT_FALSE(d.has_value());
        EXPECT_EQ(d.error().code, static_cast<std::uint32_t>(Code::kInvalidSlotId));
    }

    // Sanity — a few good ids accepted.
    EXPECT_TRUE(is_valid_slot_id("slot_01"));
    EXPECT_TRUE(is_valid_slot_id("Auto-Save-2026-05-30"));
    EXPECT_TRUE(is_valid_slot_id("a"));
}

// -----------------------------------------------------------------------------
// 11) Overwrite — saving a slot twice keeps only the latest body and the
//     header reflects the new size.
// -----------------------------------------------------------------------------
TEST(SaveSystem, OverwriteReplacesBody)
{
    auto root = make_unique_root("overwrite");
    SaveSystem sys(root);

    const auto v1 = bytes_from("v1");
    const auto v2 = bytes_from("version-two-much-larger-body");
    ASSERT_TRUE(sys.save("slot", v1, SaveFormat::kBinary).has_value());
    ASSERT_TRUE(sys.save("slot", v2, SaveFormat::kBinary, "Updated").has_value());

    auto loaded = sys.load("slot");
    ASSERT_TRUE(loaded.has_value());
    ASSERT_EQ(loaded->size(), v2.size());
    EXPECT_EQ(std::memcmp(loaded->data(), v2.data(), v2.size()), 0);

    const auto slots = sys.list_slots();
    ASSERT_EQ(slots.size(), 1U);
    EXPECT_EQ(slots.front().label, "Updated");
    EXPECT_EQ(slots.front().size_bytes, v2.size());
}

// -----------------------------------------------------------------------------
// 12) Format flip — changing format from kBinary to kJson cleans up the old
//     body file (no stale leftovers).
// -----------------------------------------------------------------------------
TEST(SaveSystem, FormatFlipCleansOldBody)
{
    auto root = make_unique_root("flip");
    SaveSystem sys(root);

    ASSERT_TRUE(sys.save("slot", bytes_from("bin"), SaveFormat::kBinary).has_value());
    EXPECT_TRUE(std::filesystem::exists(sys.slot_directory("slot") / "body.bin"));

    ASSERT_TRUE(sys.save("slot", bytes_from("{}"), SaveFormat::kJson).has_value());
    EXPECT_TRUE (std::filesystem::exists(sys.slot_directory("slot") / "body.json"));
    EXPECT_FALSE(std::filesystem::exists(sys.slot_directory("slot") / "body.bin"));

    // load(slot) auto-detects format via meta.json.
    auto loaded = sys.load("slot");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->size(), 2U);

    // Explicit-format mismatch surfaces kFormatMismatch.
    auto mismatch = sys.load("slot", SaveFormat::kBinary);
    ASSERT_FALSE(mismatch.has_value());
    EXPECT_EQ(mismatch.error().code, static_cast<std::uint32_t>(Code::kFormatMismatch));
}

// -----------------------------------------------------------------------------
// 13) Corrupt header — manually trashing meta.json causes load to return
//     kCorruptHeader (and list_slots silently skips the slot, never throws).
// -----------------------------------------------------------------------------
TEST(SaveSystem, CorruptHeaderSurfacesError)
{
    auto root = make_unique_root("corrupt");
    SaveSystem sys(root);

    ASSERT_TRUE(sys.save("slot", bytes_from("body"), SaveFormat::kBinary).has_value());

    // Overwrite meta.json with garbage.
    {
        std::ofstream f(sys.slot_directory("slot") / "meta.json",
                        std::ios::trunc);
        f << "this is not json";
    }

    auto loaded = sys.load("slot");
    ASSERT_FALSE(loaded.has_value());
    EXPECT_EQ(loaded.error().code, static_cast<std::uint32_t>(Code::kCorruptHeader));

    // list_slots tolerates the bad header silently.
    EXPECT_EQ(sys.list_slots().size(), 0U);
}

// -----------------------------------------------------------------------------
// 14) default_storage_root is computable and ends in `CHROMODYNAMIC/saves`.
// -----------------------------------------------------------------------------
TEST(SaveSystem, DefaultStorageRootContainsBranding)
{
    const auto root = default_storage_root();
    EXPECT_FALSE(root.empty());
    const auto s = root.generic_string();
    EXPECT_NE(s.find("CHROMODYNAMIC/saves"), std::string::npos)
        << "default root was: " << s;
}

}  // namespace
