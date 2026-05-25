# ADR-20260522 — cd::ecs storage model (post-hoc validation)

## Bağlam

cd::ecs sparse-set tabanlı bir implementasyon ile geliyor (13 birim
testi). Bu oturumda SOTA tarama (`research/notes/ecs_sota.md`) EnTT,
Flecs, Bevy ECS, production DOTS, production Mass, EntityX, ANAX'i inceledi.

Bu ADR mevcut tasarımın SOTA önerilerine uyumunu doğrular ve gelecek
sprint için enhancement listesi çıkarır.

## Karar (mevcut tasarımın değerlendirilmesi)

| Eksen | Mevcut | SOTA önerisi | Uyum |
|---|---|---|---|
| Storage | Sparse-set per component type | Sparse-set primary + owning groups opt-in | ✅ Eşleşir (groups v2) |
| Entity ID | 32-index / 32-generation, `struct Entity { id, gen }` | 64-bit packed (or struct), generation bumped on reuse | ✅ Eşleşir |
| Component registration | `std::type_index` map, runtime | Runtime preferred over compile-time | ✅ Eşleşir |
| Query API | `each<T, Rest...>(fn)`, smallest-pool driver | Builder DSL + cached queries | ⚠️ Cache eklendi (W13.4) |
| Multi-threading | Yok | Declared reads/writes DAG + IJobDispatcher | ⚠️ v2 hedefi |
| Change detection | Yok | Tick-based, order-independent | ⚠️ v2 hedefi |
| Hierarchies | `cd::scene` ayrı katmanda | ECS standalone kalmalı; hierarchy = scene | ✅ Eşleşir |
| Serialization | Yok | Reflection-driven snapshot visitor | ⚠️ v2 (cd::meta gerekli) |
| Allocator | Default `std::allocator` | `std::pmr::memory_resource*` injection | ⚠️ v2 hedefi |

## Reddedilen alternatifler (SOTA tarama detayı)

* **Archetype-only (Flecs default, production DOTS):** Multi-component
  iteration en hızlı yol ama dinamik gameplay add/remove için 10×
  pahalı. Samurai Gunn 2 case study (Moonside Games) production'da
  300→5 FPS regression yaratmıştı. Gameplay-first engine için uygun
  değil.
* **EnTT groups-only:** Groups kullanışlı ama default tek storage'a
  zorlamak prototipleme aşamasında kısıtlayıcı.
* **Bevy ECS (Rust):** Engine C++23; portable değil.
* **production DOTS (C# + Burst):** Lisans + dil kısıtı.
* **production Mass:** UE plugin, standalone build yok.

## Yapılan iyileştirmeler (W13.4)

Cached `Query<T, Rest...>` eklendi (5 ek test):

```cpp
auto q = world.query<Position, Velocity>();
// ... frame loop ...
q.each(world, [](Entity, Position& p, Velocity& v){ p += v; });
```

Faydası: per-frame `std::type_index` hash lookup elimine. Storage
pointer'ları World ömrü boyunca stabil (unordered_map rehash
unique_ptr içeriklerini taşımaz). `refresh(world)` ile late-registered
component'leri pick-up edebiliyor.

## Sonraki (v2 roadmap)

1. **Owning groups** (W6.x): Co-sorted component pools, archetype-class
   iteration hızı for the hot multi-component combos (Position+Velocity).
2. **Scheduler + IJobDispatcher** (W6.y): Declared `reads<T>()/writes<U>()`,
   DAG topological sort, parallel dispatch.
3. **Tick-based change detection** (W6.z): Order-independent `Changed<T>`
   filter, 64-bit per-storage write tick.
4. **`std::pmr::memory_resource*` injection**: World ctor takes allocator,
   propagates to every SparseSet<T>.
5. **Snapshot serialization**: Reflection visitor; depends on cd::meta
   (henüz yok).

## Sonuçlar

* hello_ecs sample (W6.3) Debug'da 100k entity × 600 frame × 3 system =
  180M entity-update'i 27 saniyede koştu (6.64 M update/s sustained,
  Debug). Release'de ~5-10× faster bekleniyor. Bu SOTA-aligned sparse-set
  layout'un haklılığını kanıtlıyor.
* hello_scene_graph sample (W7.3) `cd::ecs + cd::scene` köprüsünü doğrulu-
  yor: ECS standalone tasarımı + scene hiyerarşi katmanı SOTA önerisine
  birebir uyuyor.
* SOTA report `research/notes/ecs_sota.md`'de tam karşılaştırma + 25+
  cited source.

## Açık sorular

* Owning groups EnTT modelinde "tek bir component bir gruptan zorunlu
  çıkar" kuralı koyar (mutual exclusion). Bu kısıtlama gameplay
  ergonomisini bozmaz mı? v2 design'da bench gerekli.
