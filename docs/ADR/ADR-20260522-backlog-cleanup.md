# ADR-20260522 — Phase 1-4 backlog cleanup

## Bağlam

S4.4'ten sonra önceki fazların "Açık sorular" / deferred maddelerini
gözden geçirip kolayca kapatılabilecekleri tamamlamak için bir bakış.
Bu ADR yapılanları + Phase 5'e net olarak ertelenenleri kayıt altına
alır.

## Yapılanlar (bu commit ile)

### 1. `cd::math::Mat<T,4> inverse(...)` (Phase 3 wart)

ADR-20260522-wave18 hello_skybox sample'ı `cd::math::inverse` yokluğunda
camera basis push-constant workaround'u kullanmak zorunda kalmıştı.
Closed-form adjugate / determinant method, |det|<ε durumda identity'e
düşme yapan defensive fallback. 3 yeni test (`Matrix.IdentityInverseEqualsIdentity`,
`Matrix.InverseTimesOriginalEqualsIdentity`, `Matrix.SingularMatrixInverseFallsBackToIdentity`).

### 2. `cd::asset::AssetRegistry::load_auto(path)` (Wave 10 açık soru)

ADR-20260522-wave10-asset-loader-adapters'da:
> path-extension → tag dispatch: registry.load_auto("file.png")
> uzantıya bakıp doğru loader'ı çağırsın. v2.

Şimdi v2 → DONE. `tag_from_extension(path)` static helper + 9 format
mapping (png/jpg/bmp/tga/hdr → image, obj, ktx2, gltf/glb, cdmesh,
cdtex, wav, json). Case-insensitive. 1 yeni test.

## Phase 5'e ertelenenler (explicit)

Bunlar mevcut faza KABUL EDİLEMEZ ölçüde büyük veya kapsam dışı:

### Render
* **N-deep pipelined render thread** (S4.4 RingBuffer + per-frame fence
  array). Şu anki 1-deep AsyncSubmit yeterli; multi-frame pipelining
  Renderer refactor + TSan CI + 7-day stress prereq.
* **Forward+ / clustered shading**, **volumetric scattering** — Phase 5+.
* **Compute shader IBL pre-filtering** (BRDF LUT + irradiance cube).
  hello_skybox analytical atmosphere yeterli görsel; gerçek IBL Phase 5.

### Asset
* **Stream-able / chunked loader** — asset_image/gltf büyük dosya
  için. Mevcut "load all into RAM" pattern <500MB asset'ler için OK.
* **Asset bundle / .pak format** — `cdmesh`+`cdtex` zaten "cooked"
  pattern'i sağlar; bundling Phase 5.
* **VFS-backed tinygltf resolver** — gltf AssetLoader şu an
  self-contained .glb / inline .gltf'le sınırlı.

### Audio
* **WASAPI / CoreAudio / ALSA platform output** — Wave 16 FileSinkBackend
  dosyaya yazıyor. Gerçek-zamanlı audio output dedicated audio thread
  + COM init + buffer underrun handling gerektirir; Phase 5.

### ECS / Scene
* **`cd::concurrency::IJobDispatcher` integration** for Scheduler
  (Wave 17 açık soru). Şu anki `std::async` backend correctness için
  yeterli; perf optimization Phase 5.
* **Skeletal animation playback** (per-bone tracks, skinning matrix
  upload) — `cd::anim` MVP single-target Transform. Skinning Phase 5.

### Editor
* **Asset browser + drag-drop + multi-select** (Wave 19 açık soru).
  Şu anki hello_inspector tek seçim + save/load.
* **Binary .cdscene format** (Wave 21 açık soru). JSON şu an yeterli
  10k node Release ~3-5 ms.

### Networking
* **`cd::net` real impl** (replication, reliable/unreliable channels).
  Stub library mevcut; ECS scheduler v2 stabilleştikten sonra.

### CI / Tooling
* **Lavapipe Linux runner** GPU smoke job. Wave 8 ADR'de not düşülmüş.
* **Golden image diff** (FLIP / SSIM). Renderer'a `framebuffer_dump`
  primitive eklemek gerek.
* **TSan-only CI job** for render thread + parallel ECS — N-deep
  pipelining'le birlikte.

### Cross-platform
* **Linux/macOS GUI sample testing** — şu an headless tier OK,
  Vulkan-on-Linux Lavapipe ile çalışabilir.
* **Mobile / Web / Console** target — Phase 5+.

### C++ workflow polish
* **Hot-reload C++ kod** (Live++ / blink). Phase 5'in başlangıcı için
  iyi bir adım. Bağımsız RAD-toolchain işi.
* **Scripting layer** (Lua / WASM). v0.3 sonrası.

## Reddedilen alternatifler

* **Tüm backlog'u tek seferde kapat:** Her madde 1-3 hafta ayrı bir
  sprint işi. Bu ADR'nin amacı **yapılabilenleri yap + diğerlerini
  net şekilde belge altına al**, "yapıldı" denemeyi engelle.
* **Backlog hiç dokunma, S4.4 sonrası direkt v0.3 tag:** İki küçük
  iyileştirme (math inverse + load_auto) düşük risk, yüksek ergonomik
  kazanç. Kapatmak doğru karar.

## Sonuçlar

| Metric | S4.4 sonu | Backlog cleanup sonu |
|---|---|---|
| Engine libraries | 50 | **50** (header-only eklemeler) |
| Samples | 29 | **29** |
| ctest binaries | 51 | **51** |
| Internal test cases | ~95 | **~99** (+3 Matrix + 1 AssetRegistry) |
| Open backlog ADRs | many | **categorised + Phase 5 routing** |

Phase 4 closure %100 + backlog explicit categorisation. v0.3.0 tag'i
atılabilir; bu commit sonrası geriye sadece "kullanıcı görsel feedback
turu" kalıyor.

## Açık sorular

* `cd::math::inverse` Release optimizer'la constexpr-folded olur mu?
  Mevcut testler Debug build'de yapılıyor, Release identity-product
  precision'ı 1e-7 civarına çekebilir (test tolerance 1e-5 yeterli olur).
* `load_auto` extension mapping'i registered loader presence'ına bakmıyor
  — registry'de "image" loader yoksa "png" yine `image` tag'iyle aranır
  ve kNoLoaderForTag döner. Bu OK ama belki erken-kontrol istenir.
