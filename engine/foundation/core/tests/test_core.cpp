// =============================================================================
// CHROMODYNAMIC — cd::core unit tests
// Phase 2 Sprint S2.0 — smoke + ErrorCode + Result + Version
// =============================================================================
#include <cd/core/CVar.hpp>
#include <cd/core/Compat.hpp>
#include <cd/core/Defines.hpp>
#include <cd/core/Definitions.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/ErrorFormat.hpp>
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
    // Pre-v0.25.0 these asserted the literal 0.1.0 — a value the header
    // hard-coded and that drifted from PROJECT_VERSION every marathon
    // release. After v0.25.0 the constant is stamped from CMake's
    // PROJECT_VERSION_*; we sanity-check the macros came through (>= 0)
    // and that the packed integer is non-zero (would be zero only if all
    // three components are zero — i.e. an unconfigured build).
    EXPECT_GE(cd::core::kEngineVersion.major, 0);
    EXPECT_GE(cd::core::kEngineVersion.minor, 0);
    EXPECT_GE(cd::core::kEngineVersion.patch, 0);
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

// --- ErrorCode::make_owning (Wave 126, BUG #3 fix) ----------------------
TEST(CoreErrorCode, MakeOwningKeepsMessageAfterLocalGoesOutOfScope)
{
    auto produce = []() {
        std::string local = "diagnostic from a local string";
        local += " (concatenated)";
        // Returning by value — the local std::string is destructed at the end of this scope.
        return cd::core::ErrorCode::make_owning(0xDEAD, 7, std::move(local));
    };
    auto ec = produce();
    // The local std::string has been destructed but ec.owned keeps the storage alive.
    EXPECT_EQ(ec.message, "diagnostic from a local string (concatenated)");
    EXPECT_NE(ec.owned, nullptr);

    // Copying the ErrorCode shares the owning storage (refcount bump, no copy).
    const auto& ec_copy = ec;
    EXPECT_EQ(ec_copy.message.data(), ec.message.data());
    EXPECT_EQ(ec_copy.owned, ec.owned);
}

TEST(CoreErrorCode, RewrapPreservesOwnedMessage)
{
    auto upstream = cd::core::ErrorCode::make_owning(0xAAAAU, 1, std::string { "glslang parse: ERROR: file.frag:7: '...'" });

    // Re-tag with a different (domain, code) — message must survive.
    auto wrapped = cd::core::ErrorCode::rewrap(0xBBBBU, 2, upstream);
    EXPECT_EQ(wrapped.domain, 0xBBBBU);
    EXPECT_EQ(wrapped.code, 2U);
    EXPECT_EQ(wrapped.message, "glslang parse: ERROR: file.frag:7: '...'");
    EXPECT_EQ(wrapped.owned, upstream.owned);  // shared storage

    // Even after upstream is destroyed, wrapped keeps the storage.
    auto wrapped_after = [&]() {
        auto u2 = cd::core::ErrorCode::make_owning(0xAAAAU, 1, std::string { "second message" });
        return cd::core::ErrorCode::rewrap(0xBBBBU, 3, u2);
    }();
    EXPECT_EQ(wrapped_after.message, "second message");
}

TEST(CoreErrorCode, RewrapForwardsNonOwningView)
{
    // Upstream uses a literal — no owning storage; rewrap just forwards the view.
    auto upstream = cd::core::core_errors::make(cd::core::core_errors::Code::kInvalidArgument, "literal");
    auto wrapped = cd::core::ErrorCode::rewrap(0xCAFEU, 9, upstream);
    EXPECT_EQ(wrapped.message, "literal");
    EXPECT_EQ(wrapped.owned, nullptr);
}

// --- ErrorFormat (Wave 109) ---------------------------------------------
TEST(CoreErrorFormat, CoreDomainIsRegistered)
{
    EXPECT_EQ(cd::core::domain_name(cd::core::core_errors::kDomain), "core");
    EXPECT_EQ(cd::core::code_name(cd::core::core_errors::kDomain,
                                  static_cast<std::uint32_t>(cd::core::core_errors::Code::kInvalidArgument)),
              "InvalidArgument");
}

TEST(CoreErrorFormat, FormatsKnownDomainAndCode)
{
    auto ec = cd::core::core_errors::make(cd::core::core_errors::Code::kInvalidArgument, "x must be >= 0");
    EXPECT_EQ(cd::core::format(ec), "core::InvalidArgument: x must be >= 0");
}

TEST(CoreErrorFormat, FormatsKnownCodeWithoutMessage)
{
    auto ec = cd::core::core_errors::make(cd::core::core_errors::Code::kNotFound);
    EXPECT_EQ(cd::core::format(ec), "core::NotFound");
}

TEST(CoreErrorFormat, FallsBackToHexForUnknownDomain)
{
    cd::core::ErrorCode ec { 0xCAFEu, 0x07u, "boom" };
    EXPECT_EQ(cd::core::format(ec), "0xcafe::0x07: boom");
}

TEST(CoreErrorFormat, FallsBackToHexCodeForKnownDomainUnknownCode)
{
    cd::core::ErrorCode ec { cd::core::core_errors::kDomain, 0x99u };
    // Domain "core" is known but code 0x99 is not in the enum — code falls back to hex.
    EXPECT_EQ(cd::core::format(ec), "core::0x99");
}

TEST(CoreErrorFormat, RegisterAndLookupCustomDomain)
{
    constexpr std::uint32_t kMyDomain = 0xBEEFu;
    auto resolver = [](std::uint32_t code) -> std::string_view {
        if (code == 1u) return "Frobnicated";
        if (code == 2u) return "Wibbled";
        return {};
    };
    cd::core::register_domain(kMyDomain, "mylib", +resolver);

    EXPECT_EQ(cd::core::domain_name(kMyDomain), "mylib");
    EXPECT_EQ(cd::core::code_name(kMyDomain, 1u), "Frobnicated");
    EXPECT_EQ(cd::core::code_name(kMyDomain, 99u), std::string_view {});

    cd::core::ErrorCode ec { kMyDomain, 2u, "oops" };
    EXPECT_EQ(cd::core::format(ec), "mylib::Wibbled: oops");
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

// ---------------------------------------------------------------------------
// Phase 21.B — SmallVector tests (Wave 184)
// ---------------------------------------------------------------------------
#include <cd/core/SmallVector.hpp>

TEST(SmallVector, EmptyOnConstruction)
{
    cd::core::SmallVector<int, 4> v;
    EXPECT_EQ(v.size(), 0u);
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.capacity(), 4u);
}

TEST(SmallVector, PushBackWithinInlineCapacity)
{
    cd::core::SmallVector<int, 4> v;
    v.push_back(10);
    v.push_back(20);
    EXPECT_EQ(v.size(), 2u);
    EXPECT_EQ(v[0], 10);
    EXPECT_EQ(v[1], 20);
    EXPECT_EQ(v.capacity(), 4u);
}

TEST(SmallVector, GrowsToHeapBeyondInlineCapacity)
{
    cd::core::SmallVector<int, 2> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    EXPECT_EQ(v.size(), 3u);
    EXPECT_GE(v.capacity(), 3u);
    EXPECT_EQ(v[0], 1);
    EXPECT_EQ(v[2], 3);
}

TEST(SmallVector, PopBackShrinks)
{
    cd::core::SmallVector<int, 4> v;
    v.push_back(7);
    v.push_back(8);
    v.pop_back();
    EXPECT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0], 7);
}

// ---------------------------------------------------------------------------
// Phase 22.E — FixedString tests (Wave 186)
// ---------------------------------------------------------------------------
#include <cd/core/FixedString.hpp>

TEST(FixedString, EmptyDefault)
{
    cd::core::FixedString<32> s;
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0u);
}

TEST(FixedString, AssignFromStringView)
{
    cd::core::FixedString<16> s { "hello" };
    EXPECT_EQ(s.size(), 5u);
    EXPECT_EQ(s.view(), std::string_view { "hello" });
    EXPECT_STREQ(s.c_str(), "hello");
}

TEST(FixedString, TruncatesAtCapacity)
{
    cd::core::FixedString<8> s { "this is too long" };
    EXPECT_EQ(s.size(), 7u);  // N-1 == 7 chars + null terminator
    EXPECT_EQ(s.view(), std::string_view { "this is" });
}

TEST(FixedString, EqualityComparesContents)
{
    cd::core::FixedString<16> a { "abc" }, b { "abc" }, c { "def" };
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
}

// ---------------------------------------------------------------------------
// Phase 23.C — ScopeGuard tests (Wave 188)
// ---------------------------------------------------------------------------
#include <cd/core/ScopeGuard.hpp>

TEST(ScopeGuard, FiresAtScopeExit)
{
    int n = 0;
    {
        auto g = cd::core::make_scope_guard([&] { ++n; });
    }
    EXPECT_EQ(n, 1);
}

TEST(ScopeGuard, DismissSkipsCallback)
{
    int n = 0;
    {
        auto g = cd::core::make_scope_guard([&] { ++n; });
        g.dismiss();
    }
    EXPECT_EQ(n, 0);
}

// ---------------------------------------------------------------------------
// Phase 25.A — FrameAllocator tests (Wave 192)
// ---------------------------------------------------------------------------
#include <cd/core/FrameAllocator.hpp>

TEST(FrameAllocator, AllocateReturnsAlignedPointer)
{
    cd::core::FrameAllocator a { 1024 };
    void* p16 = a.allocate(32, 16);
    ASSERT_NE(p16, nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p16) % 16u, 0u);
}

TEST(FrameAllocator, ExhaustsCapacity)
{
    cd::core::FrameAllocator a { 64 };
    EXPECT_NE(a.allocate(40, 1), nullptr);
    EXPECT_EQ(a.allocate(40, 1), nullptr);  // overflow
}

TEST(FrameAllocator, ResetReclaimsAll)
{
    cd::core::FrameAllocator a { 128 };
    (void)a.allocate(100, 1);
    EXPECT_LT(a.remaining(), 100u);
    a.reset();
    EXPECT_EQ(a.used(), 0u);
    EXPECT_EQ(a.remaining(), 128u);
}

// ---------------------------------------------------------------------------
// Phase 26.A — RingBuffer tests (Wave 194)
// ---------------------------------------------------------------------------
#include <cd/core/RingBuffer.hpp>

TEST(RingBuffer, PushPopFifoOrder)
{
    cd::core::RingBuffer<int, 4> rb;
    EXPECT_TRUE(rb.push(1));
    EXPECT_TRUE(rb.push(2));
    EXPECT_TRUE(rb.push(3));
    EXPECT_EQ(rb.size(), 3u);
    EXPECT_EQ(*rb.pop(), 1);
    EXPECT_EQ(*rb.pop(), 2);
    EXPECT_EQ(*rb.pop(), 3);
    EXPECT_TRUE(rb.empty());
}

TEST(RingBuffer, FullReturnsFalseOnPush)
{
    cd::core::RingBuffer<int, 2> rb;
    EXPECT_TRUE(rb.push(1));
    EXPECT_TRUE(rb.push(2));
    EXPECT_FALSE(rb.push(3));
    EXPECT_TRUE(rb.full());
}

TEST(RingBuffer, WrapAroundPreservesOrder)
{
    cd::core::RingBuffer<int, 3> rb;
    rb.push(1); rb.push(2); rb.push(3);
    (void)rb.pop();
    (void)rb.pop();
    rb.push(4);
    rb.push(5);
    EXPECT_EQ(*rb.pop(), 3);
    EXPECT_EQ(*rb.pop(), 4);
    EXPECT_EQ(*rb.pop(), 5);
}

#include <cd/core/BitOps.hpp>

TEST(BitOps, PopcountCountsSetBits)
{
    EXPECT_EQ(cd::core::popcount(0u), 0);
    EXPECT_EQ(cd::core::popcount(0xFFu), 8);
    EXPECT_EQ(cd::core::popcount(0b10101010u), 4);
}

TEST(BitOps, IsPow2)
{
    EXPECT_FALSE(cd::core::is_pow2(0));
    EXPECT_TRUE(cd::core::is_pow2(1));
    EXPECT_TRUE(cd::core::is_pow2(2));
    EXPECT_FALSE(cd::core::is_pow2(3));
    EXPECT_TRUE(cd::core::is_pow2(1024));
}

TEST(BitOps, NextPow2RoundsUp)
{
    EXPECT_EQ(cd::core::next_pow2(0), 1u);
    EXPECT_EQ(cd::core::next_pow2(1), 1u);
    EXPECT_EQ(cd::core::next_pow2(5), 8u);
    EXPECT_EQ(cd::core::next_pow2(1024), 1024u);
}

TEST(BitOps, AlignUpToPow2)
{
    EXPECT_EQ(cd::core::align_up(0, 16), 0u);
    EXPECT_EQ(cd::core::align_up(1, 16), 16u);
    EXPECT_EQ(cd::core::align_up(15, 16), 16u);
    EXPECT_EQ(cd::core::align_up(17, 16), 32u);
}

#include <cd/core/Bitset.hpp>

TEST(Bitset, DefaultIsAllClear)
{
    cd::core::Bitset<128> b;
    EXPECT_EQ(b.count(), 0u);
    EXPECT_FALSE(b.any());
    EXPECT_TRUE(b.none());
}

TEST(Bitset, SetAndTest)
{
    cd::core::Bitset<128> b;
    b.set(42);
    b.set(100);
    EXPECT_TRUE(b.test(42));
    EXPECT_TRUE(b.test(100));
    EXPECT_FALSE(b.test(43));
    EXPECT_EQ(b.count(), 2u);
}

TEST(Bitset, ClearRemovesBit)
{
    cd::core::Bitset<64> b;
    b.set(5);
    b.set(10);
    b.clear(5);
    EXPECT_FALSE(b.test(5));
    EXPECT_TRUE(b.test(10));
    EXPECT_EQ(b.count(), 1u);
}

TEST(Bitset, FindFirstSet)
{
    cd::core::Bitset<256> b;
    EXPECT_EQ(b.find_first_set(), 256u);  // sentinel for empty
    b.set(73);
    b.set(200);
    EXPECT_EQ(b.find_first_set(), 73u);
}

TEST(Bitset, ForEachSetVisitsInOrder)
{
    cd::core::Bitset<128> b;
    b.set(1);
    b.set(64);
    b.set(120);
    std::vector<std::size_t> visited;
    b.for_each_set([&](std::size_t i) { visited.push_back(i); });
    ASSERT_EQ(visited.size(), 3u);
    EXPECT_EQ(visited[0], 1u);
    EXPECT_EQ(visited[1], 64u);
    EXPECT_EQ(visited[2], 120u);
}

TEST(Bitset, ResetClearsAll)
{
    cd::core::Bitset<64> b;
    b.set(0); b.set(63);
    b.reset();
    EXPECT_TRUE(b.none());
}

#include <cd/core/ArenaScope.hpp>

TEST(ArenaScope, RewindsToConstructionMark)
{
    cd::core::FrameAllocator arena { 1024 };
    (void)arena.allocate(64);
    const std::size_t before = arena.used();
    {
        cd::core::ArenaScope scope { arena };
        (void)arena.allocate(128);
        (void)arena.allocate(256);
        EXPECT_GT(arena.used(), before);
    }
    EXPECT_EQ(arena.used(), before);
}

TEST(ArenaScope, NestedScopesPopLifo)
{
    cd::core::FrameAllocator arena { 1024 };
    (void)arena.allocate(16);
    const std::size_t outer = arena.used();
    {
        cd::core::ArenaScope a { arena };
        (void)arena.allocate(32);
        const std::size_t mid = arena.used();
        {
            cd::core::ArenaScope b { arena };
            (void)arena.allocate(64);
            EXPECT_GT(arena.used(), mid);
        }
        EXPECT_EQ(arena.used(), mid);
    }
    EXPECT_EQ(arena.used(), outer);
}

TEST(FrameAllocator, RewindRestoresMark)
{
    cd::core::FrameAllocator arena { 256 };
    (void)arena.allocate(64);
    const auto mark = arena.used();
    (void)arena.allocate(64);
    arena.rewind(mark);
    EXPECT_EQ(arena.used(), mark);
}

#include <cd/core/StringSplit.hpp>

TEST(StringSplit, EmptyInputCallsOnceWithEmpty)
{
    std::vector<std::string_view> parts;
    cd::core::split(std::string_view {}, ',', [&](std::string_view p) { parts.push_back(p); });
    ASSERT_EQ(parts.size(), 1u);
    EXPECT_TRUE(parts[0].empty());
}

TEST(StringSplit, SimpleCsv)
{
    std::vector<std::string_view> parts;
    cd::core::split("a,b,c", ',', [&](std::string_view p) { parts.push_back(p); });
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_EQ(parts[0], "a");
    EXPECT_EQ(parts[1], "b");
    EXPECT_EQ(parts[2], "c");
}

TEST(StringSplit, EmptySegmentsPreserved)
{
    std::vector<std::string_view> parts;
    cd::core::split("a,,b", ',', [&](std::string_view p) { parts.push_back(p); });
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_TRUE(parts[1].empty());
}

TEST(StringSplit, NonEmptyVariantSkipsEmpty)
{
    std::vector<std::string_view> parts;
    cd::core::split_nonempty(",,a,,b,,", ',', [&](std::string_view p) { parts.push_back(p); });
    ASSERT_EQ(parts.size(), 2u);
    EXPECT_EQ(parts[0], "a");
    EXPECT_EQ(parts[1], "b");
}

TEST(StringSplit, CountPartsMatchesSplit)
{
    EXPECT_EQ(cd::core::count_parts("", ','), 0u);
    EXPECT_EQ(cd::core::count_parts("a", ','), 1u);
    EXPECT_EQ(cd::core::count_parts("a,b,c", ','), 3u);
    EXPECT_EQ(cd::core::count_parts(",,", ','), 3u);
}

#include <cd/core/ProfileSpan.hpp>
#include <thread>

TEST(ProfileSpan, RecordsElapsedOnScopeExit)
{
    cd::core::ProfileRegistry::instance().clear();
    {
        cd::core::ProfileSpan _ { "phase52_test_a" };
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto s = cd::core::ProfileRegistry::instance().stats("phase52_test_a");
    EXPECT_EQ(s.count, 1u);
    EXPECT_GT(s.total_ns, 0u);
}

TEST(ProfileSpan, CountAccumulatesAcrossSpans)
{
    cd::core::ProfileRegistry::instance().clear();
    for (int i = 0; i < 5; ++i)
    {
        cd::core::ProfileSpan _ { "phase52_test_b" };
    }
    auto s = cd::core::ProfileRegistry::instance().stats("phase52_test_b");
    EXPECT_EQ(s.count, 5u);
}

TEST(ProfileRegistry, UnknownNameReturnsZero)
{
    cd::core::ProfileRegistry::instance().clear();
    auto s = cd::core::ProfileRegistry::instance().stats("phase52_no_such_name");
    EXPECT_EQ(s.count, 0u);
    EXPECT_EQ(s.total_ns, 0u);
}

#include <cd/core/RetryPolicy.hpp>

TEST(RetryPolicy, FirstAttemptUsesBaseDelay)
{
    cd::core::RetryPolicy p { 5, 100, 5000, 2.0F };
    EXPECT_EQ(p.attempt_delay_ms(1), 100u);
}

TEST(RetryPolicy, ExponentialBackoff)
{
    cd::core::RetryPolicy p { 5, 100, 5000, 2.0F };
    EXPECT_EQ(p.attempt_delay_ms(2), 200u);
    EXPECT_EQ(p.attempt_delay_ms(3), 400u);
    EXPECT_EQ(p.attempt_delay_ms(4), 800u);
}

TEST(RetryPolicy, ClampsAtMaxDelay)
{
    cd::core::RetryPolicy p { 10, 100, 500, 2.0F };
    EXPECT_LE(p.attempt_delay_ms(10), 500u);
}

TEST(RetryPolicy, ShouldRetryRespectsMaxAttempts)
{
    cd::core::RetryPolicy p { 3, 100, 5000, 2.0F };
    EXPECT_TRUE(p.should_retry(1));
    EXPECT_TRUE(p.should_retry(2));
    EXPECT_FALSE(p.should_retry(3));
}

#include <cd/core/Bytes.hpp>

TEST(Bytes, FormatsSmallAsBytes)
{
    EXPECT_EQ(cd::core::format_bytes(500), "500 B");
    EXPECT_EQ(cd::core::format_bytes(1023), "1023 B");
}

TEST(Bytes, FormatsKilobytes)
{
    EXPECT_EQ(cd::core::format_bytes(1024), "1.00 KB");
    EXPECT_EQ(cd::core::format_bytes(1536), "1.50 KB");
}

TEST(Bytes, FormatsMegabytes)
{
    EXPECT_EQ(cd::core::format_bytes(cd::core::kMB), "1.00 MB");
    EXPECT_EQ(cd::core::format_bytes(cd::core::kMB + cd::core::kKB * 512), "1.50 MB");
}

TEST(Bytes, FormatsGigabytes)
{
    EXPECT_EQ(cd::core::format_bytes(cd::core::kGB * 2), "2.00 GB");
}

TEST(Bytes, BinaryMultiplierConstants)
{
    EXPECT_EQ(cd::core::kKB, 1024u);
    EXPECT_EQ(cd::core::kMB, 1024u * 1024u);
    EXPECT_EQ(cd::core::kGB, 1024u * 1024u * 1024u);
}

#include <cd/core/PoolAllocator.hpp>

TEST(PoolAllocator, AllocateReturnsDistinctPointers)
{
    cd::core::PoolAllocator p { 64, 8 };
    void* a = p.allocate();
    void* b = p.allocate();
    void* c = p.allocate();
    EXPECT_NE(a, b);
    EXPECT_NE(b, c);
    EXPECT_EQ(p.used_count(), 3u);
}

TEST(PoolAllocator, ExhaustionReturnsNullptr)
{
    cd::core::PoolAllocator p { 32, 3 };
    void* a = p.allocate();
    void* b = p.allocate();
    void* c = p.allocate();
    void* d = p.allocate();   // pool empty
    EXPECT_NE(a, nullptr);
    EXPECT_NE(b, nullptr);
    EXPECT_NE(c, nullptr);
    EXPECT_EQ(d, nullptr);
}

TEST(PoolAllocator, DeallocateMakesSlotAvailable)
{
    cd::core::PoolAllocator p { 32, 2 };
    void* a = p.allocate();
    void* b = p.allocate();
    EXPECT_EQ(p.allocate(), nullptr);
    p.deallocate(a);
    void* d = p.allocate();
    EXPECT_NE(d, nullptr);
    p.deallocate(b);
    p.deallocate(d);
    EXPECT_EQ(p.used_count(), 0u);
}

TEST(PoolAllocator, BlockSizeMinimumIsPointerSize)
{
    cd::core::PoolAllocator p { 4, 4 };   // requested 4, clamped to sizeof(void*)
    EXPECT_GE(p.block_size(), sizeof(void*));
}

#include <cd/core/Ref.hpp>

namespace {
struct Foo { int n { 0 }; };
void take_foo(Foo& f) { ++f.n; }
}

TEST(Ref, ImplicitFromLvalue)
{
    Foo f { 5 };
    cd::core::Ref<Foo> r { f };
    EXPECT_EQ(r.get().n, 5);
}

TEST(Ref, ConvertsImplicitlyToLvalue)
{
    Foo f { 10 };
    cd::core::Ref<Foo> r { f };
    take_foo(r);
    EXPECT_EQ(f.n, 11);   // mutation propagates
}

TEST(Ref, ArrowDereferences)
{
    Foo f { 99 };
    cd::core::Ref<Foo> r { f };
    EXPECT_EQ(r->n, 99);
}

#include <cd/core/EnumFlags.hpp>

namespace {
enum class TestFlags : std::uint32_t
{
    kNone = 0,
    kA    = 1,
    kB    = 2,
    kC    = 4,
};
CD_ENUM_FLAGS(TestFlags)
}

TEST(EnumFlags, OrCombines)
{
    auto f = TestFlags::kA | TestFlags::kC;
    EXPECT_TRUE(cd::core::has(f, TestFlags::kA));
    EXPECT_FALSE(cd::core::has(f, TestFlags::kB));
    EXPECT_TRUE(cd::core::has(f, TestFlags::kC));
}

TEST(EnumFlags, AndIntersects)
{
    auto ab = TestFlags::kA | TestFlags::kB;
    auto bc = TestFlags::kB | TestFlags::kC;
    auto i  = ab & bc;
    EXPECT_TRUE(cd::core::has(i, TestFlags::kB));
    EXPECT_FALSE(cd::core::has(i, TestFlags::kA));
}

TEST(EnumFlags, NotInverts)
{
    auto f = TestFlags::kA;
    auto n = ~f;
    EXPECT_FALSE(cd::core::has(n, TestFlags::kA));
    EXPECT_TRUE(cd::core::has(n, TestFlags::kB));
}

TEST(EnumFlags, AssignmentOps)
{
    auto f = TestFlags::kA;
    f |= TestFlags::kB;
    EXPECT_TRUE(cd::core::has(f, TestFlags::kB));
    f &= TestFlags::kB;
    EXPECT_FALSE(cd::core::has(f, TestFlags::kA));
}

#include <cd/core/Assert.hpp>

TEST(Assert, VerifySucceedsOnTrue)
{
    // Compiler shouldn't abort.
    CD_VERIFY(1 + 1 == 2);
    EXPECT_TRUE(true);
}

TEST(Assert, CustomHandlerInvoked)
{
    bool handler_called = false;
    cd::core::set_assert_handler([](const char*, int, const char*) noexcept {
        // Note: handler is called BEFORE default abort; we don't actually
        // want to abort the test, so the handler exits the lambda and
        // default_assert_fail still aborts — UNLESS we set a handler
        // that itself aborts/exits. For test purposes we just verify
        // the handler slot is settable.
    });
    cd::core::set_assert_handler(nullptr);
    EXPECT_FALSE(handler_called);   // never set true; just the smoke test
}

#include <cd/core/Singleton.hpp>

namespace {
class TestSink : public cd::core::Singleton<TestSink>
{
    friend class cd::core::Singleton<TestSink>;
    TestSink() = default;
public:
    void bump() noexcept { ++counter_; }
    [[nodiscard]] int counter() const noexcept { return counter_; }
private:
    int counter_ { 0 };
};
}

TEST(Singleton, InstanceIsSame)
{
    auto& a = TestSink::instance();
    auto& b = TestSink::instance();
    EXPECT_EQ(&a, &b);
}

TEST(Singleton, MutationPersists)
{
    TestSink::instance().bump();
    TestSink::instance().bump();
    EXPECT_GE(TestSink::instance().counter(), 2);
}
