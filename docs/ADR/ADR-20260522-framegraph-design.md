# ADR-20260522 — cd::framegraph design (post-hoc validation)

## Bağlam

CHROMODYNAMIC'in render tier'i `cd::render::Renderer` üzerinden temel
swapchain begin/end + ICommandBuffer recording sağlıyor. Çok-pass render
(shadow, gbuffer, lighting, post) ve transient resource aliasing için
declarative bir orkestrasyon katmanı gerekiyordu. Bu oturumda yapılan
SOTA taraması (`research/notes/framegraph_sota.md`) Frostbite (GDC 2017),
Granite (Themaister), UE RDG, AMD RPS, skaarj1989/FrameGraph, bgfx,
The Forge ve Sokol'u inceledi.

Mevcut `cd::framegraph` implementasyonu 10 birim testi ile çalışıyor.
Bu ADR mevcut tasarımı SOTA önerilerine göre değerlendirir ve gelecek
sprint roadmap'i belirler.

## Karar (mevcut tasarımın değerlendirilmesi)

| Eksen | Mevcut tasarım | SOTA önerisi | Uyum |
|---|---|---|---|
| Pass deklarasyonu | `add_pass(PassDesc{name, reads, writes, execute})` | Lambda pair (setup + execute), Frostbite-style | ✅ Eşleşir (PassDesc + std::function execute) |
| Resource handle | `ResourceHandle { uint32_t index, generation }` opaque | Typed integer handle | ✅ Eşleşir |
| Transient vs imported | İki ayrı API: `create_texture` vs `import_texture` | Aynı | ✅ Eşleşir |
| Compilation | Per-frame `compile()` + `execute()` | Frostbite per-frame, Granite bake+reset | Mevcut tasarım Frostbite modeli — ✅ kabul |
| Pass sırası | Registration order, no DAG | Topological sort + dead-code elimination | ⚠️ v2 hedefi |
| Aliasing | Yok (her transient kendi VkImage'i) | Conservative interval-based | ⚠️ v2 hedefi |
| Barrier inference | Per-pass `reads.state` / `writes.state` declared | Semi-explicit (access type → stage) | ✅ Eşleşir |
| Async compute | Yok | Opt-in pass flag + automatic fence | ⚠️ v2 hedefi |
| Debug | Pass + resource counts | DOT export + ImGui timeline | ⚠️ v2 hedefi |

## Reddedilen alternatifler (SOTA tarama detayı)

* **bgfx model (view ID + sort key, no graph):** Resource dependency
  tracking ve transient aliasing değerini kaybeder. Multi-pass post
  pipeline elden yazılır. Engine'in "general-purpose" vizyonuna ters.
* **The Forge model (manual `cmdResourceBarrier`):** Programmer her
  state transition'ı el ile koyar. Hata yüzeyi büyük; bizim ICommandBuffer
  zaten barrier API'sini açıyor ama bunu OTOMATİK olarak çıkarmak istiyoruz.
* **AMD RPSL DSL:** HLSL superset bir scripting dili tanıtmak engine'in
  C++23 monoglot tutarlılığına ters; sadece RPS SDK'sine değer için
  ekstra bir derleyici dependency çekmek değer/maliyet eşiğini geçmiyor.

## Sonraki (v2 roadmap)

1. **Topological sort + DCE** (W4.4 — gelecek sprint): backbuffer
   resource'tan reachability BFS, ulaşılamayan pass'leri drop et.
2. **Transient aliasing** (W4.5): VkMemoryRequirements interval-based
   bin-packing, 256 MB heap blokları.
3. **Async compute opt-in** (W4.6): `PassFlags::AsyncCompute` + auto
   semaphore insertion.
4. **DOT export** (W4.7): `fg.export_dot(ostream&)` → Graphviz dump.

## Sonuçlar

* Mevcut MVP framegraph sample (`hello_framegraph`) tek-pass demo olarak
  çalışıyor; çok-pass demo v2 sonrası gelecek (W4.5 sample = "render to
  texture + post effect", aliasing ve barrier inference'ı somutlaştırır).
* SOTA report `research/notes/framegraph_sota.md`'de tüm tasarım
  gerekçesi ve karşılaştırmalı analiz mevcut.

## Açık sorular

* Granite bake+reset modeline geçiş v2'de mantıklı mı? Per-frame rebuild
  basit ama O(N log N) compile cost amortize edilmiyor. Editor için
  per-frame win, runtime için baked win.
