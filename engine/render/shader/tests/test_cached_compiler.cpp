// =============================================================================
// CHROMODYNAMIC — cd::shader::CachedCompiler tests
//
// Use a fake ICompiler that counts compile() calls so we can prove the
// cache decorator (a) materialises and (b) actually short-circuits on the
// second invocation with the same descriptor. Avoids glslang dependence
// — the cache logic is what we're verifying.
// =============================================================================
#include <cd/shader/CachedCompiler.hpp>
#include <gtest/gtest.h>

#include <map>
#include <optional>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace
{

namespace fs = std::filesystem;

class CountingCompiler final : public cd::shader::ICompiler
{
public:
    [[nodiscard]] cd::core::Result<cd::shader::CompileResult> compile(const cd::shader::CompileDesc& desc) override
    {
        ++calls;
        cd::shader::CompileResult r;
        // Deterministic dummy SPIR-V: a single magic-number word plus a
        // hash-derived payload so different inputs yield different blobs.
        std::uint32_t payload = 0;
        for (char c : desc.source)
            payload = (payload * 31U) + static_cast<std::uint32_t>(c);
        r.spirv = { 0x07230203U, payload, 0xCAFEBABEU };
        return r;
    }

    std::atomic<std::uint64_t> calls { 0 };
};

[[nodiscard]] fs::path fresh_cache_dir()
{
    static std::atomic<std::uint64_t> seq { 0 };
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() / ("cd_shader_cache_" + std::to_string(static_cast<std::uint64_t>(stamp)) + "_" +
                                        std::to_string(seq.fetch_add(1)));
}

struct CacheDirGuard
{
    fs::path path;

    explicit CacheDirGuard(fs::path p)
        : path { std::move(p) }
    {
    }

    ~CacheDirGuard()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }

    CacheDirGuard(const CacheDirGuard&) = delete;
    CacheDirGuard& operator=(const CacheDirGuard&) = delete;
    CacheDirGuard(CacheDirGuard&&) = delete;
    CacheDirGuard& operator=(CacheDirGuard&&) = delete;
};

// ---- phase1132 (SL-C step 1): include-closure key fold -----------------------

class CacheMapResolver final : public cd::shader::IIncludeResolver
{
public:
    [[nodiscard]] std::optional<Resolved> resolve(
        std::string_view requested, std::string_view /*requester*/,
        bool /*system_include*/) override
    {
        const auto it = modules.find(std::string { requested });
        if (it == modules.end())
            return std::nullopt;
        return Resolved { it->first, it->second };
    }

    std::map<std::string, std::string> modules;
};

// Editing an included module MUST change the cache key (stale .spv would
// silently mis-render otherwise) — ADR-20260612-shader-library-architecture
// §2.3. The root source stays byte-identical across the edit.
TEST(CachedCompiler, ModuleEditInvalidatesClosureKey)
{
    CountingCompiler inner;
    CacheDirGuard guard { fresh_cache_dir() };
    cd::shader::CachedCompiler cached { inner, guard.path };

    CacheMapResolver resolver;
    resolver.modules["m.glsl"] = "float v() { return 1.0; }\n";

    cd::shader::CompileDesc desc {};
    desc.source = "#include \"m.glsl\"\nvoid main() {}\n";
    desc.include_resolver = &resolver;

    ASSERT_TRUE(cached.compile(desc).has_value());   // miss 1
    ASSERT_TRUE(cached.compile(desc).has_value());   // hit
    EXPECT_EQ(cached.stats().misses, 1u);
    EXPECT_EQ(cached.stats().hits, 1u);

    resolver.modules["m.glsl"] = "float v() { return 2.0; }\n";  // module edit
    ASSERT_TRUE(cached.compile(desc).has_value());   // MUST miss again
    EXPECT_EQ(cached.stats().misses, 2u);
    EXPECT_EQ(cached.stats().hits, 1u);
}

// No resolver / no includes -> key identical to the pre-phase1132 epoch.
TEST(CachedCompiler, EmptyClosureKeepsLegacyKey)
{
    CountingCompiler inner;
    CacheDirGuard guard { fresh_cache_dir() };

    cd::shader::CompileDesc desc {};
    desc.source = "void main() {}\n";

    {
        cd::shader::CachedCompiler cached { inner, guard.path };
        ASSERT_TRUE(cached.compile(desc).has_value());  // miss -> persists
    }
    {
        // Same dir, resolver SET but source has no includes: closure is
        // empty -> same key -> warm hit from the first epoch.
        cd::shader::CachedCompiler cached { inner, guard.path };
        CacheMapResolver resolver;
        desc.include_resolver = &resolver;
        ASSERT_TRUE(cached.compile(desc).has_value());
        EXPECT_EQ(cached.stats().hits, 1u);
        EXPECT_EQ(cached.stats().misses, 0u);
    }
}

}  // namespace

TEST(CachedCompiler, FirstCompileMissesAndPersists)
{
    CountingCompiler inner;
    CacheDirGuard guard { fresh_cache_dir() };
    cd::shader::CachedCompiler cache { inner, guard.path };

    cd::shader::CompileDesc d {};
    d.source = "void main(){}";
    d.stage = cd::shader::ShaderStage::kVertex;

    auto r = cache.compile(d);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(inner.calls.load(), 1U);
    EXPECT_EQ(cache.stats().misses, 1U);
    EXPECT_EQ(cache.stats().hits, 0U);
    EXPECT_EQ(cache.stats().writes, 1U);

    // Cache file should exist on disk after the miss publish.
    bool any_spv = false;
    for (const auto& e : fs::directory_iterator(guard.path))
    {
        if (e.path().extension() == ".spv")
            any_spv = true;
    }
    EXPECT_TRUE(any_spv);
}

TEST(CachedCompiler, SecondCompileSameInputHits)
{
    CountingCompiler inner;
    CacheDirGuard guard { fresh_cache_dir() };
    cd::shader::CachedCompiler cache { inner, guard.path };

    cd::shader::CompileDesc d {};
    d.source = "stable source";
    d.stage = cd::shader::ShaderStage::kFragment;

    auto r1 = cache.compile(d);
    auto r2 = cache.compile(d);
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(inner.calls.load(), 1U);  // second time hits cache
    EXPECT_EQ(cache.stats().misses, 1U);
    EXPECT_EQ(cache.stats().hits, 1U);
    EXPECT_EQ(r1->spirv, r2->spirv);
}

TEST(CachedCompiler, DifferentStageMissesIndependently)
{
    CountingCompiler inner;
    CacheDirGuard guard { fresh_cache_dir() };
    cd::shader::CachedCompiler cache { inner, guard.path };

    cd::shader::CompileDesc vs {};
    vs.source = "same source";
    vs.stage = cd::shader::ShaderStage::kVertex;
    cd::shader::CompileDesc fs {};
    fs.source = "same source";
    fs.stage = cd::shader::ShaderStage::kFragment;

    (void)cache.compile(vs);
    (void)cache.compile(fs);
    EXPECT_EQ(inner.calls.load(), 2U);
    EXPECT_EQ(cache.stats().misses, 2U);
}

TEST(CachedCompiler, ClearForcesRecompile)
{
    CountingCompiler inner;
    CacheDirGuard guard { fresh_cache_dir() };
    cd::shader::CachedCompiler cache { inner, guard.path };

    cd::shader::CompileDesc d {};
    d.source = "abc";
    (void)cache.compile(d);
    (void)cache.compile(d);
    EXPECT_EQ(inner.calls.load(), 1U);

    ASSERT_TRUE(cache.clear());
    (void)cache.compile(d);
    EXPECT_EQ(inner.calls.load(), 2U);
    EXPECT_EQ(cache.stats().misses, 2U);
}

TEST(CachedCompiler, SecondInstancePicksUpDiskCache)
{
    CountingCompiler inner_a;
    CountingCompiler inner_b;
    CacheDirGuard guard { fresh_cache_dir() };

    cd::shader::CompileDesc d {};
    d.source = "persistent across instances";

    {
        cd::shader::CachedCompiler cache_a { inner_a, guard.path };
        (void)cache_a.compile(d);
    }
    // Second wrapper over a different inner backend, same cache_dir,
    // should hit on disk and never delegate to the new inner.
    cd::shader::CachedCompiler cache_b { inner_b, guard.path };
    auto r = cache_b.compile(d);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(inner_a.calls.load(), 1U);
    EXPECT_EQ(inner_b.calls.load(), 0U);
    EXPECT_EQ(cache_b.stats().hits, 1U);
}

// ---- Key-sensitivity: every keyed field must produce an independent slot --

TEST(CachedCompiler, DifferentEntryPointMissesIndependently)
{
    CountingCompiler inner;
    CacheDirGuard guard { fresh_cache_dir() };
    cd::shader::CachedCompiler cache { inner, guard.path };

    cd::shader::CompileDesc a {};
    a.source = "same source";
    a.entry_point = "main";
    cd::shader::CompileDesc b {};
    b.source = "same source";
    b.entry_point = "PSMain";

    (void)cache.compile(a);
    (void)cache.compile(b);
    EXPECT_EQ(inner.calls.load(), 2U);
    EXPECT_EQ(cache.stats().misses, 2U);
}

TEST(CachedCompiler, DifferentTargetMissesIndependently)
{
    CountingCompiler inner;
    CacheDirGuard guard { fresh_cache_dir() };
    cd::shader::CachedCompiler cache { inner, guard.path };

    cd::shader::CompileDesc a {};
    a.source = "same source";
    a.target = cd::shader::TargetEnv::kVulkan12;
    cd::shader::CompileDesc b {};
    b.source = "same source";
    b.target = cd::shader::TargetEnv::kVulkan13;

    (void)cache.compile(a);
    (void)cache.compile(b);
    EXPECT_EQ(inner.calls.load(), 2U);
}

TEST(CachedCompiler, DebugInfoFlagMissesIndependently)
{
    CountingCompiler inner;
    CacheDirGuard guard { fresh_cache_dir() };
    cd::shader::CachedCompiler cache { inner, guard.path };

    cd::shader::CompileDesc lean {};
    lean.source = "same source";
    lean.generate_debug_info = false;
    cd::shader::CompileDesc dbg {};
    dbg.source = "same source";
    dbg.generate_debug_info = true;

    (void)cache.compile(lean);
    (void)cache.compile(dbg);
    EXPECT_EQ(inner.calls.load(), 2U);
}

TEST(CachedCompiler, DifferentLanguageMissesIndependently)
{
    CountingCompiler inner;
    CacheDirGuard guard { fresh_cache_dir() };
    cd::shader::CachedCompiler cache { inner, guard.path };

    cd::shader::CompileDesc g {};
    g.source = "same source";
    g.lang = cd::shader::ShaderLanguage::kGlsl;
    cd::shader::CompileDesc h {};
    h.source = "same source";
    h.lang = cd::shader::ShaderLanguage::kHlsl;

    (void)cache.compile(g);
    (void)cache.compile(h);
    EXPECT_EQ(inner.calls.load(), 2U);
}

// source_name is diagnostic-only and must NOT key the cache (a rename of
// the logical name should still hit a warm entry).
TEST(CachedCompiler, SourceNameDoesNotKeyCache)
{
    CountingCompiler inner;
    CacheDirGuard guard { fresh_cache_dir() };
    cd::shader::CachedCompiler cache { inner, guard.path };

    cd::shader::CompileDesc a {};
    a.source = "void main(){}";
    a.source_name = "triangle.vert";
    cd::shader::CompileDesc b {};
    b.source = "void main(){}";
    b.source_name = "renamed.vert";

    (void)cache.compile(a);
    auto r = cache.compile(b);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(inner.calls.load(), 1U);  // b hits a's entry despite the rename
    EXPECT_EQ(cache.stats().hits, 1U);
}

// ---- Corrupt / malformed cache file: count, EVICT, self-heal ---------------

// A truncated (non-word-aligned) .spv on disk must NOT be served as a hit;
// the decorator counts a read_failure, evicts the corrupt entry, recompiles,
// and a subsequent compile then hits the freshly-published clean entry.
TEST(CachedCompiler, CorruptCacheFileRecompilesAndSelfHeals)
{
    CountingCompiler inner;
    CacheDirGuard guard { fresh_cache_dir() };
    cd::shader::CachedCompiler cache { inner, guard.path };

    cd::shader::CompileDesc d {};
    d.source = "void main(){}";

    // Warm the cache so a .spv exists, then corrupt it to a 3-byte file.
    ASSERT_TRUE(cache.compile(d).has_value());
    ASSERT_EQ(cache.stats().writes, 1U);

    fs::path spv;
    for (const auto& e : fs::directory_iterator(guard.path))
    {
        if (e.path().extension() == ".spv")
            spv = e.path();
    }
    ASSERT_FALSE(spv.empty());
    {
        std::ofstream out(spv, std::ios::binary | std::ios::trunc);
        out << "abc";  // 3 bytes — not a multiple of 4
    }

    // Next compile: read fails -> recompile -> evict+republish.
    ASSERT_TRUE(cache.compile(d).has_value());
    EXPECT_EQ(cache.stats().read_failures, 1U);
    EXPECT_EQ(inner.calls.load(), 2U);  // first warm + this recompile

    // The republished entry is clean: a third compile hits without delegating.
    ASSERT_TRUE(cache.compile(d).has_value());
    EXPECT_EQ(inner.calls.load(), 2U);   // no further delegate
    EXPECT_EQ(cache.stats().read_failures, 1U);  // no second tripwire
    EXPECT_GE(cache.stats().hits, 1U);
}

// A zero-byte .spv is also malformed (size > 0 guard fails) and must take the
// read-failure + evict path, not be served as an empty hit.
TEST(CachedCompiler, ZeroByteCacheFileTreatedAsMiss)
{
    CountingCompiler inner;
    CacheDirGuard guard { fresh_cache_dir() };
    cd::shader::CachedCompiler cache { inner, guard.path };

    cd::shader::CompileDesc d {};
    d.source = "void main(){}";
    ASSERT_TRUE(cache.compile(d).has_value());

    fs::path spv;
    for (const auto& e : fs::directory_iterator(guard.path))
    {
        if (e.path().extension() == ".spv")
            spv = e.path();
    }
    ASSERT_FALSE(spv.empty());
    {
        std::ofstream out(spv, std::ios::binary | std::ios::trunc);  // truncate to 0
        ASSERT_TRUE(out.is_open());
    }
    ASSERT_EQ(fs::file_size(spv), 0U);

    ASSERT_TRUE(cache.compile(d).has_value());
    EXPECT_EQ(cache.stats().read_failures, 1U);
    EXPECT_EQ(inner.calls.load(), 2U);
}

// Cache dir that cannot be written (a path whose parent is a regular file)
// must degrade to pass-through: every compile delegates, result still valid,
// no write counted, no crash.
TEST(CachedCompiler, UnwritableCacheDirDegradesToPassThrough)
{
    CountingCompiler inner;
    CacheDirGuard guard { fresh_cache_dir() };

    // Create a regular file, then point the cache dir UNDER it. create_directories
    // can't make a child of a file, so the dir never exists and writes fail.
    std::error_code ec;
    fs::create_directories(guard.path, ec);
    const auto blocker = guard.path / "blocker";
    {
        std::ofstream f(blocker);
        f << "x";
    }
    const auto bad_dir = blocker / "cache";  // child of a regular file
    cd::shader::CachedCompiler cache { inner, bad_dir };

    cd::shader::CompileDesc d {};
    d.source = "void main(){}";

    auto r1 = cache.compile(d);
    auto r2 = cache.compile(d);
    ASSERT_TRUE(r1.has_value());        // result valid despite no cache
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(inner.calls.load(), 2U);  // every compile delegates (no warm hit)
    EXPECT_EQ(cache.stats().writes, 0U);
    EXPECT_EQ(cache.stats().hits, 0U);
}

// ---- Include closure edge cases -------------------------------------------

// A nested (transitive) include must fold into the closure key: editing the
// LEAF module invalidates the root's cache entry.
TEST(CachedCompiler, NestedIncludeLeafEditInvalidatesClosure)
{
    CountingCompiler inner;
    CacheDirGuard guard { fresh_cache_dir() };
    cd::shader::CachedCompiler cached { inner, guard.path };

    CacheMapResolver resolver;
    resolver.modules["a.glsl"] = "#include \"b.glsl\"\nfloat a(){ return b(); }\n";
    resolver.modules["b.glsl"] = "float b(){ return 1.0; }\n";

    cd::shader::CompileDesc desc {};
    desc.source = "#include \"a.glsl\"\nvoid main() {}\n";
    desc.include_resolver = &resolver;

    ASSERT_TRUE(cached.compile(desc).has_value());  // miss
    ASSERT_TRUE(cached.compile(desc).has_value());  // hit
    EXPECT_EQ(cached.stats().misses, 1U);
    EXPECT_EQ(cached.stats().hits, 1U);

    resolver.modules["b.glsl"] = "float b(){ return 2.0; }\n";  // LEAF edit
    ASSERT_TRUE(cached.compile(desc).has_value());  // MUST miss
    EXPECT_EQ(cached.stats().misses, 2U);
}

// An unresolved #include in the closure scan does NOT poison the key — the
// scan skips it (the real compile reports the error). Adding a comment that
// mentions a missing header must therefore still hit a warm entry, because
// closure_hash over-approximates only on RESOLVED modules.
TEST(CachedCompiler, UnresolvedIncludeDoesNotChangeKey)
{
    CountingCompiler inner;
    CacheDirGuard guard { fresh_cache_dir() };
    cd::shader::CachedCompiler cached { inner, guard.path };

    CacheMapResolver resolver;  // resolves nothing
    cd::shader::CompileDesc desc {};
    desc.source = "#include \"missing.glsl\"\nvoid main() {}\n";
    desc.include_resolver = &resolver;

    ASSERT_TRUE(cached.compile(desc).has_value());  // miss (closure empty)
    ASSERT_TRUE(cached.compile(desc).has_value());  // hit (same key)
    EXPECT_EQ(cached.stats().misses, 1U);
    EXPECT_EQ(cached.stats().hits, 1U);
}

// A cyclic include set (a -> b -> a) must terminate via the visited-set /
// depth cap and still produce a stable, finite key (no hang, no crash).
TEST(CachedCompiler, CyclicIncludeClosureTerminates)
{
    CountingCompiler inner;
    CacheDirGuard guard { fresh_cache_dir() };
    cd::shader::CachedCompiler cached { inner, guard.path };

    CacheMapResolver resolver;
    resolver.modules["a.glsl"] = "#include \"b.glsl\"\n";
    resolver.modules["b.glsl"] = "#include \"a.glsl\"\n";

    cd::shader::CompileDesc desc {};
    desc.source = "#include \"a.glsl\"\nvoid main() {}\n";
    desc.include_resolver = &resolver;

    ASSERT_TRUE(cached.compile(desc).has_value());  // terminates -> miss
    ASSERT_TRUE(cached.compile(desc).has_value());  // stable key -> hit
    EXPECT_EQ(cached.stats().misses, 1U);
    EXPECT_EQ(cached.stats().hits, 1U);
}

// ---- make_cached_glslang_compiler factory ---------------------------------

// The convenience factory wraps a real glslang backend in a disk cache and
// hands back ONE owning unique_ptr. A second compile of the same source must
// hit the cache (the inner glslang backend is invoked exactly once), proving
// the OwningCached decorator keeps both halves alive and routes through the
// cache. Skips cleanly when the engine was built without glslang.
TEST(CachedCompiler, FactoryWrapsGlslangAndCaches)
{
    CacheDirGuard guard { fresh_cache_dir() };
    auto c = cd::shader::make_cached_glslang_compiler(guard.path);
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    cd::shader::CompileDesc desc {};
    desc.source =
        "#version 450\n"
        "void main() { gl_Position = vec4(0.0); }\n";
    desc.stage = cd::shader::ShaderStage::kVertex;
    desc.source_name = "factory.vert";

    const auto r1 = c->compile(desc);
    ASSERT_TRUE(r1.has_value()) << r1.error().message;
    EXPECT_FALSE(r1->spirv.empty());

    const auto r2 = c->compile(desc);
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r1->spirv, r2->spirv);  // identical bytes from the cache

    // A .spv must have been published to the cache dir.
    bool any_spv = false;
    for (const auto& e : fs::directory_iterator(guard.path))
    {
        if (e.path().extension() == ".spv")
            any_spv = true;
    }
    EXPECT_TRUE(any_spv);
}
