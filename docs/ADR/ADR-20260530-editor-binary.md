# ADR-20260530 — Standalone Editor Binary (cd_editor_app)

- **Status**: Proposed (Phase 1 design only — no implementation)
- **Date**: 2026-05-30
- **Branch**: dev
- **Author**: architect subagent (orchestrated under team-lead)
- **Iglberger format**: Context / Decision / Rejected Alternatives / Consequences
- **Supersedes / refines**: ADR-012 (Editor Mechanics, 2026-05-17) — same intent at the
  *mechanics* layer; this ADR specifies the *binary*, the *project file format*, and
  the integration contract with the libraries that have shipped since (cd::ui::editor,
  cd::asset::gltf::load_scene, cd::render::scene::ingest_gltf_scene, cd::script).
- **Related**: ADR-009 (UI Architecture), ADR-012 (Editor Mechanics), ADR-006 (Asset),
  ADR-004 (ECS-Scene), ADR-20260530-generic-gltf-scene-loading,
  ADR-20260530-ui-widget-library, ADR-20260530-gameplay-library-family,
  ADR-017 (DtForHil Pattern Salvage / Plugin DLL hot-reload),
  ADR-015 (Concurrency Job System), ADR-005 (Foundation Policy).

---

## 1. Bağlam (Context)

### 1.1 Bugünkü durum

The engine currently ships an **embedded editor toolkit** but no standalone binary:

| Layer                              | Where it lives today                          | Status                                        |
|------------------------------------|-----------------------------------------------|-----------------------------------------------|
| Editor framework                   | `engine/ui/editor/` (`cd::editor`)            | Built; aggregates panels (Editor.hpp).        |
| Hierarchy view                     | `cd::editor::HierarchyView`                   | Built; tree-walks `cd::scene::Scene`.         |
| Property inspector                 | `cd::editor::ui::PropertyInspector`           | Built; reflects component data.               |
| Selection set                      | `cd::editor::SelectionSet` + `SelectionOutline` | Built; selection outline as a render pass.  |
| Transform gizmo (3D)               | `cd::editor::AxisGizmo` + `TransformGizmo`    | Built; emits `TransformCommands` (undoable).  |
| Undo/redo history                  | `cd::editor::EditHistory` (`ICommand` stack)  | Built; bounded depth + 4 MiB budget.          |
| Command palette                    | `cd::editor::CommandPalette` (Ctrl+P)         | Built; searchable action menu.                |
| Bookmarks                          | `cd::editor::Bookmark`                        | Built; saved camera transforms.               |
| Preferences store                  | `cd::editor::PreferencesStore`                | Built; persists layout/theme.                 |
| Asset palette (drag-drop source)   | `cd::editor::ui::AssetPalette`                | Built (skeleton).                             |
| Generic glTF load                  | `cd::asset::gltf::load_scene` → `LoadedScene` | **Designed** in ADR-20260530-generic-gltf-scene-loading (phase 439). |
| Generic glTF ingest                | `cd::render::scene::ingest_gltf_scene`        | **Designed** in same ADR (phase 440).         |
| Lua scripting                      | `cd::script::Engine` (Lua 5.4)                | Built; opaque VM wrapper.                     |
| Sample harness                     | `samples/engine/hello_engine/`                | Built; **not** a shippable editor binary.     |

Yani: *parçalar var, ürün yok.* hello_engine bir geliştirici-örneği — drag-drop yok,
project file yok, multi-document yok, Play-in-Editor yok, hot-reload bus dağınık,
plugin DLL yok. Editör kavramı `cd::editor::Editor` sınıfında bir **framework**
olarak kalmış; **bir kullanıcının çift-tıklayıp açtığı tek bir .exe** olarak değil.

### 1.2 Neden ayrı bir binary?

1. **Editor-runtime ayrımı**: oyuncuya gönderilen oyun binary'si editor kodunu
   içermemeli (binary boyut + saldırı yüzeyi + lisans). Aynı şekilde editör
   kullanıcı oyun yapımcısıdır — oyun binary'sinin gameplay döngüsüne mahkum
   olmamalıdır.
2. **Stable user surface**: bir IDE gibi açılır, son projeyi açar, "File → New
   Project" menüsü vardır, .cdproj uzantısını OS associate eder.
3. **Plugin yüzeyi**: editör binary'si DLL/`.so` hot-reload yapar (ADR-017 P2);
   oyun binary'sinde bu mekanizma olmamalı (production'da güvenlik riski).
4. **Asset hot-reload yüksek QoS gerektirir**: file-watcher + ingest pipeline'ı
   oyun runtime'ında **opsiyonel**, editör runtime'ında **zorunlu**. Ayrı binary
   bu ayrımı taşıyabilir.
5. **Phase 1 disiplini**: bu ADR sadece **tasarım**. Implementasyon yok. Editör
   binary'sinin yaslanacağı kontratlar (`cd::ui` Phase 1+2, `cd::ui::editor`,
   `cd::asset::gltf::load_scene`, `cd::render::scene::ingest_gltf_scene`,
   `cd::script::Engine`) hazır veya tasarlanmış durumda — bu ADR onları bir
   ürüne **paketler**.

### 1.3 Kısıtlar

- C++23, `-Wall -Werror -Wextra -Wshadow -Wnon-virtual-dtor -Wpedantic -Wconversion`.
- Tek codebase, platform-spesifik kod platform abstraction katmanından geçer
  (`cd::platform`, `cd::events`); OS-spesifik dosya diyaloğu/sürükle-bırak
  `cd::platform::native_dialog` arkasında.
- Vendor matrisi: editor binary'si yalnız MIT/BSL/Apache 2 bağımlılık ekler
  (ADR-016). Editör shell'in kendisi vendor-free C++ — bütün heavy-lift
  zaten engine library'lerinde.
- `chroma::editor::app::` namespace; tüm binary kodu `apps/editor/` altında;
  *hiçbir engine library'si editor binary'sine bağımlı olmaz* (yön DAG: editor
  → libs, libs → editor değil).
- Performans: 60 Hz tick (interactive); editor render pass'i ≤4 ms CPU + ≤4 ms
  GPU 1080p'de; cold-start ≤2 s SSD'de orta boy proje.
- Determinism: project file yükle → kapat → tekrar yükle yan etki üretmemeli
  (asset registry idempotent).

### 1.4 ADR-012 ile ilişki

ADR-012 editör mekaniklerini (gizmo space/snap/pivot, level editing, ProBuilder,
spline, terrain, outliner, viewport quad-view, hot-reload, scripting) çiziyor.
Bu ADR onun **binary paketleme + project format + asset-flow + PIE + plugin**
parçasını netleştirir. ADR-012'deki mekanikler aynen geçerli; bu ADR onların
**hangi proseste**, **hangi dosya formatıyla**, **hangi olay bus'ında**
yaşadığını tarif eder.

---

## 2. Karar (Decision)

### 2.0 Binary topology

Yeni bir uygulama target'ı kurulur:

```
apps/editor/                          # NEW
  CMakeLists.txt                      # cd_add_executable(cd_editor_app)
  src/
    Main.cpp                          # entry point, App lifecycle
    App.hpp / App.cpp                 # session shell (chroma::editor::app)
    ProjectFile.hpp / ProjectFile.cpp # .cdproj load/save (TOML/JSON)
    AssetBrowser.hpp / AssetBrowser.cpp
    PieController.hpp / PieController.cpp
    HotReloadBus.hpp / HotReloadBus.cpp
    PluginHost.hpp / PluginHost.cpp
    DockLayout.hpp / DockLayout.cpp
  resources/
    default-layout.toml
    default-theme.toml
    cd-editor.ico
  tests/
    test_project_file.cpp
    test_asset_browser_routing.cpp
    test_pie_lifecycle.cpp
    test_hot_reload_bus.cpp
    test_plugin_host_unload.cpp
```

Link grafiği (compile-time DAG, single direction):

```
                          cd_editor_app  (binary)
                                |
        +-----------+-----------+-----------+-----------+-------------+
        |           |           |           |           |             |
   cd::ui::*    cd::ui::      cd::scene  cd::render  cd::asset    cd::script
   (Phase 1+2)  editor                                  (+ gltf)   (Lua 5.4)
        |           |
        v           v
                 cd::ecs · cd::input · cd::math · cd::concurrency
                                |
                            cd::rhi · cd::log · cd::core · foundation
```

Yön kuralı: **editor binary kütüphaneleri bilir; kütüphane editör'ü bilmez.**
İhlal CMake helper'ı (`cd_add_library`) reddeder.

### 2.1 Project file format — `.cdproj`

#### 2.1.1 Format seçimi

- **Tercih**: TOML 1.0 (insan-okur, diff-dostu, açık şema). Fallback parser:
  `cd::asset::json` üzerine TOML mini-reader (bu ADR'nin Phase 2 implementation
  detayı; sözleşme format-bağımsız).
- **İkincil seçenek**: JSON Schema (Draft 2020-12), eğer TOML reader maliyeti
  fazlaysa. CI'da iki readi de pas etmeli.
- **Dosya uzantısı**: `.cdproj` (Windows file association + macOS Info.plist
  UTI: `engine.chroma.cdproj` Phase 2).
- **Versiyon**: `schema_version = 1` zorunlu; reader forward-compatible major
  bump'a kadar.

#### 2.1.2 Şema (v1)

```toml
schema_version = 1

[project]
name        = "MyAdventure"
guid        = "8c4d2e10-4f1f-4b91-9e3a-3d3b9e7b9a8c"   # stable identity
created_at  = "2026-05-30T12:00:00Z"
chroma_min  = "1.0.0"                                   # engine version gate

[paths]
asset_root  = "Content"                                 # relative to .cdproj
scene_root  = "Content/Scenes"
script_root = "Content/Scripts"
build_out   = "Build"

[startup]
opening_scene = "Content/Scenes/Main.cdscene"
camera_bookmark = "default"                             # cd::editor::Bookmark name

[editor]
layout      = "Layouts/default-layout.toml"             # dock state (saved separately)
theme       = "dark"
recent_files = [
  "Content/Scenes/Main.cdscene",
  "Content/Scenes/Boss.cdscene",
]

[runtime]
target_fps      = 60
pie_window_mode = "embedded"                            # "embedded" | "popout"
gameplay_lua    = "Content/Scripts/main.lua"

[[plugins]]
id       = "cd.plugin.example_tool"
path     = "Plugins/example_tool.dll"                   # or .so / .dylib
auto_load = true

[asset_database]
# pointer to a separate .cdadb cache file — cdproj does NOT
# embed the asset index (kept small, mergeable).
cache_path = ".cdcache/asset_index.cdadb"
```

#### 2.1.3 Invariantlar

1. `guid` ömür boyu sabit (Git merge sırasında çakışma olursa kullanıcı
   manuel çözer; tool refüze etmez ama warn'lar).
2. `paths.*` daima **proje köküne relatif** (cross-machine reproducibility).
3. `recent_files` MRU listesi UI-state; reader hata vermeden silebilir.
4. `chroma_min` engine'in `cd::core::version()` ile karşılaştırılır; geriye-uyumlu
   olmayan minor bump editör user-confirm sorar.
5. Bilinmeyen alan reader tarafından **korunur** (round-trip preserves unknown
   keys); Phase 2'de bir alan eklemek eski projeyi bozmaz.

#### 2.1.4 API kontratı

```cpp
namespace chroma::editor::app {

struct ProjectPaths {
    std::filesystem::path asset_root;
    std::filesystem::path scene_root;
    std::filesystem::path script_root;
    std::filesystem::path build_out;
};

struct ProjectStartup {
    std::filesystem::path opening_scene;
    std::string           camera_bookmark;
};

struct ProjectPluginEntry {
    std::string                  id;
    std::filesystem::path        path;
    bool                         auto_load { true };
};

struct ProjectFile {
    std::uint32_t                schema_version { 1 };
    std::string                  name;
    std::string                  guid;
    std::string                  created_at;
    std::string                  chroma_min;
    ProjectPaths                 paths;
    ProjectStartup               startup;
    std::string                  editor_layout_path;
    std::string                  editor_theme;
    std::vector<std::filesystem::path> recent_files;
    std::uint32_t                target_fps { 60 };
    std::string                  pie_window_mode { "embedded" };
    std::filesystem::path        gameplay_lua;
    std::vector<ProjectPluginEntry> plugins;
    std::filesystem::path        asset_cache_path;
    // round-trip preservation:
    std::string                  raw_trailing_blob;
};

enum class ProjectError : std::uint32_t {
    kFileNotFound, kParseFailed, kSchemaTooNew, kInvariantViolation
};

[[nodiscard]] auto load_project(std::filesystem::path cdproj)
    -> std::expected<ProjectFile, ProjectError>;

[[nodiscard]] auto save_project(const ProjectFile& proj,
                                std::filesystem::path cdproj)
    -> std::expected<void, ProjectError>;

}  // namespace chroma::editor::app
```

### 2.2 Asset browser

#### 2.2.1 Sorumluluk

Asset browser, project'in `paths.asset_root` ağacını tarar, dosya tiplerini
tanır, thumbnail render eder, drag-source davranışı sağlar. **Drop hedefi**
viewport veya hierarchy view'dir.

#### 2.2.2 Drag-drop glTF flow (canonical)

```
+---------------------------+      drag begin       +-------------------+
| Asset browser tree        | --------------------> | Drag payload:     |
| (Sponza.gltf seçili)      |                       |   {kind: kGltf,   |
+---------------------------+                       |    path: <abs>}   |
                                                    +-------------------+
                                                              |
                                                              v
                                              drop on viewport (3D pick)
                                                              |
                                                              v
                            +-------------------------------------+
                            | cd::asset::gltf::load_scene(path)   |  ADR-20260530
                            |   -> LoadedScene (POD, owning)      |  generic-gltf
                            +-------------------------------------+
                                              |
                                              v
                  +-------------------------------------------------+
                  | cd::render::scene::ingest_gltf_scene(           |
                  |   device, world, scene, loaded,                 |
                  |   IngestOptions { drop_world_xform })           |
                  |     -> IngestResult { root_entity, ... }        |
                  +-------------------------------------------------+
                                              |
                                              v
                           +---------------------------------+
                           | Wrap in undoable command:       |
                           | cd::editor::SpawnSceneCommand   |
                           |   (push to EditHistory)         |
                           +---------------------------------+
                                              |
                                              v
                        Selection set <- IngestResult.root_entity
                        Hierarchy view auto-scrolls/expands new root
```

`SpawnSceneCommand::apply()` ingest sonucunu sahneye ekler; `revert()` tüm
oluşturulan entity'leri tek kalemde despawn eder. ECS-level batch destroy
zaten `cd::ecs::World::destroy_subtree(root)` üzerinden mevcut → command
state'i sadece `root_entity` + kayıp parent referansı taşır (~32 byte).

#### 2.2.3 Tanınan tipler ve handler tablosu

| Uzantı         | Handler                                                  | Drop hedefi            |
|----------------|----------------------------------------------------------|------------------------|
| `.gltf`/`.glb` | `cd::asset::gltf::load_scene` + `ingest_gltf_scene`      | viewport / hierarchy   |
| `.cdscene`     | `cd::scene::load_scene_file`                             | hierarchy (replace)    |
| `.cdmesh`     | `cd::asset_cdmesh::load` + create static mesh entity     | viewport               |
| `.cdtex`/`.ktx2` | `cd::asset_ktx2::load` + assign to selected material | inspector slot         |
| `.wav`/`.ogg`  | `cd::asset_wav::load` + create audio source entity       | hierarchy              |
| `.lua`         | bind to selected entity's `ScriptComponent`              | inspector slot         |
| `.cdproj`      | refüze (kendi içine project drop yok)                    | —                      |

Bilinmeyen uzantı: drop sırasında "Unsupported asset type" toast; eklenmeye
çalışıldığında plugin registry sorgulanır (§2.7 plugin sözleşmesi
`register_asset_handler` hook'u).

#### 2.2.4 Thumbnail cache

`.cdcache/thumbnails/<sha256(path+mtime)>.png` — asset değişirse cache
miss; render thread'i kullanmadan offline thread (cd::concurrency job)
ile üretilir. Cap: 256 MiB default, LRU evict.

### 2.3 Inspector panel pattern

`cd::editor::ui::PropertyInspector` (mevcut) seçili entity'nin tüm
component'larını walk eder ve **component-başına property drawer** çağırır.
Property drawer kayıt mekaniği şudur:

```cpp
namespace chroma::editor::app {

template <typename Component>
struct PropertyDrawerTraits {
    static void draw(cd::editor::ui::PropertyInspector::Context& ctx,
                     Component& comp);
    static constexpr std::string_view label = Component::kEditorLabel;
};

// Plugin / gameplay code:
inline void register_builtin_drawers(cd::editor::PropertyDrawerRegistry& reg) {
    reg.register_drawer<cd::scene::LocalTransform>(PropertyDrawerTraits<cd::scene::LocalTransform>::draw);
    reg.register_drawer<cd::scene::Light>(PropertyDrawerTraits<cd::scene::Light>::draw);
    reg.register_drawer<cd::render::Renderable>(PropertyDrawerTraits<cd::render::Renderable>::draw);
    // ...
}

}  // namespace chroma::editor::app
```

Drawer'lar **mutasyonları doğrudan yapmaz** — `cd::editor::EditHistory.push(
std::make_unique<ComponentEditCommand<T>>(entity, before, after))` üretir.
Bu ADR-012 "all mutations event-sourced" kararıyla uyumludur.

Reflection: Phase 1'de runtime registration (yukarıdaki `register_drawer<T>`);
Phase 2'de `cd::ecs` reflection meta (ADR-004 Q-ECS-reflect) entegrasyonu
ile otomatik drawer üretimi araştırılır.

### 2.4 Scene graph view + transform gizmo + undo/redo

Bunların *mekaniği* ADR-012'de kararlaştırıldı; bu ADR sadece **binary
içindeki bağlamı** verir.

#### 2.4.1 Scene graph view

- Kaynak: `cd::scene::Scene` (tek authoritative tree).
- `cd::editor::HierarchyView` Parent/Children component zinciri walk eder.
- Drag-reparent → `ReparentCommand` (undoable; SceneGraph invariant
  korunur: cycle yok).
- Multi-select: Ctrl/Shift + click; `cd::editor::SelectionSet` tutar.
- Filtre/arama: name + tag prefix; sub-second cevap (HierarchyView
  zaten Phase 5'te bench'lendi).

#### 2.4.2 Transform gizmo

- `cd::editor::AxisGizmo` viewport overlay olarak çizilir (kendi render
  pass'ı, depth-test opsiyonel).
- Drag → `TransformCommand` (translate/rotate/scale).
- Space/snap/pivot ADR-012'deki `GizmoManager` üzerinden seçilir;
  Universal gizmo default.
- Hover-detect modu (T/R/S otomatik): UI'de toggle, default OFF (klasik
  T/R/S tuş seçimi); user-test sonrası flip edilebilir.

#### 2.4.3 Undo/redo (`History` panel)

- Mevcut `cd::editor::EditHistory` (128 entry / 4 MiB bütçe).
- History panel: liste, "Undo to here" + "Redo to here" linear-history;
  branching yok (Phase 3).
- Klavye: Ctrl+Z / Ctrl+Y (Ctrl+Shift+Z macOS).
- "Clear history" scene reload + project switch sırasında otomatik
  (mevcut `clear()` çağrılır).

### 2.5 Play-in-Editor (PIE)

#### 2.5.1 Hedef

Editör açıkken oyunu **aynı proses içinde** çalıştırabilmek; oyun durdurulduğunda
sahne **edit öncesi haline** dönmeli. Bu UE'nin "Play" / Unity'nin "Play" butonuna
denktir.

#### 2.5.2 State machine

```
        +----------+  Play   +----------+  Pause  +----------+
        | kEditing | ------> | kPlaying | <-----> | kPaused  |
        |          | <------ |          |         |          |
        +----------+  Stop   +----------+         +----------+
                                  ^                    |
                                  |                    | Step (advance 1 frame)
                                  +--------------------+
```

Geçişler `chroma::editor::app::PieController::request_transition(PieState)`;
illegal geçişlere `kInvalidState` döner.

#### 2.5.3 Sahne snapshot/restore

- `kPlaying` öncesi: `cd::scene::snapshot_to_buffer(scene)` → in-memory blob
  (mevcut Scene serialize altyapısı; cd_asset_json üzerinden Phase 2'de aynı
  binary path).
- `kPlaying` sırasında oyun ECS'i serbestçe mutasyona uğrar (entity oluştur,
  yok et, component değiştir).
- `Stop`: `cd::scene::restore_from_buffer(scene, blob)` → editöre dönüş;
  selection set + gizmo state korunur ama entity referansları yeniden bağlanır
  (entity ID stable değilse `chroma::editor::app::EntityIdRemap` kullanılır).

#### 2.5.4 Tick koordinasyonu

```cpp
namespace chroma::editor::app {

class PieController {
public:
    enum class State : std::uint8_t { kEditing, kPlaying, kPaused };

    struct TickInputs {
        float dt_seconds;
        cd::input::InputContext& input;
    };

    /// Called every editor frame. Forwards dt to game systems when
    /// kPlaying; returns the number of game systems ticked.
    auto tick(const TickInputs&) -> std::size_t;

    [[nodiscard]] auto state() const noexcept -> State { return state_; }

    auto request_play()  -> std::expected<void, PieError>;
    auto request_pause() -> std::expected<void, PieError>;
    auto request_stop()  -> std::expected<void, PieError>;
    auto request_step()  -> std::expected<void, PieError>;  // kPaused only

private:
    State state_ { State::kEditing };
    std::vector<std::byte> snapshot_;
    // ... game system refs, lua VM handle
};

}
```

#### 2.5.5 Tetikleyiciler

- F5: Play / Stop toggle.
- Shift+F5: Stop.
- F6: Pause.
- F10: Step (kPaused → 1 frame → kPaused).
- Toolbar: ▶ ⏸ ⏹ ⏭ butonları.

#### 2.5.6 Determinism + tehlike

- Oyun script'i (gameplay_lua) PIE sırasında `cd::script::Engine` instance'ından
  çalışır; script crash'i `std::expected<void, ScriptError>` ile yakalanır,
  editör hayatta kalır.
- Snapshot blob boyutu büyük sahneler için (>50 MB) "Play takes Xs to start"
  uyarısı; Phase 2 incremental diff snapshot araştırılır.

### 2.6 Asset hot-reload bus

#### 2.6.1 Mimari

Mevcut `cd::asset::HotReloadQueue` + `cd::asset::FileWatcher` üzerine **tek
bir merkezi pub/sub bus**:

```cpp
namespace chroma::editor::app {

enum class ReloadKind : std::uint8_t {
    kAsset, kShader, kScript, kPlugin, kProjectFile
};

struct ReloadEvent {
    ReloadKind                 kind;
    std::filesystem::path      path;
    std::uint64_t              version;     // monotonic per path
    std::chrono::steady_clock::time_point detected_at;
};

class HotReloadBus {
public:
    using Handler = std::function<void(const ReloadEvent&)>;

    /// Subscribe; returns a token. Drop the token to unsubscribe (RAII).
    [[nodiscard]] auto subscribe(ReloadKind k, Handler h) -> SubscriptionToken;

    /// Producer side. Called by FileWatcher threads.
    void publish(ReloadEvent ev);

    /// Drain queued events on the editor thread. Returns events dispatched.
    auto pump() -> std::size_t;
};

}
```

#### 2.6.2 Veri yolu

```
FileWatcher (thread)        Compiler watch (thread)       Plugin watcher
        |                            |                            |
        +-------------+--------------+----------------------------+
                      |
                      v   publish(ReloadEvent)
              HotReloadBus  (lock-free MPSC queue)
                      |
                      v   pump() on editor main thread
        +--------+--------+--------+--------+----------+
        |        |        |        |        |          |
   asset    shader   script   plugin   project file
   handler  handler  handler  handler  handler
        |        |        |        |        |
        v        v        v        v        v
   re-ingest  pipeline  Lua hot- DLL unload save/reload
   gltf/img   rebuild   reload   + reload
```

#### 2.6.3 Idempotency

- `version` monotonic; aynı path için aynı versiyon iki kez gelirse handler
  no-op.
- Aynı path için multiple back-to-back değişiklik 100 ms coalesce window'da
  birleştirilir.
- Reload başarısız olursa handler `std::expected` ile error döner;
  bus log'lar + toast gösterir, edit oturumu sürer.

#### 2.6.4 Render-thread güvenliği

- Asset reload swap'i renderer için **frame-boundary atomic**: yeni handle
  hazır olduktan sonra mevcut frame'in sonunda swap edilir
  (`cd::concurrency` triple buffer pattern). Eski GPU resource sonraki
  N=3 frame sonunda free.

### 2.7 Plugin model (Lua + native DLL)

#### 2.7.1 İki katmanlı plugin

1. **Lua extensions** (`cd::script::Engine` üzerinden) — script-only tool'lar.
   No native compilation; `Plugins/<id>/init.lua` yüklenir.
2. **Native DLL** (Phase 2) — `cd::plugin::Loader` (ADR-017) üzerinden
   `.dll` / `.so` / `.dylib`; state-preserving hot-reload.

Bu ADR Phase 1 **Lua-only** karar verir; native DLL hot-reload ADR-017'nin
P2 phase'inde detaylanır, editör binary buna **plug-ready**.

#### 2.7.2 Lua plugin sözleşmesi

`Plugins/<id>/init.lua` aşağıdaki global hook'ları tanımlayabilir:

```lua
-- Plugin manifest (returned table)
return {
  id          = "cd.plugin.example_tool",
  name        = "Example Tool",
  version     = "0.1.0",
  api         = 1,              -- editor plugin ABI version

  on_load     = function(ctx) ... end,
  on_unload   = function(ctx) ... end,

  -- UI hooks
  register_menu_items = function(menu) ... end,    -- menu = MenuBar handle
  register_panels     = function(dock) ... end,    -- dock = dock layout handle

  -- Asset pipeline hooks
  register_asset_handler = function(reg) ... end,  -- custom extensions

  -- Command palette
  register_commands  = function(palette) ... end,  -- palette = CommandPalette
}
```

`ctx` objesi C++ tarafında `cd::script::Table` olarak inşa edilir; içerir:

- `ctx.editor` — `chroma::editor::app::App` proxy (selection, scene, history).
- `ctx.ecs`    — `cd::ecs::World` query/spawn proxy.
- `ctx.assets` — `cd::asset::AssetRegistry` proxy.
- `ctx.log`    — `cd::log` çıkışı.

C++ binding: `cd::script::Engine::bind_table(...)` kullanılır (mevcut API'nin
yetmediği yerlerde header-only küçük helper, vendor-free).

#### 2.7.3 Plugin host lifecycle

```cpp
namespace chroma::editor::app {

class PluginHost {
public:
    enum class LoadError : std::uint32_t {
        kManifestMissing, kManifestInvalid, kApiMismatch, kInitFailed
    };

    [[nodiscard]] auto load(std::filesystem::path init_lua)
        -> std::expected<PluginHandle, LoadError>;

    auto unload(PluginHandle) -> std::expected<void, LoadError>;

    /// Hot-reload all plugins that point at changed files.
    /// Called by HotReloadBus handler for kPlugin events.
    auto reload_all() -> std::size_t;

    [[nodiscard]] auto loaded_plugins() const
        -> std::span<const PluginRecord>;
};

}
```

Manifest `api = 1` mismatch durumunda plugin yüklenmez; toast + log.

#### 2.7.4 Güvenlik

- Lua sandbox: `os.execute`, `io.popen`, `os.exit` editör tarafından
  override edilip blok'lanır (default-deny; project file `[plugins.allow]`
  ile explicit izin verilebilir Phase 2).
- Native DLL Phase 2'de signing zorunlu (ADR-017 P2 + ADR-014 distribution).

### 2.8 Aşma noktaları (state-of-art üstü)

1. **Hot-reload first** (ADR-012 G2 üzerine inşa) — asset + shader + script +
   plugin tek bus, idempotent version'lı, render-thread frame-atomic.
2. **Generic glTF drag-drop** — per-asset glue kod yok (ADR-20260530-generic-
   gltf-scene-loading kontratı). Sektör standardı Unity/UE içe-aktarım
   sihirbazı; biz tek sürükle-bırak.
3. **PIE event-sourced snapshot** — `cd::scene` POD snapshot + restore;
   "Play, edit while playing, Stop, edits korunur" davranışı **gelecek
   evrim** olarak açık tutulur (Phase 3).
4. **Lua-first plugin** — derleme gerektirmez; native DLL ikinci kademe;
   topluluk-uzantılı asset handler + panel + command.
5. **Project file forward-compat** — bilinmeyen alan korunur; minor schema
   bump eski proje açar.
6. **Tek namespace `chroma::editor::app::`** — engine library'ler hiç
   etkilenmez (DAG temiz).
7. **Editor binary cold start ≤2 s** — projeyi parse → asset index cache
   load → opening scene lazy ingest → ilk frame'i göster.

---

## 3. Reddedilen alternatifler

1. **Editor'ü oyun binary'sine gömmek (UE-vari `-game` flag).** Production
   binary'sinde editör kodu güvenlik + boyut + lisans riski; library-oriented
   vizyona aykırı. Reddedildi.

2. **Editor'ü web/electron tabanlı yapmak (Tauri, CEF).** Web ön-yüz +
   native engine süreç IPC overhead'i + JS bağımlılığı + brand-able UI
   `cd::ui` ailesini by-pass eder. Reddedildi.

3. **Project file = `.json` (TOML yerine).** JSON insan-yazımı için ağır
   (trailing comma yok, yorum yok); merge-conflict TOML'da daha okunaklı.
   JSON fallback şema-eşdeğer parser olarak korunur. TOML birinci sınıf.

4. **Project file = SQLite veritabanı (UE `.uproject` + DDC paterni).**
   Diff-ability ve text-merge kaybedilir. Asset cache için `.cdcache/` ayrı
   binary; project dosyası daima text. Reddedildi.

5. **Asset browser drag-drop'u hello_engine'de prototiplemek.** hello_engine
   bir editör değil; per-asset glue sample stillinde kalır. Drag-drop binary
   içinde ait. Reddedildi.

6. **PIE = ayrı proses (UE `-PlayInStandalone`).** Editör ↔ oyun IPC v1'de
   ağır; aynı proses içi snapshot/restore yeterli. Multi-process PIE
   Phase 3'te (ADR-012 G4 IPC-ready edit log üzerine).

7. **Plugin = Python (Blender pattern).** Embedded Python ABI + GIL +
   bağımlılık zinciri (CPython 3.x + pip ekosistem) MIT-only vendor
   matrisini şişirir. Lua 5.4 zaten engine'de; tek VM.

8. **Native DLL plugin Phase 1.** Hot-reload + state-preserving DLL
   ABI çözümü ADR-017 P2 ile birlikte gelecek; Phase 1'de Lua yeterli.

9. **Project file alanlarını TOML root'una serpmek (flat).** İç-içe tablo
   (`[paths]`, `[editor]`, `[runtime]`) okunabilirlik için önemli; flat
   yapısı 30+ key sonrası karmaşık. Reddedildi.

10. **Asset handler tablosunu binary'de hard-code etmek.** Plugin registry
    (`register_asset_handler`) gelecek-uyumlu; built-in handler'lar registry'ye
    ilk önyükleme sırasında push edilir, plugin'ler ekleyebilir.

---

## 4. Sonuçlar (Consequences)

### 4.1 Olumlu

- Kullanıcı çift-tıklar `.cdproj` → editör açılır → drag-drop ile içerik ekler.
  "Editor binary yok" sürtünmesi yok olur.
- hello_engine sample'ı saf-engine demo olarak kalır (per-asset glue zaten
  generic glTF ADR'siyle eridi); editör senaryosu için ayrı yüzey vardır.
- Tüm editör mutasyonları `EditHistory` üzerinden geçer; undo/redo bütünlüğü
  drag-drop dahil herşeyi kapsar.
- Hot-reload bus dağınık FileWatcher subscription'larını tek noktada toplar;
  yeni handler eklemek `subscribe()` + `publish()` ile sabit-zaman.
- Plugin Lua tarafı topluluğa açık genişleme yüzeyi; brand-able tool'lar
  + asset handler + panel + command zenginleşir.
- PIE in-process snapshot-restore "play to test" döngüsünü saniye-altı
  yapar (UE seviyesinde "Play" sürtünmesinden iyi).

### 4.2 Olumsuz / migration yükü

- Yeni binary target = yeni CI matrisi (Windows + Linux + macOS x3 backend).
- `.cdproj` parser + writer + round-trip test yeni kod; tahmini ~1.5 KLOC
  inc. test.
- Plugin host + sandbox + manifest validation ~1 KLOC.
- Asset browser + thumbnail cache + drag payload routing ~2 KLOC.
- PIE controller + snapshot/restore wiring ~800 LOC; snapshot için
  `cd::scene` POD serialize tamamlanmamışsa onun da bir alt-PR'i.
- Hot-reload bus ~400 LOC; mevcut HotReloadQueue üzerine ince katman.
- Toplam editor binary ilk-sürüm ~6 KLOC + ~1.2 KLOC test (Phase 2,
  4-6 hafta tek-developer).

### 4.3 Geri uyumluluk

- Engine library'lerin public API'si **değişmez**; editor binary yalnız
  tüketicidir. Mevcut hello_engine + diğer sample'lar etkilenmez.
- ADR-012'deki mekanikler aynen geçerli; bu ADR onları paketler.
- `.cdproj` v1 schema future minor bump'lar bilinmeyen alanları korur
  (forward-compat).

### 4.4 Riskler

- **Snapshot blob boyutu**: büyük sahnede (50k+ entity) "Play" gecikmesi
  fark edilir; mitigation Phase 2 incremental diff.
- **Lua sandbox**: `os.execute` engellense de `require` dosya I/O hâlâ
  yapabilir; full sandbox Phase 2 (allowlist-based).
- **Plugin ABI versiyonlama**: `api = 1` minor bump editör versiyonuyla
  birlikte yönetilmeli; CHANGELOG + migration guide gerek.
- **Cross-platform native dialog**: Windows IFileDialog + macOS NSOpenPanel
  + Linux portal/zenity; `cd::platform::native_dialog` Phase 1'de sadece
  Windows; Linux/macOS fallback `cd::ui::FileBrowser` (intra-window).
- **Quad-viewport VRAM**: ADR-012 quad-view 4× RT — editör binary'de
  varsayılan SINGLE; QUAD opt-in (project preferences).
- **Asset cache invalidation**: `.cdcache/asset_index.cdadb` corrupt olursa
  rebuild path açık olmalı; CI test eder.

### 4.5 Replace-Ready (D1)

- TOML parser (vendor-free veya MIT vendor); değiştirilebilir.
- Native dialog her platformda `cd::platform` arkasında; değiştirilebilir.
- Lua hot-replaceable çünkü `cd::script::Engine` opaque.

---

## 5. Açık sorular

| ID | Soru | Şu anki yanıt |
|---|---|---|
| Q1 | TOML parser vendor mı (toml++) yoksa el yapımı mı? | Phase 2 implementor seçer; vendor MIT/BSL ise OK. |
| Q2 | `.cdproj` Git LFS gerekli mi? | Hayır — text, küçük; LFS asset binary'leri için (cdmesh/cdtex/ktx2). |
| Q3 | PIE snapshot incremental diff ne zaman? | Phase 3 (50k+ entity sahne ölçütü tetikleyince). |
| Q4 | Plugin marketplace? | Phase 3+ (ADR-014 distribution iç). |
| Q5 | Native DLL plugin SDK header'ı | Phase 2 (ADR-017 P2 ile birlikte). |
| Q6 | Multi-document editing (aynı anda 2 sahne)? | Phase 2 — `App` tek-document Phase 1; tab-based doc shell Phase 2. |
| Q7 | Editor headless mode (CI'da `--no-window`)? | Evet — `chroma::editor::app::App::run_headless()` Phase 2. |
| Q8 | Project upgrade wizard (v1 → v2 schema)? | Phase 2 — `chroma::editor::app::migrate_project_file()`. |
| Q9 | Asset browser remote (network drive) latency? | Phase 1 sync; Phase 2 background thread + spinner. |
| Q10 | Editor scripting record-and-replay (macro)? | Phase 3 — `EditHistory` event log zaten dump'lanabilir; replay yeni ekleme. |

---

## 6. Cross-cutting referanslar

- **ADR-009 (UI Architecture)** — dual-runtime kararı; editor binary retained
  yan; `cd::ui::*` Phase 1+2 widget kütüphane ailesi (ADR-20260530-ui-widget-
  library) editor binary'nin görsel yüzeyi.
- **ADR-012 (Editor Mechanics)** — gizmo, snap, level editing, ProBuilder,
  spline, terrain, viewport. Bu ADR onları **binary'e paketler**.
- **ADR-006 (Asset Pipeline)** — drag-drop ingest yolu asset loader tier
  + AsyncStreamer + HotReloadQueue üzerine kuruludur.
- **ADR-20260530-generic-gltf-scene-loading** — drag-drop glTF kontratı
  (load_scene + ingest_gltf_scene) editor'ün **doğrudan** kullandığı API.
- **ADR-004 (ECS-Scene)** — Hierarchy view, transform commands, reparent
  ECS Parent/Children component'larına bağlıdır.
- **ADR-015 (Concurrency Job System)** — FileWatcher + thumbnail render +
  asset reload arka-thread işleri job system üzerinden.
- **ADR-017 (DtForHil Pattern Salvage / Plugin DLL)** — native plugin hot-
  reload Phase 2; bu ADR Lua tarafını netleştirir.
- **ADR-005 (Foundation Policy)** — `std::expected` + `noexcept` + `[[nodiscard]]`
  policy editor binary'sinde de zorunlu.
- **ADR-014 (CI/CD Distribution)** — `.cdproj` association installer,
  cd_editor_app code-signing, plugin marketplace Phase 3.
- **ADR-20260530-gameplay-library-family** — PIE çalıştırılan oyun script'leri
  + gameplay modülleri editor binary'sinin "Play" hedefidir.

---

## 7. Kanıt + referans (engineering)

- Unreal Editor architecture: docs.unrealengine.com/5.0/en-US/unreal-editor-interface
- Unity Editor extensibility: docs.unity3d.com/Manual/extending-the-editor.html
- Godot Editor plugin API: docs.godotengine.org/en/stable/tutorials/plugins/editor/
- TOML 1.0 specification: toml.io/en/v1.0.0
- Blender "Play" + state preservation: docs.blender.org/manual/en/latest/animation/
- ImGui dock-space (reference for layout serialization): github.com/ocornut/imgui/wiki/Docking
- Live++ — state-preserving native hot-reload: liveplusplus.tech
- Erich Gamma et al., "Design Patterns" (Command pattern for undo/redo).
- Robert C. Martin, "Clean Architecture" (dependency direction = editor → libs).
- Iglberger, K. "C++ Software Design" (ADR template + dependency inversion).

---

## 8. Phase 1 deliverable scope (this ADR only)

This document is **design only**. No code lands with this ADR. The implementation
plan below is informational, to be opened as separate ADRs / PRs when scheduled:

| Phase | Deliverable                                              | Effort |
|-------|----------------------------------------------------------|--------|
| 1     | This ADR (design accepted, dependencies confirmed).      | 0.5 d  |
| 2     | `apps/editor/` skeleton + `.cdproj` parser + tests.      | 1 wk   |
| 3     | Asset browser + drag-drop glTF routing.                  | 1 wk   |
| 4     | Inspector drawer registry + selection wiring.            | 0.5 wk |
| 5     | PIE controller + snapshot/restore.                       | 1 wk   |
| 6     | HotReloadBus consolidation + render-thread frame-atomic. | 0.5 wk |
| 7     | PluginHost (Lua) + manifest + sandbox.                   | 1 wk   |
| 8     | Cross-platform native dialog (Windows first).            | 0.5 wk |
| 9     | Installer (.cdproj OS association) + code signing.       | 0.5 wk |
| 10    | Documentation: editor user guide, plugin SDK guide.      | 1 wk   |

**Total**: ~6.5 weeks, 1 senior developer; parallelizable to ~4 weeks with
2 developers (asset browser + PIE in parallel after Phase 2).
