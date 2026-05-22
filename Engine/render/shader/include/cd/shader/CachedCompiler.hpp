// =============================================================================
// CHROMODYNAMIC — cd/shader/CachedCompiler.hpp
//
// Decorator around `ICompiler` that persists compiled SPIR-V to disk and
// returns cached blobs on subsequent invocations with the same inputs.
//
// The cache key is a 64-bit FNV-1a hash over:
//   * source text bytes
//   * stage
//   * language
//   * target environment
//   * entry-point name
//   * generate_debug_info flag
//
// FNV-1a is intentionally NOT cryptographic — we want a fast key, not a
// signature. A collision means the cache returns the wrong SPIR-V; with
// 64-bit width and an engine-scale shader corpus (≪ 1 M shaders) the
// birthday-bound probability is on the order of 1e-12, which we accept.
// Callers that need stronger guarantees can layer a real hash (SHA-256)
// on top by extending `CacheKey::compute`.
//
// File layout:
//   <cache_dir>/<key_hex>.spv     32-bit SPIR-V words
//   <cache_dir>/<key_hex>.meta    one-line UTF-8: "{stage};{lang};{name}"
// The .meta sidecar lets a future cache-bust tool clear shaders for one
// specific stage / source-name without nuking the whole directory.
//
// Concurrency:
//   * Read path is wait-free (single open() + read() + parse), safe under
//     concurrent readers.
//   * Write path takes std::filesystem::rename for atomic publication —
//     two threads compiling the same hash both end up with a complete
//     file; the loser's rename succeeds silently overwriting the winner.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/Result.hpp>
#include <cd/shader/Compiler.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace cd::shader
{

/// Statistics for unit tests + the eventual debug HUD. Reset to zero on
/// construction; never decremented (monotonic counters).
struct CacheStats
{
    std::uint64_t hits { 0 };
    std::uint64_t misses { 0 };
    std::uint64_t writes { 0 };
    std::uint64_t read_failures { 0 };  ///< Cache file existed but could not be read / parsed.
};

class CachedCompiler final : public ICompiler
{
public:
    /// Wrap `inner` with a disk cache under `cache_dir`. The directory is
    /// created on construction if it does not exist. `inner` must outlive
    /// the CachedCompiler (we keep an unowned pointer so multiple wrappers
    /// can share a single backend instance — e.g. one cache per project).
    explicit CachedCompiler(ICompiler& inner, std::filesystem::path cache_dir);

    [[nodiscard]] cd::core::Result<CompileResult> compile(const CompileDesc& desc) override;

    [[nodiscard]] const CacheStats& stats() const noexcept
    {
        return stats_;
    }

    /// Wipe the cache directory contents. Used by tests and by tools that
    /// need to force-recompile (engine version bump, shader-include macro
    /// change). Returns false if the directory cannot be cleared.
    bool clear();

private:
    [[nodiscard]] std::filesystem::path path_for_(std::uint64_t key, std::string_view suffix) const;

    ICompiler* inner_;
    std::filesystem::path cache_dir_;
    CacheStats stats_ {};
};

/// Convenience factory: wraps `cd::shader::make_glslang_compiler()` so a
/// sample / runtime can opt into caching with one call. The owned inner
/// compiler stays alive for the lifetime of the returned wrapper.
[[nodiscard]] std::unique_ptr<ICompiler> make_cached_glslang_compiler(std::filesystem::path cache_dir);

}  // namespace cd::shader
