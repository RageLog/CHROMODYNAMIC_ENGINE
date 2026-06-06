# cd::asset::shader_cache

Asset-tier in-process SPIR-V blob cache. An additive **sidecar** of
`cd::shader::CachedCompiler` — never a version bump, never a glslang
dependency. The cache layer treats SPIR-V words as opaque payloads
keyed by `ShaderKey { source_hash, entry_point, stage, spec_const_hash }`.

The library exists so loaders that receive **pre-compiled** SPIR-V
(`.pak`, `.cdtex`, baked shipping packs) can store / retrieve blobs
without dragging in the glslang compile fan-out. The compiler path
(`cd::shader::CachedCompiler`) is the right tool for source-to-SPIR-V;
this cache is the right tool for SPIR-V-to-disk + SPIR-V-from-disk.

## Public surface

```cpp
namespace cd::asset::shader_cache {

struct ShaderKey
{
    std::string  source_hash;       // opaque; caller computes
    std::string  entry_point;       // "main", "vs_main"
    uint32_t     stage;             // cd::shader::ShaderStage cast to uint32
    uint32_t     spec_const_hash;   // 0 = none
};

class ShaderCache
{
public:
    void                          store(ShaderKey, std::vector<uint32_t> spirv);
    [[nodiscard]] std::optional<std::vector<uint32_t>>
                                  lookup(const ShaderKey&) const;
    [[nodiscard]] std::size_t     entry_count() const noexcept;

    cd::expected<void, Error>     save_to_disk(const std::filesystem::path&);
    cd::expected<void, Error>     load_from_disk(const std::filesystem::path&);
    void                          clear();
};

}
```

## File format

```
[8 B  magic       "CDSC\x00\x01\x00\x00"]
[4 B  entry_count]
for entry in entries:
    [4 B  source_hash_len ] [N B source_hash    ]   (no NUL)
    [4 B  entry_point_len ] [N B entry_point    ]   (no NUL)
    [4 B  stage           ]
    [4 B  spec_const_hash ]
    [4 B  spirv_word_count] [N*4 B spirv words  ]
    [8 B  cached_at_ms    ]
```

All multi-byte integers are little-endian. The format is intentionally
simple — no compression, no versioned frames. A future sprint can wrap
it with a streaming compressor or bump the magic for a v2 break; today
it is stable enough for shipping pack consumers to depend on.

## Decoupling from `cd::shader`

`cd::shader` is listed as a PUBLIC dep **only** so callers can pass
`cd::shader::ShaderStage` enum values without a separate
`target_link_libraries`. The cache itself never calls into glslang or
any `ICompiler` — the dep is purely for the shared type vocabulary
(`stage` field). The cache library is `EXCLUDE_FROM_INSTALL` only
because `cd::shader` transitively is (vendored glslang via FetchContent).

## Hash key conventions

`source_hash` is **caller-computed and treated as opaque**. The cache
does no source parsing. Common authoring patterns:

* `xxhash64(source_bytes)` rendered as hex — fast, 16 chars.
* `sha256` truncated to 16 chars — cryptographically strong if shared.
* Build-system content hash — `bazel`/`buck`/`ninja` already compute
  one; reuse it verbatim.

`spec_const_hash` is `0` when no specialisation constants are bound;
otherwise the caller's choice (typically a small `xxhash32` over the
sorted `(id, value)` pairs).

## See also

* `cd::shader::CachedCompiler` — disk-backed compiler decorator for
  source-to-SPIR-V (depends on glslang).
* `cd::asset::pak` — shipping pack format that embeds pre-compiled
  SPIR-V using this cache's on-disk layout.
