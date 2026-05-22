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

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace
{

namespace fs = std::filesystem;

class CountingCompiler final : public cd::shader::ICompiler
{
public:
    [[nodiscard]] cd::core::Result<cd::shader::CompileResult>
    compile(const cd::shader::CompileDesc& desc) override
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
    return fs::temp_directory_path() /
           ("cd_shader_cache_" + std::to_string(static_cast<std::uint64_t>(stamp)) + "_" +
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
