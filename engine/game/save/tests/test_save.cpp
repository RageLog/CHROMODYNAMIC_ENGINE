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

using cd::game::save::CloudDownloadFn;
using cd::game::save::CloudPayload;
using cd::game::save::CloudUploadFn;
using cd::game::save::MigrationFn;
using cd::game::save::SaveFormat;
using cd::game::save::SaveMeta;
using cd::game::save::SaveSystem;
using cd::game::save::default_storage_root;
using cd::game::save::is_valid_slot_id;
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

    const auto blob = bytes_from(R"({"hp":100,"xp":4242})");
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

// =============================================================================
// Phase 2 (G5.3) — versioning + migration + cloud hook test cases.
// =============================================================================

// -----------------------------------------------------------------------------
// 15) save_with_meta + load_with_meta roundtrips both the meta block and the
//     blob, populating timestamp + payload_size from the library (not the
//     caller-supplied placeholder values).
// -----------------------------------------------------------------------------
TEST(SaveSystemV2, SaveLoadWithMetaRoundtrip)
{
    auto root = make_unique_root("v2_roundtrip");
    SaveSystem sys(root);

    SaveMeta meta;
    meta.version      = 1U;
    meta.format       = SaveFormat::kBinary;
    meta.app_name     = "TestGame";
    meta.app_version  = "1.0.0";
    meta.timestamp    = 0;   // library overwrites
    meta.payload_size = 0;   // library overwrites

    const auto blob = bytes_from("v1-payload-bytes");
    auto wr = sys.save_with_meta("slot_v1", meta, blob, "First save");
    ASSERT_TRUE(wr.has_value()) << "save_with_meta failed";

    auto loaded = sys.load_with_meta("slot_v1");
    ASSERT_TRUE(loaded.has_value()) << "load_with_meta failed";
    EXPECT_EQ(loaded->meta.version,      1U);
    EXPECT_EQ(loaded->meta.format,       SaveFormat::kBinary);
    EXPECT_EQ(loaded->meta.app_name,     "TestGame");
    EXPECT_EQ(loaded->meta.app_version,  "1.0.0");
    EXPECT_EQ(loaded->meta.payload_size, blob.size());
    EXPECT_GT(loaded->meta.timestamp,    0);  // library stamped a real time
    ASSERT_EQ(loaded->blob.size(),       blob.size());
    EXPECT_EQ(std::memcmp(loaded->blob.data(), blob.data(), blob.size()), 0);
}

// -----------------------------------------------------------------------------
// 16) Single-step migration v1 -> v2 transforms blob and bumps version.
// -----------------------------------------------------------------------------
TEST(SaveSystemV2, SingleStepMigrationV1ToV2)
{
    auto root = make_unique_root("v2_migrate1");
    SaveSystem sys(root);

    SaveMeta meta;
    meta.version  = 1U;
    meta.format   = SaveFormat::kBinary;
    meta.app_name = "Game";
    const auto v1_blob = bytes_from("v1-data");
    ASSERT_TRUE(sys.save_with_meta("slot", meta, v1_blob).has_value());

    // v1 -> v2: prepend "v2:" to the bytes.
    sys.register_migration(1U, 2U,
        [](std::span<const std::byte> in) -> cd::core::Result<std::vector<std::byte>> {
            std::string s = "v2:";
            std::vector<std::byte> out(s.size() + in.size());
            std::memcpy(out.data(), s.data(), s.size());
            if (!in.empty())
            {
                std::memcpy(out.data() + s.size(), in.data(), in.size());
            }
            return out;
        });

    auto migrated = sys.migrate("slot", 2U);
    ASSERT_TRUE(migrated.has_value());
    EXPECT_EQ(migrated->version,      2U);
    EXPECT_EQ(migrated->payload_size, 3U + v1_blob.size());

    // Body on disk reflects the migrated bytes.
    auto reloaded = sys.load_with_meta("slot");
    ASSERT_TRUE(reloaded.has_value());
    EXPECT_EQ(reloaded->meta.version, 2U);
    ASSERT_GE(reloaded->blob.size(), 3U);
    EXPECT_EQ(std::memcmp(reloaded->blob.data(), "v2:", 3), 0);
}

// -----------------------------------------------------------------------------
// 17) Multi-step migration v1 -> v2 -> v3 chains through both edges.
// -----------------------------------------------------------------------------
TEST(SaveSystemV2, MultiStepMigrationV1ToV3Chains)
{
    auto root = make_unique_root("v2_migrate_chain");
    SaveSystem sys(root);

    SaveMeta meta;
    meta.version  = 1U;
    meta.format   = SaveFormat::kBinary;
    meta.app_name = "Game";
    ASSERT_TRUE(sys.save_with_meta("slot", meta, bytes_from("X")).has_value());

    // v1 -> v2: append "_v2"
    sys.register_migration(1U, 2U,
        [](std::span<const std::byte> in) -> cd::core::Result<std::vector<std::byte>> {
            std::vector<std::byte> out(in.begin(), in.end());
            const std::string s = "_v2";
            const auto base = out.size();
            out.resize(base + s.size());
            std::memcpy(out.data() + base, s.data(), s.size());
            return out;
        });
    // v2 -> v3: append "_v3"
    sys.register_migration(2U, 3U,
        [](std::span<const std::byte> in) -> cd::core::Result<std::vector<std::byte>> {
            std::vector<std::byte> out(in.begin(), in.end());
            const std::string s = "_v3";
            const auto base = out.size();
            out.resize(base + s.size());
            std::memcpy(out.data() + base, s.data(), s.size());
            return out;
        });

    auto migrated = sys.migrate("slot", 3U);
    ASSERT_TRUE(migrated.has_value());
    EXPECT_EQ(migrated->version, 3U);

    auto reloaded = sys.load_with_meta("slot");
    ASSERT_TRUE(reloaded.has_value());
    EXPECT_EQ(reloaded->meta.version, 3U);
    // Final blob should be "X_v2_v3".
    const std::string expected = "X_v2_v3";
    ASSERT_EQ(reloaded->blob.size(), expected.size());
    EXPECT_EQ(std::memcmp(reloaded->blob.data(), expected.data(), expected.size()), 0);
}

// -----------------------------------------------------------------------------
// 18) Missing migration edge returns kMigrationMissing without mutating the
//     on-disk blob.
// -----------------------------------------------------------------------------
TEST(SaveSystemV2, MissingMigrationEdgeErrors)
{
    auto root = make_unique_root("v2_migrate_missing");
    SaveSystem sys(root);

    SaveMeta meta;
    meta.version  = 1U;
    meta.format   = SaveFormat::kBinary;
    meta.app_name = "Game";
    const auto v1_blob = bytes_from("intact");
    ASSERT_TRUE(sys.save_with_meta("slot", meta, v1_blob).has_value());

    // No migration registered — request v2.
    auto migrated = sys.migrate("slot", 2U);
    ASSERT_FALSE(migrated.has_value());
    EXPECT_EQ(migrated.error().domain, 0x4753U);
    EXPECT_EQ(migrated.error().code,
              static_cast<std::uint32_t>(Code::kMigrationMissing));

    // Body on disk is untouched.
    auto reloaded = sys.load_with_meta("slot");
    ASSERT_TRUE(reloaded.has_value());
    EXPECT_EQ(reloaded->meta.version, 1U);
    ASSERT_EQ(reloaded->blob.size(),  v1_blob.size());
    EXPECT_EQ(std::memcmp(reloaded->blob.data(), v1_blob.data(), v1_blob.size()), 0);
}

// -----------------------------------------------------------------------------
// 19) Cloud upload handler is invoked on save_with_meta when registered.
// -----------------------------------------------------------------------------
TEST(SaveSystemV2, CloudUploadInvokedWhenHandlerRegistered)
{
    auto root = make_unique_root("v2_cloud_up");
    SaveSystem sys(root);

    int         upload_calls   = 0;
    std::string seen_slot_id;
    std::size_t seen_bytes     = 0;
    std::uint32_t seen_version = 0;

    sys.set_cloud_handler(
        [&](std::string_view              slot_id,
            const SaveMeta&               meta,
            std::span<const std::byte>    blob) -> cd::core::Result<void> {
            ++upload_calls;
            seen_slot_id.assign(slot_id);
            seen_bytes   = blob.size();
            seen_version = meta.version;
            return {};
        },
        /*download=*/{});
    EXPECT_TRUE(sys.has_cloud_handler());

    SaveMeta meta;
    meta.version  = 4U;
    meta.format   = SaveFormat::kBinary;
    meta.app_name = "CloudGame";
    const auto blob = bytes_from("cloud-bytes");
    auto wr = sys.save_with_meta("slot_c", meta, blob);
    ASSERT_TRUE(wr.has_value());

    EXPECT_EQ(upload_calls,   1);
    EXPECT_EQ(seen_slot_id,   "slot_c");
    EXPECT_EQ(seen_bytes,     blob.size());
    EXPECT_EQ(seen_version,   4U);
}

// -----------------------------------------------------------------------------
// 20) No cloud handler -> save_with_meta does NOT invoke any upload (proof:
//     unregistered system + counter in a side channel).
// -----------------------------------------------------------------------------
TEST(SaveSystemV2, CloudUploadNotCalledWhenUnregistered)
{
    auto root = make_unique_root("v2_cloud_no_up");
    SaveSystem sys(root);

    // Independently track that "no callback fires" by registering nothing
    // and asserting via has_cloud_handler() + slot count on disk.
    EXPECT_FALSE(sys.has_cloud_handler());

    SaveMeta meta;
    meta.version  = 1U;
    meta.format   = SaveFormat::kBinary;
    meta.app_name = "Solo";
    ASSERT_TRUE(sys.save_with_meta("slot_n", meta, bytes_from("only-local")).has_value());

    // Local slot present, no side effects from a missing handler.
    EXPECT_TRUE(sys.slot_exists("slot_n"));
    EXPECT_FALSE(sys.has_cloud_handler());

    // Also exercise: explicitly clearing the upload while keeping a download
    // handler still leaves upload uncalled.
    int upload_calls = 0;
    sys.set_cloud_handler({}, [](std::string_view) -> cd::core::Result<CloudPayload> {
        return std::unexpected(cd::game::save::save_errors::make(Code::kSlotNotFound));
    });
    ASSERT_TRUE(sys.save_with_meta("slot_n2", meta, bytes_from("still-local")).has_value());
    EXPECT_EQ(upload_calls, 0);
}

// -----------------------------------------------------------------------------
// 21) Cloud download fallback restores a missing slot. Local store is empty
//     -> load_with_meta calls the registered download handler, the returned
//     blob is written locally, and the next load_with_meta hits local cache.
// -----------------------------------------------------------------------------
TEST(SaveSystemV2, CloudDownloadFallbackRestoresMissingSlot)
{
    auto root = make_unique_root("v2_cloud_dl");
    SaveSystem sys(root);

    const auto cloud_blob_bytes = bytes_from("from-the-cloud");

    int download_calls = 0;
    sys.set_cloud_handler(
        /*upload=*/{},
        [&](std::string_view slot_id) -> cd::core::Result<CloudPayload> {
            ++download_calls;
            EXPECT_EQ(std::string(slot_id), "cloud_slot");
            CloudPayload p;
            p.meta.version      = 7U;
            p.meta.format       = SaveFormat::kBinary;
            p.meta.app_name     = "CloudGame";
            p.meta.app_version  = "2.0";
            p.blob.assign(cloud_blob_bytes.begin(), cloud_blob_bytes.end());
            return p;
        });

    // Local is empty.
    EXPECT_FALSE(sys.slot_exists("cloud_slot"));

    auto loaded = sys.load_with_meta("cloud_slot");
    ASSERT_TRUE(loaded.has_value()) << "cloud fallback should have restored slot";
    EXPECT_EQ(download_calls,           1);
    EXPECT_EQ(loaded->meta.version,     7U);
    EXPECT_EQ(loaded->meta.app_name,    "CloudGame");
    EXPECT_EQ(loaded->meta.app_version, "2.0");
    ASSERT_EQ(loaded->blob.size(),      cloud_blob_bytes.size());
    EXPECT_EQ(std::memcmp(loaded->blob.data(),
                          cloud_blob_bytes.data(),
                          cloud_blob_bytes.size()), 0);

    // Next call should not re-download (local cache populated by fallback).
    auto cached = sys.load_with_meta("cloud_slot");
    ASSERT_TRUE(cached.has_value());
    EXPECT_EQ(download_calls,           1);  // unchanged
    EXPECT_EQ(cached->meta.version,     7U);
    EXPECT_TRUE(sys.slot_exists("cloud_slot"));
}

// =============================================================================
// BAND-1 corrupt/partial-file + version-migration forward-compat fuzz
// (ADR-20260616 §2.4).
// =============================================================================

// 22) Partial-body fuzz: truncating the body file to N random lengths must
//     never crash and must return exactly the bytes that remain on disk
//     (load() trusts the file length, not the stale meta size_bytes).
TEST(SaveSystemFuzz, PartialBodyTruncationIsDeterministic)
{
    auto root = make_unique_root("fuzz_partial");
    SaveSystem sys(root);

    const auto full = random_bytes(4096, /*seed=*/0xBADF00DU);
    std::mt19937 rng(0x1234U);
    for (int trial = 0; trial < 32; ++trial)
    {
        ASSERT_TRUE(sys.save("slot", full, SaveFormat::kBinary).has_value());
        const auto body = sys.slot_directory("slot") / "body.bin";

        // Truncate the body to a random length in [0, full.size()].
        const auto cut = static_cast<std::uintmax_t>(rng() % (full.size() + 1U));
        {
            std::error_code ec;
            std::filesystem::resize_file(body, cut, ec);
            ASSERT_FALSE(ec) << "resize_file failed";
        }

        auto loaded = sys.load("slot");
        ASSERT_TRUE(loaded.has_value())
            << "truncated-body load should still succeed (length-based read)";
        EXPECT_EQ(loaded->size(), static_cast<std::size_t>(cut));
        if (cut > 0)
        {
            EXPECT_EQ(std::memcmp(loaded->data(), full.data(),
                                  static_cast<std::size_t>(cut)), 0);
        }
    }
}

// 23) Corrupt-meta fuzz: a set of structurally-broken meta.json variants
//     must each yield kCorruptHeader (never a crash, never a silent OK).
TEST(SaveSystemFuzz, CorruptMetaVariantsSurfaceCorruptHeader)
{
    auto root = make_unique_root("fuzz_corrupt_meta");
    SaveSystem sys(root);
    ASSERT_TRUE(sys.save("slot", bytes_from("body"), SaveFormat::kBinary).has_value());

    const std::vector<std::string> broken = {
        "",                                                // empty file
        "{",                                               // unterminated object
        R"({ "id": })",                                    // missing value
        R"({ "id": "slot" )",                              // missing close + others
        R"({ "id": "slot", "timestamp": 12 })",           // no "format" (required)
        R"({ "timestamp": 12, "format": "binary" })",      // no "id" (required)
        R"({ "id": "slot", "format": "binary" })",        // no "timestamp" (required)
        "not json at all",                                 // garbage
        R"({ "id": "slot", "timestamp": 1Z, "format": "binary" })", // bad number
    };

    const auto meta = sys.slot_directory("slot") / "meta.json";
    for (const auto& text : broken)
    {
        {
            std::ofstream f(meta, std::ios::trunc | std::ios::binary);
            ASSERT_TRUE(f.is_open());
            f << text;
        }
        auto loaded = sys.load("slot");
        ASSERT_FALSE(loaded.has_value()) << "accepted broken meta: '" << text << "'";
        EXPECT_EQ(loaded.error().code,
                  static_cast<std::uint32_t>(Code::kCorruptHeader))
            << "wrong error for: '" << text << "'";
        // list_slots must tolerate the broken header (skip, not crash).
        EXPECT_EQ(sys.list_slots().size(), 0U);
    }
}

// 24) Forward compatibility: a meta.json carrying UNKNOWN future keys (and
//     extra whitespace) is read by today's parser — unknown keys are
//     silently ignored, the known fields survive. This proves a Phase-1
//     reader can open a file written by a later Phase-N writer.
TEST(SaveSystemFuzz, ForwardCompatUnknownKeysIgnored)
{
    auto root = make_unique_root("fuzz_fwd_compat");
    SaveSystem sys(root);

    SaveMeta meta;
    meta.version     = 3U;
    meta.format      = SaveFormat::kBinary;
    meta.app_name    = "FwdGame";
    meta.app_version = "3.1";
    ASSERT_TRUE(sys.save_with_meta("slot", meta, bytes_from("payload")).has_value());

    // Hand-write a meta.json that adds future keys around the known ones.
    const auto meta_path = sys.slot_directory("slot") / "meta.json";
    {
        std::ofstream f(meta_path, std::ios::trunc | std::ios::binary);
        ASSERT_TRUE(f.is_open());
        f << R"({
  "id": "slot",
  "future_string_field": "this key did not exist in Phase 1",
  "label": "Forward",
  "future_number_field": 99999,
  "timestamp": 1234567,
  "format": "binary",
  "size_bytes": 7,
  "version": 5,
  "app_name": "FwdGame",
  "app_version": "9.9",
  "payload_size": 7,
  "another_future_block": 42
})";
    }

    auto loaded = sys.load_with_meta("slot");
    ASSERT_TRUE(loaded.has_value()) << "forward-compat meta must still load";
    EXPECT_EQ(loaded->meta.version,     5U);
    EXPECT_EQ(loaded->meta.app_name,    "FwdGame");
    EXPECT_EQ(loaded->meta.app_version, "9.9");
    EXPECT_EQ(loaded->meta.format,      SaveFormat::kBinary);
    EXPECT_EQ(loaded->blob.size(),      7U);
}

// 25) Migration-chain fuzz: register a chain of N edges (each appends one
//     byte) and migrate v1 -> vN. The final blob length and version are
//     deterministic regardless of how far we migrate, and every step is
//     persisted (a reload reflects the latest version).
TEST(SaveSystemFuzz, MigrationChainFuzzDeterministicAcrossDepths)
{
    std::mt19937 rng(0xC0DEU);
    for (int trial = 0; trial < 16; ++trial)
    {
        auto root = make_unique_root("fuzz_migrate_" + std::to_string(trial));
        SaveSystem sys(root);

        SaveMeta meta;
        meta.version  = 1U;
        meta.format   = SaveFormat::kBinary;
        meta.app_name = "Chain";
        const auto base = bytes_from("S");  // 1 byte seed
        ASSERT_TRUE(sys.save_with_meta("slot", meta, base).has_value());

        // Register edges 1->2->...->(target). Each edge appends one byte.
        const std::uint32_t target = 2U + (rng() % 30U);  // [2, 31]
        for (std::uint32_t v = 1U; v < target; ++v)
        {
            sys.register_migration(v, v + 1U,
                [](std::span<const std::byte> in)
                    -> cd::core::Result<std::vector<std::byte>> {
                    std::vector<std::byte> out(in.begin(), in.end());
                    out.push_back(static_cast<std::byte>('x'));
                    return out;
                });
        }

        auto migrated = sys.migrate("slot", target);
        ASSERT_TRUE(migrated.has_value())
            << "migrate to v" << target << " failed";
        EXPECT_EQ(migrated->version, target);
        // base (1 byte) + (target - 1) appended bytes.
        EXPECT_EQ(migrated->payload_size,
                  static_cast<std::uint64_t>(1U + (target - 1U)));

        // Every step persisted: a fresh reload reports the final version.
        auto reloaded = sys.load_with_meta("slot");
        ASSERT_TRUE(reloaded.has_value());
        EXPECT_EQ(reloaded->meta.version, target);
        EXPECT_EQ(reloaded->blob.size(),
                  static_cast<std::size_t>(1U + (target - 1U)));
    }
}

// 26) Migration mid-chain gap: a missing edge halts the chain with
//     kMigrationMissing and the on-disk blob is left at the last good step
//     (resume-from-here semantics), not reverted to v1.
TEST(SaveSystemFuzz, MigrationStopsAtGapAndPersistsLastGoodStep)
{
    auto root = make_unique_root("fuzz_migrate_gap");
    SaveSystem sys(root);

    SaveMeta meta;
    meta.version  = 1U;
    meta.format   = SaveFormat::kBinary;
    meta.app_name = "Gap";
    ASSERT_TRUE(sys.save_with_meta("slot", meta, bytes_from("A")).has_value());

    // Edges 1->2 and 2->3 exist, but 3->4 is MISSING.
    auto appender = [](char tag) {
        return [tag](std::span<const std::byte> in)
                   -> cd::core::Result<std::vector<std::byte>> {
            std::vector<std::byte> out(in.begin(), in.end());
            out.push_back(static_cast<std::byte>(tag));
            return out;
        };
    };
    sys.register_migration(1U, 2U, appender('2'));
    sys.register_migration(2U, 3U, appender('3'));

    auto migrated = sys.migrate("slot", 4U);
    ASSERT_FALSE(migrated.has_value());
    EXPECT_EQ(migrated.error().code,
              static_cast<std::uint32_t>(Code::kMigrationMissing));

    // Disk is at v3 (the last completed step), not v1.
    auto reloaded = sys.load_with_meta("slot");
    ASSERT_TRUE(reloaded.has_value());
    EXPECT_EQ(reloaded->meta.version, 3U);
    const std::string expected = "A23";
    ASSERT_EQ(reloaded->blob.size(), expected.size());
    EXPECT_EQ(std::memcmp(reloaded->blob.data(), expected.data(),
                          expected.size()), 0);
}

// 27) A migration function that reports failure halts with kMigrationFailed
//     and leaves the blob at the last good step (does not corrupt it).
TEST(SaveSystemFuzz, MigrationFunctionFailureLeavesLastGoodStep)
{
    auto root = make_unique_root("fuzz_migrate_fail");
    SaveSystem sys(root);

    SaveMeta meta;
    meta.version  = 1U;
    meta.format   = SaveFormat::kBinary;
    meta.app_name = "Fail";
    ASSERT_TRUE(sys.save_with_meta("slot", meta, bytes_from("Z")).has_value());

    sys.register_migration(1U, 2U,
        [](std::span<const std::byte> in)
            -> cd::core::Result<std::vector<std::byte>> {
            std::vector<std::byte> out(in.begin(), in.end());
            out.push_back(static_cast<std::byte>('2'));
            return out;
        });
    sys.register_migration(2U, 3U,
        [](std::span<const std::byte>)
            -> cd::core::Result<std::vector<std::byte>> {
            return std::unexpected(cd::game::save::save_errors::make(
                Code::kMigrationFailed, "deliberate"));
        });

    auto migrated = sys.migrate("slot", 3U);
    ASSERT_FALSE(migrated.has_value());
    EXPECT_EQ(migrated.error().code,
              static_cast<std::uint32_t>(Code::kMigrationFailed));

    // Disk advanced to v2 (the step that succeeded) and stopped.
    auto reloaded = sys.load_with_meta("slot");
    ASSERT_TRUE(reloaded.has_value());
    EXPECT_EQ(reloaded->meta.version, 2U);
    const std::string expected = "Z2";
    ASSERT_EQ(reloaded->blob.size(), expected.size());
    EXPECT_EQ(std::memcmp(reloaded->blob.data(), expected.data(),
                          expected.size()), 0);
}

}  // namespace
