# apps/editor — Black Screen Root Cause

**Date**: 2026-05-31
**Symptom**: `apps/editor` binary boots cleanly (log shows "dock layout ready with 7 nodes (5 panels)", "booted Vulkan backend + Renderer swapchain", "clean exit (6619 frames)") but presents a solid dark-navy window — no panels, no dock splits, no widgets visible.

## Root cause

`apps/editor/main.cpp:654`:

```cpp
constexpr bool kSubmitterPipelineReady = false;
if constexpr (kSubmitterPipelineReady)
{
    submitter.record(cmd, frame.extent);
}
```

The UI submitter (which encodes the `DrawBatcher`-emitted panel quads into the active render pass) is **deliberately gated off** by a `constexpr bool` set to `false`. The render pass therefore:

1. begin_render_pass → swapchain clear (navy `{0.06, 0.07, 0.10, 1.0}`)
2. `submitter.record()` skipped (gate is false)
3. end_render_pass

Result: only the clear color reaches the screen. All `batcher.quad(...)` calls accumulate vertex/index/command data but never get submitted to the GPU.

The inline comment explains the intent:

> The Submitter pipeline lands with cd::material UI variants per ADR-20260530 Phase 1.5 (mirrors hello_ui's gating). Until then submitter.record() is gated — the swapchain clear above is what proves the editor's frame loop is alive.

This is an intentional milestone gate, not a bug per se — but the user-visible result is indistinguishable from a broken editor.

## Cross-check

The same gate exists in `samples/ui/hello_ui` (the gate is mirrored from there). When `cd::material` ships its UI variants and `hello_ui` flips its gate, `apps/editor` should flip in the same commit.

## Fix path

Two routes, in priority order:

### Route A — flip the gate (needs cd::material UI variants)

1. Ship the cd::material UI shader variant (vertex + fragment) under `engine/render/material/ui/` or wherever the ADR-20260530 Phase 1.5 milestone parks it.
2. Wire it into `cd::ui::renderer_rhi::Submitter::create(...)` so `submitter.record()` has a valid pipeline to bind.
3. Flip `kSubmitterPipelineReady` to `true` in both `apps/editor/main.cpp` AND `samples/ui/hello_ui/main.cpp`.
4. Verify: `editor.exe --headless 5` produces the expected DrawBatcher draw-call count > 0 per frame; visually the 5 panel quads + splitter lines + tab strips render in their theme palette colors.
5. Optional: add a no-regression test that asserts `submitter.command_count() > 0` after the first non-trivial frame.

**Effort**: 1-2 weeks (material UI variants are a real piece of work — needs vertex format + fragment sampler + descriptor layout + pipeline cache key + theme palette UBO).

### Route B — fallback path that proves the editor is alive (cheap)

If the cd::material work is queued behind heavier items, drop in an INLINE shader path in `cd::ui::renderer_rhi::Submitter` (similar to what `samples/game/hello_world` does — inline GLSL push-constant MVP). This is what gets the editor visible without waiting on cd::material:

1. Add a fallback `Submitter::create_with_inline_shader(...)` constructor that compiles a minimal GLSL UI vertex+fragment via the existing `cd::shader` glslang path.
2. Same pipeline format as DrawBatcher (pos2 + uv2 + color4).
3. Single push-constant for viewport size.
4. No texture sampler in v1 (use vertex color only) — covers panel backgrounds + splitter lines + tab strip rectangles. Glyph rendering needs the cd::ui_font atlas which DOES need a texture sampler, so glyphs stay grey/missing until Route A.

**Effort**: 2-3 days. Unblocks visibility, leaves glyphs as the only known visual gap.

## Queue placement

This item is **not in scope of Marathon-2-revised** (which is library + new-editor focus — Marathon-2 ships the panel libraries that *use* the Submitter, but doesn't address the Submitter gate itself). It belongs in **Marathon-3 or N+2**, alongside the cd::material UI variant work that ADR-20260530 Phase 1.5 already names.

**Suggested entry** (to be added to `docs/MARATHON_PLAN_NEXT.md` after M2 wraps, under a new T0.4 row):

```
### T0.4 Editor Submitter gate flip (apps/editor visible)

* **Status**: BLOCKED on cd::material UI variants (per ADR-20260530 Phase 1.5).
* **Symptom**: editor boots clean, frame loop alive, but presents only swapchain clear.
* **Audit**: docs/AUDIT/editor-black-screen-2026-05-31.md
* **Route A**: ship cd::material UI variants → flip kSubmitterPipelineReady in apps/editor + hello_ui (1-2 weeks).
* **Route B**: inline GLSL fallback in Submitter::create_with_inline_shader (2-3 days, unblocks visibility, glyphs deferred).
* **Effort**: A=1-2 weeks, B=2-3 days.
```

## Notes

- The gate is in **apps/editor** (new editor binary, in-scope per project direction) — not in hello_engine, so this IS work we'll do, not legacy noise.
- `cd::ui_widgets`, `cd::ui_renderer_rhi`, `cd::ui_font`, the 4 panel libraries (Marathon-2-revised W2), `cd::editor_panel`, and `cd::editor` are ALL functioning correctly — they emit the right DrawBatcher commands. The gap is exclusively at the GPU-pipeline stage.
- The behavior is **deterministic and reproducible**: every run shows the same dark-navy window.
- No leak, no crash, no rendering-thread issue — clean exit at 6619 frames.
