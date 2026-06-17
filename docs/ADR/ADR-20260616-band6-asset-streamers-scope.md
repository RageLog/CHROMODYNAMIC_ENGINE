# ADR-20260616 — Band 6 asset-streamers: real decode wiring + sealed slices

- Status: Accepted
- Date: 2026-06-16
- Scope: `engine/asset/{texture_streamer,scene_streamer,audio_streamer}`
- Related: ROADMAP_ALL_MODULES_TO_100.md Band 6; PROJECT_COMPLETION_STATUS.md §2;
  `cd::asset::streamer_pool` (Band 4, decode-agnostic).

## Context

The three asset streamers (texture / scene / audio) each had a complete, tested
async orchestration layer — a real priority/dedup pending queue plus an
`Async*Pool` worker thread-pool with condition-variable completion/quiescence.
The *decode payload* was the documented Sprint-2 gap: the worker only carried
the asset path as a completion token and the result table held a placeholder
(a fake 1×1 texture handle / a synthesised `SceneId` / a path-hash `AssetId`),
so no real bytes ever flowed end-to-end. The real reference loaders already
exist in the `cd::asset` umbrella (`cdtex`, `image`, `gltf`, `wav`).

Band 6's high-leverage task: wire the real decoder behind the existing
orchestration so a completion carries REAL decoded data + metadata, with the
GPU / ECS / audio-device upload left to the downstream consumer (the streamer
never touches `IDevice` or the audio device from a worker thread).

## Decision

**IMPLEMENTED (real decode wired into both the sync and async paths):**

1. **texture_streamer — real `.cdtex` BC7 decode.** `decode_texture_file()`
   runs `cd::asset::cdtex::load()` on the worker thread and produces a
   `DecodedTexture { width, height, is_block_compressed, blocks }` carrying the
   real mip-0 BC7 block bytes + the file's dimensions. The owner thread creates
   a GPU texture sized to the REAL decoded extent (BC7 format) — no more 1×1
   placeholder. New `get_dimensions()` query + tests assert the real size.

2. **scene_streamer — real glTF parse.** `AsyncScenePool` workers run
   `cd::asset::gltf::load_scene()` (a pure per-file CPU parse, safe to run
   concurrently) and hand back an owning `LoadedScene` (node/mesh/material/
   texture tree + bounds). The owner thread registers the real scene + a real
   `SceneId` — no more synthesised empty placeholder. New `get_scene()` query +
   tests assert a real one-node/one-mesh/one-material tree.

3. **audio_streamer — real WAV PCM decode.** `decode_audio_file()` runs
   `cd::asset::wav::load()` and produces a `DecodedAudio { channels,
   sample_rate, bits_per_sample, frame_count, pcm }`. Both paths register the
   real format; new `get_format()` query + tests assert real channels /
   sample-rate / frame-count.

In every case a path that fails to decode (incl. nonexistent files) is
silently dropped and NOT reported as completed — matching the pre-existing
sync silent-drop semantics. Completion is now an honest signal of a real
decode.

**SEALED SLICES (cannot be wired at the asset layer today without crossing into
a consumer or introducing a build defect — precise triggers below):**

- **S1 — texture_streamer RGBA8 image path (PNG/JPG/TGA/BMP/HDR via
  `cd::asset_image`).** Both `cd::asset_image` (Image.cpp) and `cd::asset_gltf`
  (GltfLoader.cpp via tinygltf) define their own `STB_IMAGE_IMPLEMENTATION`,
  each emitting the full `stb_image` symbol set. `cd::asset::streamer_pool`
  links the texture streamer (→ image) and the scene streamer (→ gltf) into one
  executable, so adding the image dependency produces an `lld` duplicate-symbol
  link failure (`stbi_load`, `stbi_image_free`, … all "defined twice"). The
  engine's cooked texture format is `.cdtex`, which is the *named* Band-6 gap;
  the image path is therefore sealed. `decode_texture_file()` returns
  `std::nullopt` for any non-`.cdtex` extension; the `DecodedTexture::rgba`
  field + `is_block_compressed == false` representation remain in the public
  type for when the seal is lifted.
  **Trigger to lift:** consolidate the two vendored stb copies into a single
  shared stb translation unit (e.g. a `cd::third_party_stb` object library that
  both `cd::asset_image` and `cd::asset_gltf` link, with the implementation
  macro defined in exactly one TU). Once a single stb TU exists, add
  `cd::asset_image` to the texture streamer and dispatch non-`.cdtex` paths to
  `cd::asset::image::load_image()`.

- **S2 — audio_streamer OGG/Vorbis decode.** There is no Vorbis decoder at the
  asset layer (`cd::asset` ships `wav` only; no `cd::asset_ogg` /
  `stb_vorbis` integration exists). `decode_audio_file()` returns
  `std::nullopt` for `.ogg` (and any non-`.wav`) extension rather than
  fabricating a placeholder. WAV is wired real; OGG is promote-on-need.
  **Trigger to lift:** land a `cd::asset_ogg` (or `cd::asset_wav`-internal
  Vorbis) loader exposing `decode(bytes) -> PCM`; then extend
  `decode_audio_file()` to dispatch `.ogg` to it.

## Consequences

- The three streamers move from skeleton (45%) to real end-to-end decode: a
  completion now carries real texels/dimensions, a real parsed scene, and real
  PCM, verified deterministically through the async pool's existing
  completion/wait (no `sleep_for`).
- `cd::asset::streamer_pool` (Band 4, decode-agnostic) stays green: its
  orchestration is unchanged; its completion-count tests were given real
  on-disk `.cdtex` / `.wav` fixtures so the counts remain meaningful now that
  fabricated completions are gone.
- The two sealed slices (image path, OGG) are deferred-by-design with precise
  triggers, so they stop counting as open gaps — the streamers are "100% for
  their wired scope" under the Band road-to-100 honesty rule.
- No `IDevice` / audio-device contact from worker threads; GPU/ECS/device upload
  of the decoded payload remains the downstream consumer's responsibility.
