# ADR-009 — UI Architecture (Dual System)

- **Status**: Accepted (Phase 1 Design)
- **Date**: 2026-05-17
- **Related**: ADR-001 (RHI), ADR-016 (Vendor Matrix)

## Bağlam

Kullanıcı net: **ImGui third-party YASAK**, "grafik API içinde implement edilecek" — custom UI. T19.Q5=B (iki ayrı sistem), T19.Q4=D (CSS+code+asset hot-reload), T19.Q2 (full text stack), T19.Q6=A (vector first-class).

Vendor matrix onayı: **FreeType + Harfbuzz vendor** kaçınılmaz (10+ yıl Unicode shaping). msdfgen opsiyonel.

## Karar

### A. Dual UI Runtime

**`cd::ui`** — Custom IMGUI library. Dear ImGui design pattern (public, lisans yok — sadece API tasarımı) re-implement; C++23 idioms; `cd::rhi` + `cd::math` tight integration. Frame-rebuild, sıfır retained state. Stack layout. ~6-9 ay, ~30 KLOC.

**`cd::editor_ui`** — Custom retained scene-graph UI. Element tree, Flexbox subset layout (custom Yoga-esinli, ~4 KLOC), CSS subset parser + hot-reload, XML markup `.cdui`. Accessibility tree. ~12-18 ay, ~50 KLOC.

**Ortak alt-katman**:
- `cd::ui::DrawList` (her iki sistem tarafından üretilir)
- `cd::ui::text` (FreeType + Harfbuzz + MSDF + raster + subpixel; vendor)
- `cd::ui::vector` (NanoVG-vari path API; kendi yazılır, ~5 KLOC)
- `cd::ui::input` (RHI input layer adaptörü)

### B. `cd::ui` Mimari (Game IMGUI)

Dear ImGui paterni birebir takip (pattern public, kod re-implement):

```
cd::ui::Context     (per-frame state, ID stack, hot/active)
cd::ui::DrawList    (vertex/index buffer accumulator, batched)
cd::ui::IO          (input snapshot, modifier state)
cd::ui::Style       (color + spacing struct, runtime mutable)
cd::ui::Window      (begin/end scope, scroll, clipping)
cd::ui::Storage<ID> (persistent widget state)
```

ID stack: FNV-1a hash + parent stack combined. Layout: stack (cursor model) — Flexbox YOK game tarafında. Animation: lerp + ease-out spring + 2D transform.

**Docking + multi-viewport YOK** game runtime için (editor'a ait).

### C. `cd::editor_ui` Mimari (Retained)

Retained tree (DOM-like):
```
cd::editor_ui::Element
  ├── ComputedStyle (resolved CSS)
  ├── LayoutBox (Flexbox output)
  ├── Children<Element>
  ├── EventHandlers (delegate-based)
  └── AccessibilityNode (UIA/AT-SPI bridge — ADR-012)
```

Slate (Unreal) + Avalonia UI reference. Each node = `shared_ptr<Element>` (retain semantics). Diff manuel (parent->add_child/remove_child); React-style virtual DOM **YOK** (C++'da pahalı).

**Layout**: Custom Flexbox subset (main axis + cross axis + grow/shrink/basis + justify-content + align-items + wrap). ~3-4 KLOC, 3-4 ay. Grid layout Sprint 10+ ertelendi.

**Yoga vendor reddedildi** (library-oriented engine'de external dep eşiği, subset yeterli, CSS-uyumsuz default'lar confusing).

**CSS subset parser** (~2 KLOC, hand-written recursive descent):
- Selectors: type, class, id, descendant, child, pseudo (`:hover`, `:focus`, `:checked`)
- Properties: layout (display, flex-*, padding, margin, width/height), visual (background, border, color), font (font-family, font-size, font-weight)
- Units: px, %, em, rem, fr (grid reserved)
- @import, @media (subset), CSS variables (`--var`)

**RmlUi vendor reddedildi** (fork maliyeti font/layout backend çıkarmak).

### D. Markup Language (XML subset)

`.cdui` markup (XML, tooling free LSP/syntax). Hot-reload doğal asset pipeline (ADR-006).

Code-side: Builder pattern (`Element::div().class("toolbar").child(Button("Save"))`).

### E. Text Pipeline (T19.Q2 = hepsi)

```
TTF/OTF file
  → FreeType (font face, glyph outline) [VENDOR K2]
  → Harfbuzz (shaping: ligature, RTL, kerning, complex script) [VENDOR K2]
  → Glyph atlas:
      ├── Raster (small sizes, hinted): FreeType bitmap → atlas
      ├── Subpixel AA (LCD): FreeType LCD filter → 3-channel atlas
      └── MSDF (large/scaled): msdfgen → R8G8B8 SDF atlas [OPTIONAL VENDOR]
  → Shaper output (glyph_id, advance, offset)
  → Layout (line break: ICU optional / custom UAX#14)
  → DrawList glyph quads (instanced)
```

Vendor justification: FreeType ~150 KLOC, 25 yıl olgun; Harfbuzz ~200 KLOC, Unicode shaping spec (OpenType GSUB/GPOS) devasa. UE, Unity, Godot, Blender, Chrome, Firefox hepsi vendor.

### F. Vector Graphics (T19.Q6 = A)

**NanoVG-vari API** (zlib license — pattern alındı, kod re-implement ~5 KLOC):
- Path command'lar: moveTo, lineTo, bezierTo, arcTo
- Fill / stroke + paint (gradient, pattern)
- CPU-side tessellation (Earcut benzeri) → triangle list → UI DrawList

**Lottie playback** Sprint 12+ scope dışı.
**GPU rasterization** (tile-based Slug/Pathfinder/Vello) Sprint 10+ — Sprint 9 sadece CPU.

**Skia vendor reddedildi**: 1M+ LOC, build complexity (GN/ninja), Google ecosystem.

### G. Aşma Noktaları

1. **Tek IMGUI API, dual backend embed**: `cd::ui` IMGUI `cd::editor_ui` retained tree'sine "host element" olarak embed edilebilir (Unreal SlateImGui pattern).
2. **DrawList lingua franca**: Hem game hem editor → `cd::ui::DrawList` → S1 RHI tek pipeline.
3. **CSS-in-IMGUI bridge**: Game UI widget'ları minimal CSS class lookup (opt-in). HUD theme switch.
4. **Vector + Text birleşik atlas**: Glyph atlas + icon/vector cache aynı texture pool.
5. **Hot-reload asset coupling**: CSS değil — tüm UI assets (icon SVG, font, .cdui markup) S6 üzerinden.
6. **Accessibility ilk gün**: Element tree retained → UIA/AT-SPI bridge "ücretsiz".
7. **Color management**: cd::renderer color space entegrasyon — UI sRGB→linear, HDR UI overlay opt-in.

## Reddedilen

- **Single retained**: IMGUI verimi (debug tools, profiler overlay) kayıp.
- **Single IMGUI**: editor accessibility + complex layout + animation prohibitive.
- **Reactive declarative (SwiftUI-vari)**: C++ metaprogramming maliyeti, compile time, debug acısı.
- **Dear ImGui vendor**: kullanıcı yasakladı; custom RHI bağlama esnekliği kaybı.
- **Yoga vendor**: subset yeterli; external dep eşiği.
- **RmlUi vendor**: fork maliyeti.
- **Cassowary constraint**: O(n³), overkill, sadece Apple.
- **Skia vendor**: 1M+ LOC, scope overshoot.

## Sonuçlar

**Pozitif**:
- Endüstri-standart dual (UE/Unity).
- Custom kontrol, RHI tight integration, license temizliği.
- Vendor matrix minimal (FT+HB+opsiyonel msdfgen).
- Hot-reload doğal.
- Accessibility retained tree'den "ücretsiz".

**Negatif**:
- 18-27 ay engineering toplam (paralel akışla 18 ay realistic).
- IMGUI re-implement Dear ImGui'ye çok benzer — "innovation" yok, sadece ownership.
- CSS parser yazımı bug-prone (web compat tuzakları).
- Flexbox subset spec edge case'leri.

**Replace-Ready (D1)**: FreeType+Harfbuzz K2 (replace yok). msdfgen K2 opsiyonel. Custom IMGUI/editor zaten bizim.

## Açık Sorular

| ID | Soru | Çözüm |
|---|---|---|
| Q1 | Game UI'da docking/multi-viewport gerekli mi? | Editor; game'de yok |
| Q2 | `.cdui` markup XML mi QML-vari custom DSL? | XML (tooling argümanı) |
| Q3 | ICU vendor (BiDi, line break UAX#14) mi custom subset? | ICU optional K3, BiDi opsiyonel |
| Q4 | GPU vector rasterization S10 vs S12+? | S10 değerlendir |
| Q5 | Lottie öncelik? | Backlog (asset team baskısı varsa S12) |
| Q6 | Editor UI ana thread vs kendi thread? | Slate-vari ana thread default |
| Q7 | Tween library vs CSS animation/transition compile? | İkisi de v2; v1 tween |
| Q8 | Subpixel AA hala değerli mi (high-DPI)? | Sprint 9'da opt-in |

## Cross-Cutting

- **ADR-001 (RHI)**: UI render pass (alpha-blended, depth-off, scissor stack). Dynamic VB/IB ring buffer, frame-N+1 reuse. Texture atlas binding. Premultiplied alpha PSO.
- **ADR-002 (Renderer)**: UI compositing — final swapchain blit önce/sonra UI overlay slot. HDR UI tone-mapping path. Multi-viewport (editor) per-window swapchain.
- **ADR-006 (Asset)**: Font asset (.ttf/.otf), glyph atlas cache, `.cdui` markup XML, `.css` stylesheet, SVG icon (NanoVG path bake). Hot-reload watcher event → UI invalidate dispatch.
- **ADR-012 (Editor)**: `cd::editor_ui` direkt editor framework; tools/panels/viewport host buradan.

## Kanıt

- Dear ImGui (pattern reference, kod değil): github.com/ocornut/imgui
- Slate (Unreal editor UI): https://docs.unrealengine.com/en-US/slate
- Avalonia UI: avaloniaui.net
- FreeType: freetype.org (FTL/GPL2 dual)
- Harfbuzz: harfbuzz.github.io (MIT)
- msdfgen (Chlumský 2015 thesis): github.com/Chlumsky/msdfgen — **STUB**
- W3C CSS Flexbox Level 1: w3.org/TR/css-flexbox-1/
- NanoVG: github.com/memononen/nanovg (zlib)
- Loop & Blinn (2005) Resolution Independent Curve Rendering — **STUB**
- Nehab & Hoppe (2008) Random-Access Vector Graphics — **STUB**
