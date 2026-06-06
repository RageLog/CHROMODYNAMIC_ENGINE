# cd::asset::validator

Pre-load asset validation. Catches **malformed, oversized, or missing
assets BEFORE they enter the render pipeline** — the failure surfaces
in the loader call site rather than later in a confusing rhi-level
error from a half-uploaded GPU resource.

## Sprints

| Sprint | Phase | Surface |
|--------|-------|---------|
| 1 | `phase611` | Magic-header checks + non-empty blob + per-format size-limit policy. |
| 2 | queued     | Deep semantic validation (mip chains, mesh topology, glTF schema). |

The Sprint-1 surface is intentionally tiny and dependency-free: only
`cd::core`. The validator can run anywhere — boot path, hot-reload
worker, CI asset linter — without dragging in `cd::asset_gltf` or
`cd::rhi`.

## Validated formats

| Family    | Formats validated                                          |
|-----------|------------------------------------------------------------|
| Texture   | PNG, JPEG, KTX2, DDS, HDR (Radiance RGBE), CDTex           |
| Scene     | glTF 2.0 (`.gltf` JSON + `.bin`, `.glb` binary container)  |
| Audio     | WAV (RIFF), OGG (Ogg Vorbis)                               |

## Public surface

```cpp
namespace cd::asset::validator {

enum class Class : uint8_t { kTexture, kScene, kAudio };

struct ValidationOptions
{
    std::size_t max_size_bytes      { 256ULL * 1024ULL * 1024ULL };
    bool        require_magic       { true };
    bool        allow_empty_payload { false };
};

cd::expected<void, Error>
    validate(std::span<const std::byte> blob,
             Class                       cls,
             const ValidationOptions&    opts = {});

}
```

## What each Sprint-1 check does

```
  Check               Action
  ─────               ──────
  magic_header        First N bytes match the format's known signature
                      (e.g. PNG = 89 50 4E 47 0D 0A 1A 0A).
  size_limit          blob.size() ≤ max_size_bytes (default 256 MiB).
  non_empty           blob.size() > 0 unless allow_empty_payload.
```

## Error domain

`validate()` returns `cd::expected<void, Error>` where `Error`
carries one of:

* `kFormatUnknown`    — magic header didn't match any known signature.
* `kSizeLimit`        — payload exceeded `max_size_bytes`.
* `kEmptyPayload`     — blob was empty and `allow_empty_payload=false`.
* `kCorruptedHeader`  — magic matched but follow-on header bytes
  failed a structural check (e.g. PNG IHDR chunk length wrong).

## Usage

```cpp
auto blob = read_file_to_bytes(path);
auto v = cd::asset::validator::validate(
    blob, cd::asset::validator::Class::kTexture);
if (!v.has_value())
{
    log_error("asset validation failed for {}: {}", path, v.error());
    return;
}
// safe to hand off to the actual loader
```

## Dependencies

Only `cd::core`. No glTF, no rhi, no audio decoder, no image
decoder. The validator works on raw bytes.
