# ADR-20260611 — cd::debug_line / cd::debug_draw kütüphane çifti

**Durum**: Accepted (implementasyon phases 1030-1060'ta landed;
ADR geriye dönük kayıt — golden-fixture-agent-iteration-loop ADR'i
gibi codification niteliğinde).

## Bağlam

Marathon Run 27-29, hello_engine'e 19 adet 3D viewport debug demosu
ekledi ve hepsi "sphere-at-position" proxy'siyle çizildi; motorun
line-list yolu yoktu. docs/RESEARCH_3D_VIEWPORT_DEBUG_VIZ.md (§12)
SOTA karşılığını tanımlar: bgfx `DebugDrawEncoder`, Bevy `Gizmos` —
her ikisi de immediate-mode shape API + retained per-frame batch.

hello_engine phase 1031/1034'te GPU tarafını inline prototipledi.
Phase 1034 adversarial review'u prototipte gerçek bir GPU
use-after-free yakaladı: kapasitesi aşılan vertex buffer'ın frame
ortasında `destroy_buffer` edilmesi, frame N-1 hâlâ okurken handle'ı
öldürüyor (fif=2 yalnız N-2'yi fence'ler). Düzeltme "outgrown buffer
frame_idx+3'e kadar park edilir" politikasıydı (TLAS-ring
konvansiyonu). Bu politika tüketici başına elle yazılırsa her yeni
tüketici aynı hatayı yeniden üretebilir.

## Karar

İki kütüphaneli ayrım:

1. **cd::debug_line** (INTERFACE; deps: core+math): RHI-bağımsız CPU
   batch. `LineVertex{Vec3f pos, Vec4f rgba}` (28 B packed) +
   `LineBatch` accumulator: line / aabb (12 edge, swapped-corner
   safe) / obb (decal right-up-forward ekseni) / frustum (inv-VP,
   Vulkan [0,1] default + GL override) / circle (≥3 seg clamp,
   zero-axis fallback) / polyline / cross / sphere (3 great circle) /
   arrow (4-wing head) / grid. GPU'suz birim-test edilebilir
   (modülerlik kuralı).
2. **cd::debug_draw** (STATIC; deps: debug_line+material+rhi+shader):
   GPU yarısı. `Renderer`: kLineList Material (depth test ON / write
   OFF, cull none, mat4 view-proj push), lazily-grown kCpuToGpu VB,
   **park-margin politikası tek yerde** (`kDestroyMargin = 3`,
   compile-time test pin'li), pass başına tek flush,
   `recreate_pipeline` ile X5 hot-reload (başarısız edit eski
   pipeline'ı korur).

Shader sözleşmesi: embedded default'lar tek color attachment yazar;
MRT pass'li tüketici `fragment_glsl` override'ı verir (hello_engine
4-target HDR pass'i nötr G-buffer yazan FS sağlar). On-disk path'ler
ADR-20260529-X5 önceliğine uyar.

## Reddedilen alternatifler

- **Tek kütüphane (CPU+GPU birlikte)**: batch'in testi GPU'ya
  bağlanırdı; headless tüketiciler (test, tool) CPU yarısını
  kullanamazdı. Ayrım bgfx/Bevy ile de uyumlu.
- **Tüketici başına inline GPU kodu** (prototip durumu): phase-1034
  bulgusunun gösterdiği gibi lifetime politikası kopyalanan her
  yerde yeniden yanlış yazılabilir.
- **ImGui draw-list ile 3D çizim**: screen-space'tir; depth-test'li
  occlusion ve world-space sabitlik gerektiren overlay'ler için
  yanlış katman.
- **Per-frame VB yeniden yaratma (grow yerine)**: allocation churn +
  aynı lifetime sorunu; park+reuse hem güvenli hem ucuz.

## Sonuçlar

- 34 hello_engine overlay'i + hello_editor grid/selection/gizmo aynı
  iki kütüphane üzerinden akıyor; yeni tüketici maliyeti ≈ 10 satır.
- Use-after-free sınıfı yapısal olarak kapandı (politika + pin testi).
- Editör çizgi-tabanlı manipülasyon araçlarının (translate gizmo,
  phase 1063-1064) temelini bu çift sağladı.
- Gelecek genişlemeler: screen-space/HUD modu, kalınlık (quad-expand)
  ve text label'lar ayrı kararlar olarak ele alınmalı — bu ADR yalnız
  1-px world-space line sözleşmesini kapsar.
