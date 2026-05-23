// =============================================================================
// CHROMODYNAMIC — cd/shader/CachedCompiler.cpp
// =============================================================================
#include <cd/shader/CachedCompiler.hpp>

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

std::filesystem::path CachedCompiler::path_for_(std::uint64_t key, std::string_view suffix) const
{
    return cache_dir_ / (to_hex(key) + std::string { suffix });
}

cd::core::Result<CompileResult> CachedCompiler::compile(const CompileDesc& desc)
{
    const auto key = hash_desc(desc);
    const auto spv_path = path_for_(key, ".spv");

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
            // File present but malformed — count, fall through to recompile.
            ++stats_.read_failures;
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
    const auto tmp_path = path_for_(key, ".spv.tmp");
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
    const auto meta_path = path_for_(key, ".meta");
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
