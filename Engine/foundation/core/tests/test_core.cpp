// =============================================================================
// CHROMODYNAMIC — cd::core unit tests
// Phase 2 Sprint S2.0 — smoke + ErrorCode + Result + Version
// =============================================================================
#include <cd/core/CVar.hpp>
#include <cd/core/Compat.hpp>
#include <cd/core/Defines.hpp>
#include <cd/core/Definitions.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Handle.hpp>
#include <cd/core/HandleStore.hpp>
#include <cd/core/Result.hpp>
#include <cd/core/SourceLocation.hpp>
#include <cd/core/Version.hpp>
#include <gtest/gtest.h>

#include <string>
#include <type_traits>

namespace
{

TEST(CoreSmoke, EngineNameAndVersion)
{
    EXPECT_EQ(cd::core::kEngineName, "CHROMODYNAMIC");
    EXPECT_EQ(cd::core::kEngineVersion.major, 0);
    EXPECT_EQ(cd::core::kEngineVersion.minor, 1);
    EXPECT_EQ(cd::core::kEngineVersion.patch, 0);
    EXPECT_GT(cd::core::kEngineVersion.packed(), 0u);
}

TEST(CoreDefines, PlatformDetected)
{
#if defined(CD_PLATFORM_WINDOWS) || defined(CD_PLATFORM_LINUX) || defined(CD_PLATFORM_MACOS)
    SUCCEED();
#else
    FAIL() << "no recognized CD_PLATFORM_* macro";
#endif
}

TEST(CoreDefines, CompilerDetected)
{
#if defined(CD_COMPILER_MSVC) || defined(CD_COMPILER_CLANG) || defined(CD_COMPILER_CLANG_CL) || defined(CD_COMPILER_GCC)
    SUCCEED();
#else
    FAIL() << "no recognized CD_COMPILER_* macro";
#endif
}

TEST(CoreDefines, ArchitectureDetected)
{
#if defined(CD_ARCH_X86_64) || defined(CD_ARCH_ARM64) || defined(CD_ARCH_RISCV64)
    SUCCEED();
#else
    FAIL() << "no recognized CD_ARCH_* macro";
#endif
}

TEST(CoreDefines, CacheLineSizePositive)
{
    EXPECT_GT(cd::core::kCacheLineSize, 0u);
    // Most desktop x86_64 + ARM64 use 64 bytes; some ARM (Apple Silicon) 128.
    EXPECT_LE(cd::core::kCacheLineSize, 256u);
}

TEST(CoreErrorCode, DefaultIsOk)
{
    cd::core::ErrorCode e;
    EXPECT_TRUE(e.ok());
    EXPECT_FALSE(static_cast<bool>(e));
    EXPECT_EQ(e, cd::core::kNoError);
}

TEST(CoreErrorCode, MakeFromCoreCode)
{
    auto e = cd::core::core_errors::make(cd::core::core_errors::Code::kInvalidArgument, "x must be >= 0");
    EXPECT_FALSE(e.ok());
    EXPECT_TRUE(static_cast<bool>(e));
    EXPECT_EQ(e.code, static_cast<std::uint32_t>(cd::core::core_errors::Code::kInvalidArgument));
    EXPECT_EQ(e.message, "x must be >= 0");
}

TEST(CoreErrorCode, Equality)
{
    auto a = cd::core::core_errors::make(cd::core::core_errors::Code::kNotFound);
    auto b = cd::core::core_errors::make(cd::core::core_errors::Code::kNotFound, "different msg");
    EXPECT_EQ(a, b);  // domain+code equality (message is informational)
}

// --- Result<T> smoke -----------------------------------------------------
cd::core::Result<int> parse_positive(int x)
{
    if (x < 0)
    {
        return cd::core::fail(cd::core::core_errors::Code::kInvalidArgument, "negative");
    }
    return x;
}

TEST(CoreResult, SuccessAndFailure)
{
    auto ok = parse_positive(7);
    ASSERT_TRUE(ok.has_value());
    EXPECT_EQ(*ok, 7);

    auto err = parse_positive(-1);
    ASSERT_FALSE(err.has_value());
    EXPECT_EQ(err.error().code, static_cast<std::uint32_t>(cd::core::core_errors::Code::kInvalidArgument));
    EXPECT_EQ(err.error().message, "negative");
}

TEST(CoreResult, MonadicCompose)
{
    auto r = parse_positive(3).transform(
        [](int x)
        {
            return x * 2;
        }
    );
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, 6);

    auto r2 = parse_positive(-2).transform(
        [](int x)
        {
            return x * 2;
        }
    );
    EXPECT_FALSE(r2.has_value());
}

TEST(CoreResult, VoidResult)
{
    auto fn = [](bool ok) -> cd::core::Result<void>
    {
        if (!ok)
            return cd::core::fail(cd::core::core_errors::Code::kAborted);
        return {};
    };
    EXPECT_TRUE(fn(true).has_value());
    EXPECT_FALSE(fn(false).has_value());
}

// --- Macro behavioral smoke (compile-time only) --------------------------
TEST(CoreDefines, NodiscardCompiles)
{
    struct S
    {
        CD_NODISCARD int compute() const noexcept
        {
            return 1;
        }
    };

    S s;
    EXPECT_EQ(s.compute(), 1);
}

TEST(CoreDefines, IgnoreUnusedNoop)
{
    int x = 42;
    CD_IGNORE_UNUSED(x);
    EXPECT_EQ(x, 42);
}

// --- DfH-salvaged Definitions.hpp smoke ----------------------------------
TEST(CoreDefinitions, OsDetected)
{
    // At least one of these must be 1 (per Definitions.hpp section 04).
#if CD_OS_WINDOWS
    EXPECT_TRUE(CD_OS_WINDOWS);
#elif CD_OS_LINUX
    EXPECT_TRUE(CD_OS_LINUX);
#elif CD_OS_MACOS
    EXPECT_TRUE(CD_OS_MACOS);
#else
    FAIL() << "no CD_OS_* flag set";
#endif
    EXPECT_NE(std::string_view { CD_OS_NAME }, std::string_view { "Unknown" });
}

TEST(CoreDefinitions, CompilerName)
{
    std::string_view name { CD_COMPILER_NAME };
    EXPECT_FALSE(name.empty());
    EXPECT_NE(name, "Unknown");
}

TEST(CoreDefinitions, ArchName)
{
    std::string_view name { CD_ARCH_NAME };
    EXPECT_FALSE(name.empty());
    EXPECT_NE(name, "Unknown");
}

TEST(CoreDefinitions, StringifyConcat)
{
    static_assert(std::string_view { CD_STRINGIFY(foo) } == "foo");
    static_assert(
        []
        {
            auto x = CD_CONCAT(42, 1);
            return x;
        }() == 421
    );
}

TEST(CoreDefinitions, VersionEncode)
{
    static_assert(CD_VERSION_ENCODE(1, 2, 3) == 10203);
}

// --- Compat smoke --------------------------------------------------------
TEST(CoreCompat, CopyCString)
{
    char buf[8] = {};
    cd::core::compat::copy_c_string(buf, sizeof(buf), "hello");
    EXPECT_STREQ(buf, "hello");

    cd::core::compat::copy_c_string(buf, sizeof(buf), "very-long-string-truncate");
    EXPECT_EQ(std::strlen(buf), 7u);  // size-1 cap
}

TEST(CoreCompat, Popcount64)
{
    EXPECT_EQ(cd::core::compat::popcount64(0u), 0);
    EXPECT_EQ(cd::core::compat::popcount64(0xFFu), 8);
    EXPECT_EQ(cd::core::compat::popcount64(~std::uint64_t { 0 }), 64);
}

TEST(CoreCompat, LocalTimeReturnsValid)
{
    std::time_t now = std::time(nullptr);
    auto tm = cd::core::compat::local_time(now);
    // Year > 1900 means localtime succeeded (tm_year is years-since-1900).
    EXPECT_GE(tm.tm_year, 100);  // 2000 + onwards
}

// --- Handle (S2.1.a) -----------------------------------------------------
namespace test_tags
{
struct Texture
{
};

struct Buffer
{
};
}  // namespace test_tags

using TextureHandle = cd::core::Handle<test_tags::Texture>;
using BufferHandle = cd::core::Handle<test_tags::Buffer>;

TEST(CoreHandle, SizeAndAlignment)
{
    static_assert(sizeof(TextureHandle) == 8);
    static_assert(std::is_trivially_copyable_v<TextureHandle>);
}

TEST(CoreHandle, DefaultIsNull)
{
    TextureHandle h;
    EXPECT_TRUE(h.is_null());
    EXPECT_FALSE(h.is_valid());
    EXPECT_FALSE(static_cast<bool>(h));
    EXPECT_EQ(h, TextureHandle::null());
    EXPECT_EQ(h.value(), 0u);
}

TEST(CoreHandle, PackUnpack)
{
    TextureHandle h { 0xABCDu, 0x42u, 0x07u };
    EXPECT_EQ(h.index(), 0xABCDu);
    EXPECT_EQ(h.generation(), 0x42u);
    EXPECT_EQ(h.type_id(), 0x07u);
    EXPECT_TRUE(h.is_valid());
}

TEST(CoreHandle, MaxIndexAndGeneration)
{
    EXPECT_EQ(TextureHandle::kMaxIndex, 0xFFFFFFFFu);
    EXPECT_EQ(TextureHandle::kMaxGeneration, 0xFFFFu);
    TextureHandle h { TextureHandle::kMaxIndex, TextureHandle::kMaxGeneration, 0xFFFFu };
    EXPECT_EQ(h.index(), TextureHandle::kMaxIndex);
    EXPECT_EQ(h.generation(), TextureHandle::kMaxGeneration);
    EXPECT_EQ(h.type_id(), 0xFFFFu);
}

TEST(CoreHandle, EqualityAndOrder)
{
    TextureHandle a { 1, 1 };
    TextureHandle b { 1, 1 };
    TextureHandle c { 1, 2 };
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_LT(a, c);  // generation higher → packed value higher
}

TEST(CoreHandle, PhantomTagsAreDistinctTypes)
{
    // This is a compile-time guarantee; the static_assert below confirms it.
    static_assert(!std::is_same_v<TextureHandle, BufferHandle>);
    // And assignment across tags would not compile.
}

TEST(CoreHandle, HashIsStableAndDistinct)
{
    std::hash<TextureHandle> hasher;
    EXPECT_EQ(hasher(TextureHandle { 1, 1 }), hasher(TextureHandle { 1, 1 }));
    EXPECT_NE(hasher(TextureHandle { 1, 1 }), hasher(TextureHandle { 1, 2 }));
    EXPECT_NE(hasher(TextureHandle::null()), hasher(TextureHandle { 1, 1 }));
}

// --- HandleStore (S2.1.a) ------------------------------------------------
struct MockResource
{
    int id = 0;
    std::string name {};
    MockResource() = default;

    MockResource(int i, std::string n)
        : id { i }
        , name { std::move(n) }
    {
    }
};

TEST(CoreHandleStore, EmptyStore)
{
    cd::core::HandleStore<MockResource, test_tags::Texture> store;
    EXPECT_EQ(store.size(), 0u);
    EXPECT_TRUE(store.empty());
    EXPECT_EQ(store.capacity(), 0u);
}

TEST(CoreHandleStore, InsertAndGet)
{
    cd::core::HandleStore<MockResource, test_tags::Texture> store;
    auto r = store.insert(MockResource { 42, "albedo" });
    ASSERT_TRUE(r.has_value());
    auto h = *r;
    EXPECT_TRUE(h.is_valid());
    EXPECT_EQ(store.size(), 1u);

    auto* p = store.get(h);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->id, 42);
    EXPECT_EQ(p->name, "albedo");
    EXPECT_TRUE(store.contains(h));
}

TEST(CoreHandleStore, EraseInvalidatesHandle)
{
    cd::core::HandleStore<MockResource, test_tags::Texture> store;
    auto h = *store.insert(MockResource { 1, "a" });
    EXPECT_TRUE(store.erase(h));
    EXPECT_FALSE(store.contains(h));
    EXPECT_EQ(store.get(h), nullptr);
    EXPECT_EQ(store.size(), 0u);

    // Double-erase is a no-op (defensive).
    EXPECT_FALSE(store.erase(h));
}

TEST(CoreHandleStore, GenerationCounterDefeatsStaleHandle)
{
    cd::core::HandleStore<MockResource, test_tags::Texture> store;
    auto h1 = *store.insert(MockResource { 1, "a" });
    store.erase(h1);

    // Reuse the slot; new handle must NOT alias the old one.
    auto h2 = *store.insert(MockResource { 2, "b" });
    EXPECT_EQ(h1.index(), h2.index());  // same slot reused
    EXPECT_NE(h1.generation(), h2.generation());
    EXPECT_NE(h1, h2);
    EXPECT_FALSE(store.contains(h1));  // stale
    EXPECT_TRUE(store.contains(h2));
    EXPECT_EQ(store.get(h2)->id, 2);
}

TEST(CoreHandleStore, MultipleInsertsAllocateFreshIndices)
{
    cd::core::HandleStore<MockResource, test_tags::Texture> store;
    auto h1 = *store.insert(MockResource { 1, "a" });
    auto h2 = *store.insert(MockResource { 2, "b" });
    auto h3 = *store.insert(MockResource { 3, "c" });
    EXPECT_NE(h1, h2);
    EXPECT_NE(h2, h3);
    EXPECT_EQ(store.size(), 3u);
    EXPECT_EQ(store.capacity(), 3u);
}

TEST(CoreHandleStore, EraseAndReinsertReusesSlot)
{
    cd::core::HandleStore<MockResource, test_tags::Texture> store;
    auto h1 = *store.insert(MockResource { 1, "a" });
    auto h2 = *store.insert(MockResource { 2, "b" });
    store.erase(h1);
    auto h3 = *store.insert(MockResource { 3, "c" });
    EXPECT_EQ(h1.index(), h3.index());  // slot reused
    EXPECT_EQ(store.size(), 2u);
    EXPECT_EQ(store.capacity(), 2u);    // no new slot
    EXPECT_TRUE(store.contains(h2));
    EXPECT_TRUE(store.contains(h3));
}

TEST(CoreHandleStore, TypeIdStampedOnHandles)
{
    cd::core::HandleStore<MockResource, test_tags::Texture> store;
    store.set_type_id(0xBEEFu);
    auto h = *store.insert(MockResource { 1, "a" });
    EXPECT_EQ(h.type_id(), 0xBEEFu);
}

TEST(CoreHandleStore, ForEachVisitsLiveOnly)
{
    cd::core::HandleStore<MockResource, test_tags::Texture> store;
    auto h1 = *store.insert(MockResource { 1, "a" });
    auto h2 = *store.insert(MockResource { 2, "b" });
    auto h3 = *store.insert(MockResource { 3, "c" });
    store.erase(h2);  // hole in the middle

    std::vector<int> seen;
    store.for_each(
        [&](TextureHandle, const MockResource& r)
        {
            seen.push_back(r.id);
        }
    );
    std::sort(seen.begin(), seen.end());
    EXPECT_EQ(seen.size(), 2u);
    EXPECT_EQ(seen[0], 1);
    EXPECT_EQ(seen[1], 3);

    // h1, h3 still live; h2 is gone.
    EXPECT_TRUE(store.contains(h1));
    EXPECT_FALSE(store.contains(h2));
    EXPECT_TRUE(store.contains(h3));
}

TEST(CoreHandleStore, StaleHandleAcrossErase)
{
    cd::core::HandleStore<MockResource, test_tags::Texture> store;
    auto h1 = *store.insert(MockResource { 1, "first" });
    auto stale_copy = h1;  // imagine handed out to caller, then forgotten
    store.erase(h1);
    // Caller comes back later expecting to read; must not see new content.
    for (int i = 0; i < 5; ++i)
    {
        (void)store.insert(MockResource { 100 + i, "rotated" });
    }
    EXPECT_FALSE(store.contains(stale_copy));
    EXPECT_EQ(store.get(stale_copy), nullptr);
}

// --- SourceLocation (S2.1.a) ---------------------------------------------
TEST(CoreSourceLocation, CurrentCapturesCallsite)
{
    auto loc = cd::core::here();
    std::string_view fname { cd::core::file_name_only(loc) };
    EXPECT_EQ(fname, "test_core.cpp");
    EXPECT_GT(loc.line(), 0u);
}

TEST(CoreSourceLocation, CompactLocationConstructible)
{
    cd::core::CompactLocation here;
    EXPECT_EQ(here.file, "test_core.cpp");
    EXPECT_GT(here.line, 0u);
}

// --- CVarRegistry (S2.4) --------------------------------------------------
TEST(CoreCVar, SetGetTyped)
{
    cd::core::CVarRegistry r;
    r.set("render.vsync", true);
    r.set("audio.master_volume", static_cast<std::int64_t>(75));
    r.set("camera.fov", 90.0);
    r.set("game.title", std::string { "CHROMODYNAMIC" });

    EXPECT_EQ(r.get_as<bool>("render.vsync"), true);
    EXPECT_EQ(r.get_as<std::int64_t>("audio.master_volume"), 75);
    EXPECT_DOUBLE_EQ(*r.get_as<double>("camera.fov"), 90.0);
    EXPECT_EQ(*r.get_as<std::string>("game.title"), "CHROMODYNAMIC");
    EXPECT_EQ(r.size(), 4u);
}

TEST(CoreCVar, MissingReturnsNullopt)
{
    cd::core::CVarRegistry r;
    EXPECT_FALSE(r.get("never.set").has_value());
    EXPECT_FALSE(r.get_as<std::int64_t>("nothing").has_value());
    EXPECT_FALSE(r.contains("nothing"));
}

TEST(CoreCVar, SubscribeFires)
{
    cd::core::CVarRegistry r;
    int fires = 0;
    cd::core::CVarValue last_value;
    auto id = r.subscribe(
        "speed",
        [&](std::string_view, const cd::core::CVarValue& v)
        {
            ++fires;
            last_value = v;
        }
    );
    r.set("speed", static_cast<std::int64_t>(10));
    r.set("speed", static_cast<std::int64_t>(20));
    EXPECT_EQ(fires, 2);
    EXPECT_EQ(std::get<std::int64_t>(last_value), 20);
    r.unsubscribe("speed", id);
    r.set("speed", static_cast<std::int64_t>(30));
    EXPECT_EQ(fires, 2);
}

TEST(CoreCVar, EraseRemovesEntry)
{
    cd::core::CVarRegistry r;
    r.set("temp.cache_size_mb", static_cast<std::int64_t>(256));
    EXPECT_TRUE(r.contains("temp.cache_size_mb"));
    r.erase("temp.cache_size_mb");
    EXPECT_FALSE(r.contains("temp.cache_size_mb"));
}

}  // namespace
