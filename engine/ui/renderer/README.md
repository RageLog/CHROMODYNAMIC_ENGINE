# cd::ui_renderer

**Purpose**: CPU-side draw batcher for the cd::ui widget renderer. Phase 1.2 of ADR-20260530-ui-widget-library. Converts UI commands (`quad` / `textured_quad` / `glyph`) into a flat `Vertex` + `uint16` index buffer pair plus a scissor- and state-keyed `DrawCommand` list ready for RHI submission.

**Namespace**: `cd::ui::renderer`.

**Headers**: `cd/ui/renderer/DrawBatcher.hpp`.

**Primary types**:
- `cd::ui::renderer::DrawBatcher` -- pure CPU batcher. `begin_frame()` resets buffers, emit calls append vertices + indices, `vertices()` / `indices()` / `commands()` return the result.
- `cd::ui::renderer::Vertex` -- 24-byte struct: `pos2 + uv2 + color4 (RGBA8) + variant (uint8) + 3 pad`. Matches the renderer pipeline vertex layout from the ADR.
- `cd::ui::renderer::DrawCommand` -- one `(variant, texture_slot, scissor)` group of indices. The RHI side iterates this list and binds scissor / pipeline / descriptor before `draw_indexed`.
- `cd::ui::renderer::material::k*` -- variant constants: `kSolid`, `kTextured`, `kGlyph`, `kNinePatch`, `kLinearGrad`, `kBlur`.
- `cd::ui::renderer::ScissorRect`, `cd::ui::renderer::AtlasUv`, `cd::ui::renderer::Color`.

**Algorithm**:
1. `quad / textured_quad / glyph` emits 4 vertices + 6 indices (winding 0-1-2 / 0-2-3, CCW from top-left).
2. Adjacent emissions with identical `(variant, texture_slot, scissor)` MERGE into the same `DrawCommand` (extends its `index_count`).
3. Differing state begins a new command.
4. `push_scissor / pop_scissor` mutates the current scissor, which becomes part of the command key.
5. Empty quads (`w <= 0 || h <= 0`) are silently dropped before consuming any buffer slot.

**Phase 1.2 scope**:
- 16-bit index (max 65535 vertices per frame -- ~16K quads, plenty for any reasonable UI page).
- Three concrete emitters: `quad`, `textured_quad`, `glyph`.
- Scissor stack.
- State-merge for batch coalescing.

**Out of Phase 1.2** (Phase 1.2b + Phase 2+):
- RHI submission tier (`cd::ui_renderer_rhi`): takes `DrawBatcher` output + a `cd::rhi::IDevice` and uploads + records draw calls. Not yet written.
- 9-patch / linear-gradient / blur emitters (variant ids reserved; emitter functions Phase 2).
- 32-bit index path for ≥ 16K quad scenes.
- Auto-flush when the index limit overflows mid-frame.

**Usage**:
```cpp
#include <cd/ui/renderer/DrawBatcher.hpp>

cd::ui::renderer::DrawBatcher batcher;
batcher.begin_frame();

batcher.quad(10.0F, 10.0F, 100.0F, 32.0F,
             cd::ui::renderer::Color { 32U, 36U, 48U, 255U });

batcher.push_scissor({ 0, 0, 200U, 200U });
auto glyph = font.glyph_uv(0x41U);  // 'A' from cd::ui::font
batcher.glyph(20.0F, 18.0F, glyph->width, glyph->height,
              /* texture_slot = */ 0U,
              { glyph->u0, glyph->v0, glyph->u1, glyph->v1 },
              cd::ui::renderer::Color::white());
batcher.pop_scissor();

// RHI side (Phase 1.2b):
auto verts = batcher.vertices();   // upload into vb
auto idx   = batcher.indices();    // upload into ib
for (auto& cmd : batcher.commands())
{
    bind(cmd.scissor);
    bind(cmd.texture_slot);
    bind(pipeline_for(cmd.variant));
    draw_indexed(cmd.index_offset, cmd.index_count);
}
```

**Test command**: `ctest --preset ninja-debug -R cd_test_batcher --output-on-failure`. Covers ~10 cases including emit / merge / split / scissor / empty / glyph / index winding.

**Notes**:
- The Vertex struct is 24 bytes (not 20 as a hand count would suggest): the trailing `(variant + 3 pad)` aligns to a 4-byte slot under default alignment rules. Pipeline layout in the shader is `vec2 + vec2 + uint8x4 + uint8x4`, total 24 bytes -- matches.
- Solid quads use `texture_slot = 0xFFFFFFFFu` (sentinel "no texture") so they merge across UV variants without forcing a texture rebind.
- The RHI integration layer (Phase 1.2b) will be its own library so this batcher stays trivially unit-testable without a GPU.
