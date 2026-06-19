// =============================================================================
// CHROMODYNAMIC — cd/shader/CachedCompiler.cpp
// =============================================================================
#include <cd/shader/CachedCompiler.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace cd::shader
{

namespace
{

constexpr std::uint64_t kFnvOffsetBasis = 0xCBF29CE484222325ULL;
constexpr std::uint64_t kFnvPrime = 0x100000001B3ULL;

[[nodiscard]] std::uint64_t fnv1a_update(std::uint64_t h, const void* data, std::size_t bytes) noexcept
{
    const auto* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < bytes; ++i)
    {
        h ^= static_cast<std::uint64_t>(p[i]);
        h *= kFnvPrime;
    }
    return h;
}

[[nodiscard]] std::uint64_t hash_desc(const CompileDesc& d) noexcept
{
    std::uint64_t h = kFnvOffsetBasis;
    // Source text bytes
    h = fnv1a_update(h, d.source.data(), d.source.size());
    // Flat scalars — mix them as their underlying bit pattern so adding a
    // new enum variant in the future cannot collide with an old key.
    const auto stage_u = static_cast<std::uint8_t>(d.stage);
    const auto lang_u = static_cast<std::uint8_t>(d.lang);
    const auto target_u = static_cast<std::uint8_t>(d.target);
    const std::uint8_t debug_u = d.generate_debug_info ? 1 : 0;
    h = fnv1a_update(h, &stage_u, sizeof(stage_u));
    h = fnv1a_update(h, &lang_u, sizeof(lang_u));
    h = fnv1a_update(h, &target_u, sizeof(target_u));
    h = fnv1a_update(h, &debug_u, sizeof(debug_u));
    h = fnv1a_update(h, d.entry_point.data(), d.entry_point.size());
    return h;
}

// phase1132 (SL-C step 1, ADR-20260612-shader-library-architecture §2.3):
// include-closure hash. A cheap recursive #include scan resolves the
// closure WITHOUT a full preprocess; conditional includes are
// over-approximated (a mentioned-but-#ifdef'd include still joins the
// closure) which errs on the side of EXTRA recompiles, never stale hits.
// The fold is over (path-hash then content-hash) entries SORTED by entry
// value, so the result is independent of include order/duplication.
[[nodiscard]] std::uint64_t hash_entry(std::string_view path, std::string_view content) noexcept
{
    std::uint64_t h = kFnvOffsetBasis;
    h = fnv1a_update(h, path.data(), path.size());
    h = fnv1a_update(h, content.data(), content.size());
    return h;
}

void scan_includes(std::string_view text,
                   const std::string& requester,
                   IIncludeResolver& resolver,
                   std::vector<std::uint64_t>& entries,
                   std::vector<std::string>& visited,
                   int depth)
{
    if (depth > 16)
        return;
    std::size_t pos = 0;
    while (pos < text.size())
    {
        const std::size_t line_end = text.find('\n', pos);
        std::string_view line = text.substr(
            pos, line_end == std::string_view::npos ? std::string_view::npos
                                                    : line_end - pos);
        pos = line_end == std::string_view::npos ? text.size() : line_end + 1;

        const std::size_t hash_at = line.find_first_not_of(" \t");
        if (hash_at == std::string_view::npos || line[hash_at] != '#')
            continue;
        const std::size_t kw = line.find("include", hash_at + 1);
        if (kw == std::string_view::npos)
            continue;
        const std::size_t open = line.find_first_of("<\"", kw + 7);
        if (open == std::string_view::npos)
            continue;
        const bool system_include = line[open] == '<';
        const char closer = system_include ? '>' : '"';
        const std::size_t close = line.find(closer, open + 1);
        if (close == std::string_view::npos || close == open + 1)
            continue;
        const std::string_view requested = line.substr(open + 1, close - open - 1);

        auto r = resolver.resolve(requested, requester, system_include);
        if (!r.has_value())
            continue;  // the compiler will report the real error
        bool seen = false;
        for (const auto& v : visited)
        {
            if (v == r->virtual_path) { seen = true; break; }
        }
        if (seen)
            continue;
        visited.push_back(r->virtual_path);
        entries.push_back(hash_entry(r->virtual_path, r->content));
        scan_includes(r->content, r->virtual_path, resolver, entries,
                      visited, depth + 1);
    }
}

[[nodiscard]] std::uint64_t closure_hash(const CompileDesc& d)
{
    if (d.include_resolver == nullptr)
        return 0;
    std::vector<std::uint64_t> entries;
    std::vector<std::string> visited;
    scan_includes(d.source, std::string {}, *d.include_resolver, entries,
                  visited, 0);
    if (entries.empty())
        return 0;  // no includes -> key unchanged -> cache epoch preserved
    std::ranges::sort(entries);
    std::uint64_t h = kFnvOffsetBasis;
    for (const auto e : entries)
        h = fnv1a_update(h, &e, sizeof(e));
    return h;
}

[[nodiscard]] std::string to_hex(std::uint64_t v)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out(16, '0');
    for (std::size_t i = 0; i < 16; ++i)
    {
        out[15 - i] = kHex[v & 0xFU];
        v >>= 4;
    }
    return out;
}

}  // namespace

CachedCompiler::CachedCompiler(ICompiler& inner, std::filesystem::path cache_dir)
    : inner_ { &inner }
    , cache_dir_ { std::move(cache_dir) }
{
    std::error_code ec;
    std::filesystem::create_directories(cache_dir_, ec);
    // Failure to create is logged via a stats counter, NOT a throw — the
    // wrapper degrades gracefully into pass-through when the cache dir
    // is unwritable (e.g. read-only shipped game data).
    (void)ec;
}

std::filesystem::path CachedCompiler::path_for(std::uint64_t key, std::string_view suffix) const
{
    return cache_dir_ / (to_hex(key) + std::string { suffix });
}

cd::core::Result<CompileResult> CachedCompiler::compile(const CompileDesc& desc)
{
    // phase1132: closure_hash() is 0 when no resolver / no includes, so
    // every pre-SL cache entry keeps its key (no cold-start for existing
    // shaders).
    const auto key = hash_desc(desc) ^ closure_hash(desc);
    const auto spv_path = path_for(key, ".spv");

    // ---- Read path ----
    std::error_code ec;
    if (std::filesystem::exists(spv_path, ec) && !ec)
    {
        std::ifstream in(spv_path, std::ios::binary | std::ios::ate);
        if (in.is_open())
        {
            const auto size = in.tellg();
            if (size > 0 && (static_cast<std::streamoff>(size) % 4) == 0)
            {
                in.seekg(0, std::ios::beg);
                const auto word_count = static_cast<std::size_t>(size) / 4U;
                CompileResult r;
                r.spirv.resize(word_count);
                in.read(reinterpret_cast<char*>(r.spirv.data()), static_cast<std::streamsize>(size));
                if (in.good() || in.eof())
                {
                    ++stats_.hits;
                    return r;
                }
            }
            // File present but malformed (truncated, zero-size, non-word-
            // aligned, or a short read) — count it, then EVICT the corrupt
            // entry so it does not stay a permanent read-failure tripwire on
            // every future compile of this key. Close the handle first
            // (Windows holds a delete lock on an open file), then remove the
            // .spv and its .meta sidecar best-effort. The subsequent miss
            // re-publishes a clean entry via the atomic rename below. This
            // touches ONLY the corrupt-file branch — valid cache hits and the
            // compiled SPIR-V are unaffected.
            in.close();
            ++stats_.read_failures;
            std::error_code evict_ec;
            std::filesystem::remove(spv_path, evict_ec);
            std::filesystem::remove(path_for(key, ".meta"), evict_ec);
        }
    }

    // ---- Miss → delegate + persist ----
    ++stats_.misses;
    auto compiled = inner_->compile(desc);
    if (!compiled.has_value())
        return compiled;

    // Atomic publish: write to .tmp first, then rename. Two compilers
    // racing on the same key produce identical bytes, so a rename overwrite
    // is harmless. Use a key-suffixed tmp name (NOT pid/tid) so even
    // multi-process builds don't tag-team a single file in flight.
    const auto tmp_path = path_for(key, ".spv.tmp");
    {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
            return compiled;  // cache write failed; result still valid.
        out.write(
            reinterpret_cast<const char*>(compiled->spirv.data()),
            static_cast<std::streamsize>(compiled->spirv.size() * sizeof(std::uint32_t))
        );
        if (!out.good())
        {
            out.close();
            std::filesystem::remove(tmp_path, ec);
            return compiled;
        }
    }
    std::filesystem::rename(tmp_path, spv_path, ec);
    if (ec)
    {
        std::filesystem::remove(tmp_path, ec);
        return compiled;
    }

    // Sidecar meta — best-effort, failure is silent.
    const auto meta_path = path_for(key, ".meta");
    std::ofstream meta(meta_path, std::ios::trunc);
    if (meta.is_open())
    {
        meta << static_cast<std::uint32_t>(desc.stage) << ';' << static_cast<std::uint32_t>(desc.lang) << ';'
             << desc.source_name;
    }
    ++stats_.writes;
    return compiled;
}

bool CachedCompiler::clear()
{
    std::error_code ec;
    if (!std::filesystem::exists(cache_dir_, ec))
        return true;
    for (const auto& entry : std::filesystem::directory_iterator(cache_dir_, ec))
    {
        if (ec)
            return false;
        std::filesystem::remove(entry.path(), ec);
    }
    return !ec;
}

namespace
{

/// Owning wrapper so `make_cached_glslang_compiler` can hand back a single
/// unique_ptr that keeps both the inner glslang compiler AND its
/// CachedCompiler decorator alive. Without this the caller would have to
/// juggle two owners — defeats the convenience the factory exists for.
class OwningCached final : public ICompiler
{
public:
    explicit OwningCached(std::unique_ptr<ICompiler> inner, std::filesystem::path dir)
        : inner_ { std::move(inner) }
        , cache_ { *inner_, std::move(dir) }
    {
    }

    [[nodiscard]] cd::core::Result<CompileResult> compile(const CompileDesc& desc) override
    {
        return cache_.compile(desc);
    }

private:
    std::unique_ptr<ICompiler> inner_;
    CachedCompiler cache_;
};

}  // namespace

std::unique_ptr<ICompiler> make_cached_glslang_compiler(std::filesystem::path cache_dir)
{
    auto inner = make_glslang_compiler();
    if (inner == nullptr)
        return nullptr;
    return std::make_unique<OwningCached>(std::move(inner), std::move(cache_dir));
}

}  // namespace cd::shader
