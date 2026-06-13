# ADR-20260613-editor-panel-fold-strategy

## Bağlam

samples ≤10 endgame hedefi çerçevesinde `hello_animator`, `hello_behavior_designer` ve
`hello_material_editor` üç ayrı sample binary olarak çalışmaktadır.  Her biri aynı
editor-panel kütüphanesini görsel olarak sergiler.  Bu üçünün silinip panellerin
`hello_editor`'a katlanması, sample sayısını 3 azaltır.

Katlama öncesi iki kritik soruya yanıt bulmak gerekir:

1. Bu paneller `cd::ui::renderer::DrawBatcher` üzerinden mi çiziliyor (cd::ui
   bağımlılığı gerektirir, ImGui ile doğrudan uyumsuz), yoksa ImGui üzerinden mi?
2. `hello_ui`'deki `kSubmitterPipelineReady = false` gate'i bu panelleri etkiliyor mu?

### Kanıt 1 — Panel draw imzaları (doğrudan .hpp okuma)

Üç panel sınıfının tek çizim metodu, draw imzası aynı formattadır ve sadece
`cd::ui::renderer::DrawBatcher` alır:

```
// Animator.hpp:125
void draw(cd::ui::renderer::DrawBatcher& batcher,
          const cd::ui::widgets::Theme&  theme,
          const cd::ui::widgets::Rect&   bounds) const;

// BehaviorDesigner.hpp:141
void draw(cd::ui::renderer::DrawBatcher& batcher,
          const cd::ui::widgets::Theme&  theme,
          const cd::ui::widgets::Rect&   bounds) const;

// MaterialEditor.hpp:100
void draw(cd::ui::renderer::DrawBatcher& batcher,
          const cd::ui::widgets::Theme&  theme,
          const cd::ui::widgets::Rect&   bounds) const;
```

Üç dosyada da `#include <imgui.h>` yoktur.  Bu paneller ImGui üzerinden çizmez;
yalnızca `cd::ui::renderer::DrawBatcher` quad komutları üretir.

### Kanıt 2 — Mevcut hello_editor host deseni

`hello_editor/main.cpp` ImGui tabanlıdır (`cd::imgui::Context` + `imgui.h`).
`panel_inspector` ve `panel_console` çift-arabirimlidir:

- `inspector_panel.draw_imgui(scene, history, rot_slider_deg, rot_slider_entity)`
  → ImGui çizim yolu, hello_editor'da kullanılan budur (main.cpp satır 2088).
- `inspector_panel.draw(batcher, theme, bounds)`
  → DrawBatcher çizim yolu, hello_animator / hello_material_editor ile aynı ailedir.

`console_panel` ise doğrudan ImGui primitive'leriyle host edilmektedir (main.cpp
satır 2148-2152, `ImGui::TextUnformatted`).  Kütüphanenin `draw()` metodu da
DrawBatcher alır; hello_editor bu metodu hiç çağırmaz.

### Kanıt 3 — kSubmitterPipelineReady gate'i

`samples/ui/hello_ui/main.cpp` satır 577-578:

```cpp
constexpr bool kSubmitterPipelineReady = false;
if constexpr (kSubmitterPipelineReady)
{
    submitter.record(cmd, frame.extent);
}
```

Bu gate yalnızca `hello_ui` sample'ına özgüdür.  `hello_animator`,
`hello_behavior_designer` ve `hello_material_editor` kendi Submitter pipeline'larını
`Submitter::create_with_inline_shader()` (Route B) ile **başarıyla** oluşturmaktadır.
Üç sample gatesize çalışır; `kSubmitterPipelineReady` onları etkilemez.

### Kanıt 4 — DrawBatcher → Submitter pipeline durumu

`hello_animator` / `hello_behavior_designer` / `hello_material_editor` şu sırayla
çalışır:

```
batcher.begin_frame()
panel.draw(batcher, theme, full_rect)
submitter.upload(batcher)
// begin_render_pass
submitter.record(cmd, extent)
// end_render_pass
renderer.end_frame()
```

Route B Submitter (`create_with_inline_shader`) inline GLSL pipeline kullanır ve
ayrı bir on-disk shader veya pipeline-ready gate gerektirmez.  Üç sample CI'da
headless 60 frame koşturulabilmektedir.

### Yapısal sorun: hello_editor DrawBatcher Submitter içermez

`hello_editor/CMakeLists.txt` şu bağımlılıkları listeler:

```cmake
cd::editor_panel_inspector
cd::editor_panel_console
cd::imgui_backend
```

`cd::ui_renderer_rhi` (Submitter içeren kütüphane) ve `cd::ui_renderer`
(DrawBatcher) **listelenmiyor**.  hello_editor, ImGui'ye dönüştürülmüş paneller
dışında hiçbir DrawBatcher→GPU yolu çalıştırmaz.

Dolayısıyla üç paneli hello_editor'a katlamak için iki seçenek mevcuttur:

- **Seçenek A** — her panel için ayrı bir ImGui penceresi açıp `panel.draw(batcher, ...)` 
  çağrısını DrawBatcher→Submitter zinciri üzerinden ImGui'nin kendi render pass'ine
  bağlamak; DrawBatcher görüntüsünü texture'a render edip bir `ImGui::Image()`
  widget'ına beslemek.  Bu yol çalışır ancak önemli altyapı gerektirir: offscreen
  render target, texture blit, Submitter bağımlılığı, per-frame descriptor güncelleme.
  
- **Seçenek B** — üç panel için doğrudan ImGui API'si ile eşdeğer widget mantığı
  yazmak (panel mantığını ImGui çerçevesinde yeniden uygulamak), bağımsız panellerin
  DrawBatcher draw'ını sarmak yerine.  Bu yol panel kütüphanelerinin mevcut imzasını
  değiştirmez; hello_editor tarafında yeni helper fonksiyonlar yazılır.

- **Seçenek C** — ertelemek; üç panel hello_editor'a katlanmaz, ayrı sample olarak
  kalır.

## Karar

**Her panel için seçenek bazında farklı verdict:**

### panel_material_editor → Seçenek B (doğrudan ImGui host, ~40 satır)

Material Editor panelinin state'i (material_id, base_color rgb, metallic, roughness)
minimal ve flat yapıdadır.  Eşdeğer ImGui görünümü beş `ImGui::DragFloat`/
`ImGui::ColorEdit3` widget'ı ile yazılır.  Panel kütüphanesi bağımlılığı olarak
hello_editor'a eklenir; panel nesnesi state taşır, görsel mantık ImGui chunk içinde
inline olarak uygulanır.

```
// host iskeleti (developer kontra referansı)
ImGui::Begin("Material Editor");
{
    cd::editor::panel::material_editor::MaterialEditor& me = g_material_editor;
    ImGui::ColorEdit3("Base Color",
        std::array<float,3>{me.base_color_r(), me.base_color_g(),
                            me.base_color_b()}.data());
    // → set_base_color(...) ile geri yaz
    float metallic = me.metallic();
    if (ImGui::SliderFloat("Metallic", &metallic, 0.0F, 1.0F))
        me.set_metallic(metallic);
    float roughness = me.roughness();
    if (ImGui::SliderFloat("Roughness", &roughness, 0.0F, 1.0F))
        me.set_roughness(roughness);
}
ImGui::End();
```

CMakeLists'e `cd::editor_panel_material_editor` eklenir.

### panel_animator → Seçenek B (doğrudan ImGui host, ~60 satır)

Animator state'i (clip list, active_clip, time, duration, playing, looping)
ImGui Listbox + SliderFloat + Checkbox kombinasyonu ile karşılanır.  Clip browser
için `ImGui::ListBox`, timeline scrubber için `ImGui::SliderFloat`, play/loop için
`ImGui::Checkbox` yeterlidir.  Görsel fidelity, tek-window DrawBatcher
versiyonundan farklı olacak; ancak endgame hedefi sample sayısı azaltmaktır,
piksel-tam eşleşme değildir.

CMakeLists'e `cd::editor_panel_animator` eklenir.

### panel_behavior_designer → Seçenek C (ERTELEME)

BehaviorDesigner, node graph layout algoritması (`measure_subtree`, `draw_real_tree`,
`draw_demo_nodes`) ile ortogonal bağlantı kenarları çizmektedir.  Bu görsel mantığı
sadece ImGui primitive'leri ile yeniden uygulamak önemsiz değildir (ImGui DrawList
ile yapılabilir, ancak ~200 satır ekstra geliştirme gerektirir) ve hello_editor'ın
mevcut scope'u bu işi haklı kılmaz.  Ayrıca `cd::game::ai_bt::BehaviorTree`
bağımlılığı hello_editor'a yeni bir kütüphane bağımlılığı ekler.

Verdict: `hello_behavior_designer` ayrı sample olarak kalır.  Sample sayısı
etkisi: 3 → 2 silme (animator + material_editor), 1 kalır (behavior_designer).

## Reddedilen alternatifler

### Seçenek A — DrawBatcher → offscreen texture → ImGui::Image

Her panel için offscreen render target, per-frame Submitter yükü ve
descriptor-per-frame güncelleme gerekirdi.  DrawBatcher çiziminin texture'a
blit edilmesi, BGRA8 swapchain formatı ile eşleşen bir attachment format seçimi
ve ImGui backend'in descriptor güncelleme protokolü (Vulkan: `VkDescriptorSet` per
frame-in-flight) gerektirir.  Bu altyapı hello_editor'a ~400 satır + yeni
kütüphane bağımlılıkları eklerdi ve samples ≤10 endgame için orantısızdır.

### Panellere `draw_imgui()` metodu ekleme (panel kütüphanesi genişletme)

Inspector'ın `draw_imgui()` çift-arabirim modelini üç panele de uygulamak
mimari olarak tutarlıdır, ancak bu her panel için ~80-120 satır `.cpp` değişikliği
ve `cd::imgui_backend` bağımlılığı eklenmesi demektir.  ADR yetki kapsamı tasarım
kontratı + builder reçetesi; o `.cpp` değişiklikleri Developer'a aittir.
Eğer `panel_material_editor::MaterialEditor::draw_imgui()` yolu ileride gerekli
görülürse bu ADR'ye bir not düşülmeli ve panel interface'i buna göre genişletilmelidir.

## Sonuçlar

### Developer'a katlama reçetesi

#### Adım 1 — panel_material_editor katlaması

1. `samples/editor/hello_editor/CMakeLists.txt` DEPS listesine `cd::editor_panel_material_editor` ekle.
2. `hello_editor/main.cpp` içinde `#include <cd/editor/panel_material_editor/MaterialEditor.hpp>` ekle.
3. İlklendirme bölümünde `cd::editor::panel::material_editor::MaterialEditor g_mat_editor;` tanımla ve
   istenen başlangıç state'ini `set_material_id` / `set_base_color` / `set_metallic` / `set_roughness`
   ile seed et.
4. ImGui frame içinde yeni bir `ImGui::Begin("Material Editor")` / `ImGui::End()` bloğu aç;
   `ColorEdit3`, `SliderFloat` widget'larını panel state'i okuyup yazacak şekilde bağla.
5. DockBuilder `ImGui::DockBuilderSplitNode` çağrılarına yeni dock slot ekle veya mevcut
   Inspector paneline tab olarak dock et.
6. `samples/editor/hello_material_editor/` dizinini ve CMakeLists referansını sil.

#### Adım 2 — panel_animator katlaması

1. `cd::editor_panel_animator` DEPS'e ekle.
2. `#include <cd/editor/panel_animator/Animator.hpp>` ekle.
3. `cd::editor::panel::animator::Animator g_animator;` tanımla, clip seed et.
4. `ImGui::Begin("Animator")` bloğu: `ImGui::ListBox` clip listesi, `ImGui::SliderFloat`
   timeline, `ImGui::Checkbox` play/loop.
5. `samples/editor/hello_animator/` sil.

#### Adım 3 — hello_behavior_designer erteleme

`hello_behavior_designer` binary'ye dokunma.  docs/SAMPLES_CONSOLIDATION_PLAN.md
NEEDS-PORT tablosundaki satırı DEFERRED + rationale ile güncelle.

### Doğrulama planı

- `cmake --build --preset ninja-debug` temiz geçmeli.
- `ctest --preset ninja-debug --output-on-failure` 260+ test PASS.
- hello_editor çalıştırıldığında "Material Editor" ve "Animator" pencerelerinin
  Material Editor'da ColorEdit3 preview, Animator'da clip listesi ile görünmesi.
- `hello_material_editor` ve `hello_animator` binary'lerinin artık build grafiğinde
  yer almaması (CMakeLists silindiğinde cmake configure uyarı vermemeli).

### Etkilenen modüller

| Dosya | Değişiklik türü |
|-------|-----------------|
| `samples/editor/hello_editor/CMakeLists.txt` | DEPS: +panel_material_editor +panel_animator |
| `samples/editor/hello_editor/main.cpp` | ImGui pencere ekleme (~100 satır) |
| `samples/editor/hello_material_editor/` | Silinir |
| `samples/editor/hello_animator/` | Silinir |
| `samples/editor/hello_behavior_designer/` | Değiştirilmez (DEFERRED) |
| `docs/SAMPLES_CONSOLIDATION_PLAN.md` | NEEDS-PORT tablosu güncelleme |

### Sample sayısı etkisi

Bu ADR'nin tüm kararları uygulandığında: 38 sample → 36 sample (−2).
`hello_behavior_designer` ertelemesi nedeniyle −3 hedefine ulaşılamaz.
