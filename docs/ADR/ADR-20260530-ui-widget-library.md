# ADR-20260530 — Engine-Native UI Widget Library (cd::ui::* family)

- **Status**: Accepted (Architect proposal, ready for Phase 1 kickoff)
- **Date**: 2026-05-30
- **Branch**: dev
- **Supersedes / refines**: ADR-009 (2026-05-17, "UI Architecture (Dual System)")
- **Related**: ADR-001 (RHI), ADR-002 (Renderer), ADR-005 (Foundation Policy), ADR-012 (Editor Mechanics), ADR-016 (Vendor Matrix)
- **Author**: architect subagent

## 1. Bağlam (Context)

### 1.1 Bugünkü durum

`engine/ui/ui/include/cd/ui/` altında dokuz **headless state primitive** var (Grep ile doğrulandı):

| Dosya | Sorumluluk | Render? |
|---|---|---|
| `Widget.hpp` | retained tree base + Panel/Label/Button + `DrawCommand` üretimi | hayır (komut emit eder, çizmez) |
| `Anchor.hpp` | piksel-precise anchor + margin solver | hayır |
| `Theme.hpp` | iki sabit palet (dark/light) + spacing token'ları | hayır |
| `ProgressBar.hpp` | `done/total` + `progress()` | hayır |
| `Spinner.hpp` | açı + hız | hayır |
| `Toast.hpp` | süreli bildirim kuyruğu | hayır |
| `Tooltip.hpp` | hover-delay state machine | hayır |
| `TabBar.hpp` | sekme listesi + aktif index | hayır |
| `ContextMenu.hpp` | sağ-tık popup items + position | hayır |

Yani bugün `cd::ui` "veriyi tut, komutu emit et" katmanı. Aşağıdaki kritik katmanlar **yok**:

- Font rasterization (glyph atlas, kerning, shaping)
- Layout engine (flex/grid/constraint solver)
- Input dispatch (focus zinciri, modal capture)
- Renderer (batched vertex/index buffer'lar RHI üzerinden)
- Animation curves (lerp, easing, spring, storyboard)
- Concrete widget catalog (TextInput, Slider, Dropdown, List, Tree, Dialog, Splitter, Color/Curve/File picker, Chart)
- Tasarım sistemi (typography scale, semantic color tokens, motion tokens, elevation)
- Accessibility (focus indicator, tab navigation contract, screen-reader hint)

### 1.2 Neden engine-native UI?

1. **Editor binary (Section C / L5)** retained-mode, brand'lenebilir, kalıcı widget'lar istiyor. ImGui ile dock + property + curve editor + chart yapılabiliyor ama "ship'lenebilir editor" görünümü ve UX'i için **brand-able theme**, **animasyon**, **accessibility** ve **gerçek odak yönetimi** gerek.
2. **Game runtime** HUD, menü, dialog, pause-screen, in-game store overlay'leri istiyor. ImGui game UI olarak shippable değildir (lisans uyarısı yok ama dev-grade look + texture font + immediate-mode-only model).
3. **imgui'nin maliyeti**: editör harici binary'lere ImGui ABI bağımlılığı, theming sınırı, accessibility yok, MSDF font yok, retained model yok, animation yok.
4. **Tek tasarım sistemi**: editör + oyun aynı `cd::ui::theme` üzerinden brand'lenebilmeli. Studio'ya özel skin tek noktada değiştirilebilir.
5. **RHI tarafsızlığı zaten var**: `cd::rhi` Vulkan/D3D12/Metal/OpenGL backend'lerini yöneten interface'i (`ICommandBuffer`, `IDevice`, vb.) zaten taşıyor. UI renderer'ı buna bind etmek doğal.

### 1.3 Kısıtlar

- C++23, modules opsiyonel (header-only mümkün), `-Wall -Werror -Wextra -Wshadow -Wnon-virtual-dtor -Wpedantic -Wconversion`.
- Tek codebase, platform-spesifik kod yok. Backend'e dokunan kısımlar yalnız `cd::rhi` üzerinden.
- `chroma::ui::*` namespace ailesi; her sub-library `cd::<lib>` (örn. `cd::ui::renderer`) bağımsız `cd_add_library(...)` ile tüketilebilir.
- Performans hedefi: 4K @ 60 Hz, ~500 widget, ≤2 ms CPU layout + ≤2 ms GPU UI pass'i.
- Retained-mode (editor) **+** immediate-mode helper'lar (game HUD): ikisi de aynı renderer üzerine oturur.
- License: vendor edilecek her şey MIT/BSL/Apache 2 olmak zorunda — ticari (NoesisGUI) yasak.

### 1.4 ADR-009 ile ilişki

ADR-009 "Dual UI Runtime" (game IMGUI + editor scene-graph) tasarımı çiziyor ve **FreeType + Harfbuzz vendor zorunlu** diyor. Bu ADR onun **somut alt-library bölünmesini, fazlı planını ve renderer kontratını** netleştirir. ADR-009'un kararı geçerli; bu ADR onun implementasyon yol haritasıdır. ADR-009 "stack layout, frame-rebuild" tarif eden tarafı `cd::ui::widgets` immediate-helpers alt-modeli olarak korunur (bkz §3.6).

## 2. Karar (Decision)

`cd::ui::*` ailesini **dokuz katmanlı sub-library** olarak inşa et; her katman bağımsız test edilir, bağımsız tüketilebilir. DAG yukarıdan aşağıya:

```
                cd::editor               (binary, replaces imgui usage)
                game samples             (HUD / menu / overlay)
                       |
                       v
              cd::ui::widgets            (concrete catalog: Button, Slider, …)
              ____________|_____________________________________________
             |       |        |        |        |          |            |
        cd::ui::ui  layout  input  renderer  font  theme(expanded)  animation
                                      |
                                      v
                                 cd::rhi  (existing)
                                      |
                                 cd::math · cd::core · cd::log · cd::asset
                                      |
                              foundation tier
```

Hiçbir geri-kenar yok: `widgets`'in altındaki yedi modül **birbirinden bağımsız** derlenir; `widgets` hepsini toplar.

### 2.1 Sub-library breakdown

| # | Library | Status | Sorumluluk | Dış bağımlılık |
|---|---------|--------|------------|----------------|
| 1 | `cd::ui::ui` | exists (expand) | retained tree + headless primitives (`Widget`, `Anchor`, `ProgressBar`, `Spinner`, `Toast`, `Tooltip`, `TabBar`, `ContextMenu`, `Theme`-stub) | `cd::core` |
| 2 | `cd::ui::font` | **new** | TTF/OTF parse, glyph rasterize, atlas pack, MSDF, shaping (BiDi/script) | `stb_truetype` (Phase 1), `harfbuzz` + `FreeType` (Phase 4 upgrade per ADR-009) |
| 3 | `cd::ui::layout` | **new** | Flex (main/cross axis, justify, align, wrap), Grid, Absolute, Constraint solver. Yoga-esque + Taffy-esque API. Saf, RHI'siz | `cd::math` |
| 4 | `cd::ui::input` | **new** | Hit-test walk, focus chain, tab nav, modal capture, gesture (drag/double-click/long-press), gamepad mapping | `cd::events` (existing input source) |
| 5 | `cd::ui::renderer` | **new** | Batched draw via `cd::rhi::ICommandBuffer` — quad/MSDF-text/9-patch/gradient/blur material'ları, scissor stack, atlas binding, vertex format `pos2+uv2+color4+flags` | `cd::rhi`, `cd::ui::font` |
| 6 | `cd::ui::widgets` | **new** | Concrete widget catalog — bkz §3.6. Retained + immediate-mode helpers | `cd::ui::ui` + tüm sibling'ler |
| 7 | `cd::ui::theme` | exists (expand) | Design tokens — typography scale, semantic color, spacing, motion, elevation, light/dark, brand override | `cd::ui::ui` |
| 8 | `cd::ui::animation` | **new** | Property animation, easing curves, spring solver, timeline + sequence | `cd::math`, `cd::frame_timing` |
| 9 | `cd::ui::a11y` | **new (Phase 4)** | Accessibility tree, focus rendering, narration hints, contrast variants | `cd::ui::ui` |

Her library kendi `tests/` binary'si ile gelir; `cd_add_library` policy bundle (ADR-005) zaten `[[nodiscard]]` + `noexcept` + `override` standardını dayatır.

### 2.2 Renderer kontratı (özet imzalar)

```cpp
// engine/ui/renderer/include/cd/ui/renderer/IUiRenderer.hpp
namespace cd::ui::renderer
{

struct Vertex { float x, y, u, v; std::uint32_t rgba; std::uint32_t flags; };

struct DrawBatch
{
    std::span<const Vertex>       vertices;
    std::span<const std::uint32_t> indices;
    cd::rhi::TextureHandle        atlas;      // font / sprite / 9-patch
    cd::rhi::Rect2D               scissor;
    std::uint32_t                 material_id; // textured / msdf / solid / gradient / blur
};

class IUiRenderer
{
public:
    virtual ~IUiRenderer() = default;
    [[nodiscard]] virtual cd::Result<void> begin_frame(cd::rhi::Extent2D viewport_px) = 0;
    virtual void submit(std::span<const DrawBatch> batches) noexcept = 0;
    [[nodiscard]] virtual cd::Result<void>
    end_frame(cd::rhi::ICommandBuffer& cb) = 0;   // emits draw via RHI
};

[[nodiscard]] std::unique_ptr<IUiRenderer> make_rhi_renderer(cd::rhi::IDevice& dev);

}  // namespace cd::ui::renderer
```

Invariantlar:

- `begin_frame` → `submit*` → `end_frame` sırası enforced (state machine).
- Renderer hiçbir global state tutmaz; her instance kendi pipeline cache'ini taşır.
- 6 fixed material variant (textured / msdf / solid / gradient / blur / sdf-shape) — pipeline switch maliyetini kısar.
- Vertex layout sabittir; widget'lar `Vertex` toplar, renderer dokunmaz.

### 2.3 Layout kontratı (özet imzalar)

```cpp
// engine/ui/layout/include/cd/ui/layout/IFlexSolver.hpp
namespace cd::ui::layout
{

enum class FlexDirection : std::uint8_t { kRow, kColumn, kRowReverse, kColumnReverse };
enum class Justify       : std::uint8_t { kStart, kCenter, kEnd, kSpaceBetween, kSpaceAround, kSpaceEvenly };
enum class Align         : std::uint8_t { kStart, kCenter, kEnd, kStretch, kBaseline };

struct FlexStyle
{
    FlexDirection direction { FlexDirection::kRow };
    Justify       justify   { Justify::kStart };
    Align         align     { Align::kStretch };
    float         gap_main  { 0.0F };
    float         gap_cross { 0.0F };
    float         grow      { 0.0F };
    float         shrink    { 1.0F };
    float         basis     { -1.0F };       // -1 = auto
    float         padding[4] { 0,0,0,0 };    // top/right/bottom/left
};

class IFlexSolver
{
public:
    virtual ~IFlexSolver() = default;
    [[nodiscard]] virtual cd::Result<void>
    solve(cd::ui::Rect viewport, std::span<const FlexStyle> styles,
          std::span<cd::ui::Rect> out_rects) const noexcept = 0;
};

}  // namespace cd::ui::layout
```

Invariantlar:

- Solver saftır (RHI'siz, allocator-friendly): aynı input → aynı output.
- `out_rects.size() == styles.size()`; caller sağlar.
- Constraint solver (Phase 4) Cassowary-tipi ek katmanla gelir; Flex/Grid bu interface'ten geçer.

### 2.4 Input kontratı (özet imzalar)

```cpp
// engine/ui/input/include/cd/ui/input/IInputDispatcher.hpp
namespace cd::ui::input
{

struct PointerEvent { float x, y; std::uint8_t button; bool down; };
struct KeyEvent     { std::uint32_t keycode; bool down; std::uint16_t mods; };
struct TextEvent    { char32_t codepoint; };

class IInputDispatcher
{
public:
    virtual ~IInputDispatcher() = default;

    [[nodiscard]] virtual cd::ui::WidgetId hit_test(float x, float y) const noexcept = 0;
    virtual void capture_modal(cd::ui::WidgetId w) noexcept = 0;
    virtual void release_modal() noexcept = 0;

    virtual void dispatch_pointer(const PointerEvent& ev) = 0;
    virtual void dispatch_key(const KeyEvent& ev) = 0;
    virtual void dispatch_text(const TextEvent& ev) = 0;

    [[nodiscard]] virtual cd::ui::WidgetId focused() const noexcept = 0;
    virtual void focus_next() noexcept = 0;
    virtual void focus_prev() noexcept = 0;
};

}  // namespace cd::ui::input
```

### 2.5 Animation kontratı (özet imzalar)

```cpp
// engine/ui/animation/include/cd/ui/animation/Animator.hpp
namespace cd::ui::animation
{

enum class Easing : std::uint8_t { kLinear, kEaseInOut, kCubicBezier, kSpring };

template <class T>
struct Track
{
    T               from{}, to{};
    float           duration_s { 0.25F };
    Easing          easing { Easing::kEaseInOut };
    float           bezier_p1x{}, bezier_p1y{}, bezier_p2x{}, bezier_p2y{};
    float           spring_stiffness { 170.0F }, spring_damping { 26.0F };
};

template <class T>
[[nodiscard]] T sample(const Track<T>& tr, float t_s) noexcept;

}  // namespace cd::ui::animation
```

### 2.6 Theme genişletmesi (özet imzalar)

```cpp
// engine/ui/ui/include/cd/ui/Theme.hpp (extended)
namespace cd::ui
{

struct Typography
{
    float display_px { 48 }, h1_px { 32 }, h2_px { 24 }, h3_px { 20 };
    float body_px    { 14 }, caption_px { 12 }, overline_px { 10 };
    std::string family_default { "Inter" };
    std::string family_mono    { "JetBrains Mono" };
};

struct Motion
{
    float dur_fast_s  { 0.10F }, dur_med_s { 0.20F }, dur_slow_s { 0.40F };
    float ease_std_p1x { 0.4F }, ease_std_p1y { 0.0F }, ease_std_p2x { 0.2F }, ease_std_p2y { 1.0F };
};

struct Elevation { Color32 shadow { 0,0,0,64 }; float blur_px { 8 }, tint { 0.04F }; };

struct ThemeV2
{
    Theme      legacy;     // backwards-compatible (existing dark/light)
    Typography type;
    Motion     motion;
    Elevation  elevation_1, elevation_2, elevation_3;
};

}  // namespace cd::ui
```

## 3. Fazlı plan (Phased plan)

### Phase 1 — Rendering foundation (~2 hafta)

| Sub-task | Çıktı | Kabul kriteri |
|---|---|---|
| P1.1 | `cd::ui::font` v1 — `stb_truetype` vendor, basic atlas (bin-pack), RGBA8 glyph blit | LRU eviction yok ama 2048×2048 atlas + ASCII + Latin-1 + Türkçe diakritik OK |
| P1.2 | `cd::ui::renderer` v1 — 6 material variant, RHI batch, scissor stack | Vulkan backend canlı çiziyor; D3D12 backend M4 parite ile çalışıyor |
| P1.3 | `cd::ui::layout` v1 — Flex (row/column + justify + align) + Absolute | 100 widget Yoga'ya karşı golden-test PASS |
| P1.4 | `cd::ui::ui` migration — `Widget::collect_draw_commands` artık `cd::ui::renderer::DrawBatch` üretir | Mevcut testler yeşil; eski API stub-only katman geriye uyumlu |
| P1.5 | `samples/ui/hello_ui/` — buton + label + progress bar ekrana çıkıyor | Lavapipe headless ve D3D12 NULL adapter ile golden-image SSIM ≥ 0.99 |

### Phase 2 — Input + concrete widgets (~3 hafta)

| Sub-task | Çıktı |
|---|---|
| P2.1 | `cd::ui::input` — hit-test, focus chain, tab nav, modal capture, mouse+keyboard+gamepad |
| P2.2 | Concrete widget'lar: Button (default/outlined/ghost/icon), Toggle, Checkbox, Radio, TextInput (single+multi), Slider (linear+log), Dropdown, Modal, List |
| P2.3 | `cd::editor` binary aşamalı geçiş: panel başlık çubukları + property drawer'lar → `cd::ui::widgets` (ImGui her panel için paralel canlı kalır; A/B karşılaştırma) |

### Phase 3 — Theming + animation (~2 hafta)

| Sub-task | Çıktı |
|---|---|
| P3.1 | `ThemeV2` — typography scale + semantic color + spacing + motion + elevation token'ları |
| P3.2 | `cd::ui::animation` — property animation, cubic-bezier + spring, timeline |
| P3.3 | Light + dark teması v2, runtime hot-swap |

### Phase 4 — Editor-spesifik widget'lar + a11y baseline (~2 hafta)

| Sub-task | Çıktı |
|---|---|
| P4.1 | Color picker, curve editor, file picker |
| P4.2 | Splitter + dock-space (panel layout persistence) |
| P4.3 | Chart + graph (profiler + telemetry için) |
| P4.4 | `cd::ui::a11y` baseline — focus indicator rendering + tab nav contract + high-contrast tema variant |
| P4.5 | Font stack upgrade: `stb_truetype` → **FreeType + HarfBuzz** vendor (ADR-009 zorunluluğu). MSDF generation (msdfgen opsiyonel; CDR cache). BiDi/Arabic/Devanagari shaping. |

### Phase 5 — Game runtime polish + lokalizasyon (~2 hafta)

| Sub-task | Çıktı |
|---|---|
| P5.1 | `samples/ui/hello_hud/` — game HUD overlay (health/ammo/minimap), imm-mode helpers ile |
| P5.2 | `samples/ui/hello_menu/` — pause-screen + settings + key remap |
| P5.3 | Multi-resolution: 4K + 1080p + mobile-portrait golden-image karşılaştırması |
| P5.4 | Lokalizasyon: `cd::i18n` lookup hook'u + RTL layout flip + Arabic/Hebrew shaping kabul testi |

**Toplam**: ~11–13 hafta. Önerilen tek-yönlü zincirleme; Phase 1 + 2 paralel sub-strand kabul edilebilir (font + renderer P1.1/P1.2 farklı geliştiriciler).

## 4. Reddedilen alternatifler

1. **ImGui'yi her yerde kullanmaya devam et**
   _Red_: dev/debug-grade; ship'lenebilir game UI değil; brand'lenemez; accessibility yok; retained-mode yok; immediate-mode loop input dispatch'i savurganca tekrarlıyor. Editor için fade-out planı zaten Phase 2–5 boyunca devrede.

2. **NoesisGUI / Coherent UI / RmlUi vendor**
   _Red_: NoesisGUI ticari (lisans maliyeti — projemiz "vendor matrix" politikasında MIT/BSL/Apache 2 + zero-cost diyor; ADR-016 ile çakışır). Coherent kapatıldı. RmlUi (MIT) ilginç ama CSS parser + DOM modeli **yıllar boyu sürdürülebilirlik yükü**; bizim tasarım sistemine zorlama mapping; ABI churn riski. Tasarım dilimizi own etmek istiyoruz.

3. **Slate-style C++ DSL only (no markup, no hot-reload)**
   _Kısmen kabul_: Phase 1–3 saf C++ DSL ile başlanır (en hızlı), Phase 6+ (sonraki ADR) opsiyonel `.cdui` markup + hot-reload (ADR-009 T19.Q4=D talebi). C++ DSL editor + HUD için yeterli; markup ileri faz.

4. **GPU-side retained scene graph (Filament UI tarzı)**
   _Red_: Mevcut `cd::rhi` pipeline'ı per-frame batched draw'a göre tasarlandı; GPU-side scene graph ikinci bir render path yaratır, framegraph entegrasyonu karmaşıklaşır. Skia/NanoVG/ImGui tarzı CPU-side vertex toplama + GPU batch draw daha temiz.

5. **Pure CSS-like RmlUi-style styling**
   _Kısmen kabul_: CSS benzeri **token isimlendirme** (`surface`, `on-surface`, `primary`, vb. Material-3'ten) `ThemeV2`'ye taşınır; tam CSS parser **yok**. Parser çok-aylık bir iş; bizim "design tokens" yeterli.

6. **Headless-only — render'ı user yapsın**
   _Red_: Bugünkü durum bu; problem da bu. Editor + game'in tutarlı look'u için renderer **kütüphane içinde** olmalı. ADR-009 zaten "own this layer" diyor.

## 5. Sonuçlar (Consequences)

### Olumlu

- **Editör shippable** hale gelir (Section C / L5 kilidi açılır).
- **Game UI** tek-namespace + tek-tema ile editor ile tutarlı görünür.
- RHI-agnostic 1. günden — Vulkan + D3D12 + Metal + OpenGL aynı `cd::rhi` arkasında.
- Tasarım sistemi (typography + semantic color + spacing + motion) brand'lenebilir.
- Accessibility 1. sınıf vatandaş — sektörde Slate dışında çok az örnek var (NoesisGUI'de var ama ticari).
- MSDF font 1. günden → 4K + scale-up okunabilir.
- ABI tek namespace'te (`cd::ui::*`); third-party UI lib bağımlılığı yok → distribution daha temiz.

### Olumsuz

- **~11–13 hafta** dedicated iş. Marathon planı içinde **Phase 1 + 2** kritik path; geri kalan paralel sub-strand kabul.
- Library sayısı +7 (`font`, `layout`, `input`, `renderer`, `animation`, `a11y`, `widgets`); README + CMake policy hijyeni Run 15+ "94% coverage" hedefini sarsabilir → her library shipping ile birlikte README zorunlu.
- Font stack iki aşamalı: `stb_truetype` (Phase 1, basit) → `FreeType + HarfBuzz` (Phase 4, ADR-009 zorunluluğu). Geçişte mevcut renderer pipeline + atlas tasarımı **kararlı** olmalı; aksi halde re-work maliyeti büyük.
- ImGui dev-debug için kalır (overlay panelleri, perf HUD) ama editör binary'sinden Phase 5 sonunda **çıkar**. Geçiş sırasında iki UI loop paralel — input dispatch çakışmasını önlemek için Phase 2 başında `cd::ui::input` "ImGui önce mi cd::ui önce mi?" priority chain'i karara bağlanmalı (öneri: `cd::ui` viewport içinde, ImGui sadece dev-overlay'de).
- `stb_truetype` (MIT, header-only) + ileride `FreeType` (FTL/GPLv2) + `HarfBuzz` (MIT) vendor tail bir miktar artar — ADR-016 vendor matrix güncellemesi (Phase 4 öncesi).

## 6. Mevcut `cd::ui::ui` ile entegrasyon

Mevcut headless primitiv'ler **bozulmaz**; aşağıdaki bridge'ler eklenir:

1. `Widget::collect_draw_commands` zaten `DrawCommand { kRect, kText }` üretiyor. Phase 1.4'te bu üretim `cd::ui::renderer::Vertex` batch'lerine **fan-out adapter** ile dönüşür (eski API geriye uyumlu kalır → mevcut testler kırılmaz).
2. `Anchor.hpp` saf math; `cd::ui::layout::Absolute` modelinin tam karşılığı — solver'ın absolute path'inde aynı imzayı reuse eder.
3. `Theme.hpp` mevcut iki paleti `ThemeV2.legacy` içinde tutar; yeni `Typography/Motion/Elevation` alanları yan yana eklenir.
4. `ProgressBar/Spinner/Toast/Tooltip/TabBar/ContextMenu` headless state'leri **olduğu gibi** kalır; Phase 2'de `cd::ui::widgets` içindeki concrete render edilen widget'lar bunları **state holder** olarak içerir.
5. `Button/Label/Panel` (Widget.hpp içinde) ya `cd::ui::widgets`'a taşınır ya da bridge ile genişletilir — karar Phase 2.1 başlangıcında; Developer önerisi: **taşı**, mevcut konum `cd::ui::ui::legacy` namespace altında deprecated-shim olarak kalır.

## 7. State-of-the-art comparison

| Özellik | NoesisGUI | UE Slate | Filament UI | Dear ImGui | **CHROMODYNAMIC hedef** |
|---|---|---|---|---|---|
| Retained mode | Var | Var | Yok | Yok | **Var (`cd::ui::ui`)** |
| Immediate-mode helper | Yok | Limited | Var | Var | **Var (`cd::ui::widgets` imm helpers)** |
| RHI-agnostic | Var | Var (RHI) | Var (Filament) | Var (backend per platform) | **Var (`cd::rhi`)** |
| MSDF font | Var | Yok | Yok | Yok | **Var (Phase 1; Phase 4 + HarfBuzz)** |
| Theme tokens | Brushes/Styles | StyleSet | Limited | Limited | **Material-3-like tokens (Phase 3)** |
| Animation | Storyboard | Curves | Yok | Yok | **Spring + cubic-bezier + timeline (Phase 3)** |
| Accessibility | Var | Var | Yok | Yok | **Baseline (Phase 4)** |
| Markup / hot-reload | XAML | Slate C++ | Yok | Yok | **Phase 6+ `.cdui` (opsiyonel)** |
| Game HUD ergonomic | OK | OK | OK | Best (imm) | **Best (mixed retained + imm helpers)** |
| Lisans | Ticari | Source-available | Apache 2 | MIT | **Engine-internal (MIT-uyumlu vendor tail)** |
| BiDi / shaping | Var | Var | Yok | Limited | **Var (Phase 4 HarfBuzz)** |
| Dock-space | Yok | Var | Yok | Var (docking branch) | **Var (Phase 4)** |
| Curve editor | Yok | Var | Yok | Yok | **Var (Phase 4)** |
| Chart widget | Limited | Yok | Yok | Implot 3rd party | **Var (Phase 4)** |

## 8. DAG + cycle audit (kanıtlı)

Önerilen DAG'da geri kenar yok:

- `cd::ui::widgets` → {`ui`, `font`, `layout`, `input`, `renderer`, `theme`, `animation`, `a11y`}
- `cd::ui::renderer` → {`rhi`, `font`} (font'a sadece atlas binding için; layout/widgets bilmiyor)
- `cd::ui::font` → {} (vendor `stb_truetype` saf)
- `cd::ui::layout` → {`math`} sadece
- `cd::ui::input` → {`events`, `ui`} — `ui`'yi sadece `WidgetId` tipi için kullanır (FORWARD declare ile zayıflatılabilir)
- `cd::ui::animation` → {`math`, `frame_timing`}
- `cd::ui::theme` → {`ui`} (sadece `Color32`'yi paylaşır)
- `cd::ui::a11y` → {`ui`, `input`}

Doğrulama: Developer Phase 1.0'da `Grep -r "#include \"cd/ui"` ile her sub-library include'larını taramalı; hiçbir alt katman `widgets` veya `editor`'a back-edge **yapmamalı**. CI'da include-graph script'i (`scripts/check_dag.py` benzeri — yoksa Phase 1.0'da kur) cycle'ları reddetmeli.

## 9. Veto kriterleri Developer için

Architect, aşağıdaki tasarım sapmalarını **veto** eder ve iade eder:

1. `cd::ui::renderer` içinde Vulkan-spesifik tip kaçağı (`VkBuffer`, `D3D12_*`). Sadece `cd::rhi::*` tipleri görünmeli.
2. `cd::ui::widgets` içinde `cd::rhi::*` doğrudan include. Widget'lar `IUiRenderer` interface'i üzerinden geçmeli (loose coupling).
3. `cd::ui::ui` içinde herhangi bir backend dep (renderer / rhi). Headless primitive'ler korunur.
4. `std::string` magic-type yerine `WidgetId`/`ThemeTokenId`/`FontId` strong typing'i kullan; FONT/THEME/WIDGET hash'leri `cd::core::Strong<...>` üzerinden.
5. Sanal metodlar `[[nodiscard]]` + uygun olanlar `noexcept`; `override` zorunlu (ADR-005 policy bundle zaten dayatıyor).
6. Global state YOK; `IUiRenderer` + `IInputDispatcher` + `IFlexSolver` instance'ları explicit context olarak geçilir.

## 10. Faz 1 kickoff dispatch (önerilen)

Aşağıdaki sıra **paralel-sub-agent**'a uygun (her madde bir Developer task'i):

1. **P1.0**: `engine/ui/font/CMakeLists.txt` + `engine/ui/font/include/cd/ui/font/{IGlyphAtlas.hpp,IFontRasterizer.hpp,FontHandle.hpp}` skeleton + `stb_truetype` vendor (single header `Dependencies/stb/stb_truetype.h`).
2. **P1.0** (paralel): `engine/ui/layout/CMakeLists.txt` + `IFlexSolver.hpp` + `FlexStyle` + saf-math Phase 1 solver (Yoga'nın sub-set'i; row/column + justify + align + gap + padding).
3. **P1.0** (paralel): `engine/ui/renderer/CMakeLists.txt` + `IUiRenderer.hpp` + 6 material enum + `Vertex` struct + scissor stack tasarımı.
4. **P1.1**: Font Phase 1 implementation — atlas bin-pack (skyline veya guillotine), Latin + Türkçe glyph rasterize, kerning lookup.
5. **P1.2**: Renderer Phase 1 implementation — Vulkan backend ilk; D3D12 paralel (M4 parite halihazırda var); pipeline cache 6 material variant için preload.
6. **P1.3**: Layout Phase 1 implementation — Flex solver, 30 unit-test Yoga golden ile karşılaştırma.
7. **P1.4**: `cd::ui::ui` adapter — `Widget::collect_draw_commands` çağrıldığında dahili olarak `cd::ui::renderer::Vertex` batch'i üretebilen yeni `to_batches(...)` üye fonksiyonu (eski API korunur).
8. **P1.5**: `samples/ui/hello_ui/main.cpp` — 1 Button + 1 Label + 1 ProgressBar; Vulkan + D3D12 golden-image SSIM ≥ 0.99.

Önerilen başlangıç dispatch: **3 paralel strand (font, layout, renderer skeleton)**, ardından integrasyon P1.4 + P1.5 tek strand.

## 11. Açık sorular (Phase 1 başında karara bağlanacak)

- Q1: `stb_truetype` mu `FreeType` mu Phase 1'de? **Öneri (Architect)**: `stb_truetype` (header-only, MIT, sıfır build maliyeti). FreeType Phase 4'te ADR-009 zorunluluğu ile gelir.
- Q2: Scissor stack max derinlik? **Öneri**: 32 — modal + tooltip + dropdown + tab + nested panel için bol.
- Q3: 6 material variant gerçekten yeterli mi? **Öneri**: Phase 1'de evet; Phase 3'te `blur` ve `shadow` material'ları için iki ek SDF-derive material gerekebilir; pipeline cache'i 8 slot'a açık tut.
- Q4: Markup (`.cdui`) hangi fazda? **Öneri**: Phase 6 (bu ADR kapsamı dışı). Şu an C++ DSL yeter.
- Q5: ImGui geçiş süresince input priority chain? **Öneri**: viewport'ta `cd::ui` önce, dev-overlay'de ImGui önce; karara Phase 2.1'de `cd::ui::input::SetCaptureMode` API'si ile bağlanır.

## 12. Onay durumu

- Architect (bu ADR'nin sahibi): **kabul edildi**.
- Team-lead onayı: bekleniyor (Phase 1 kickoff için Developer task ataması).
- Developer kontratı: §2.2–§2.6 imzalar; §9 veto kriterleri; §10 dispatch sırası.

## 13. Sonraki adım

Team-lead bu ADR'yi okur, Phase 1 dispatch'i (§10) Developer'a paralel-sub-agent çağrısıyla atar. Architect Phase 1.0 sonunda DAG check'i ile dönüş yapar (cycle / layer violation taraması).

— Architect, 2026-05-30
