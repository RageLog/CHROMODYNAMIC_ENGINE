# ADR-20260522 — Three small library additions: profile + camera + headless

## Bağlam

Bu oturumda render-tier'a üç birbirinden bağımsız ama küçük yardımcı
kütüphane eklendi. Tek tek ADR yazmak overkill; sıkıştırılmış format.

## Karar

### cd::profile (W15.1, W10.3)

CPU scope-timer + sink interface + iki concrete sink (NullSink default,
BufferSink ring buffer) + StatsAggregator (per-name min/avg/max/count
rollup). Header-only API'ler + tek `.cpp` (Scope state).

```cpp
{
  CD_PROFILE_SCOPE("frame.upload");
  // ...
}
auto stats = aggregator.snapshot();  // sorted by total_ns desc
```

7 test (4 base + 3 aggregator). Tracy / Optick / RAD entegrasyonu v2 —
şimdilik kendi sink ihtiyacı yok, future ImGui HUD aynı StatsAggregator'ı
besleyecek.

### cd::camera (W7.1, W13.3)

Header-only `Camera` POD + `OrbitController` (auto-spin + drag + zoom) +
`auto_frame_aabb()` + `Frustum` extraction + p-vertex AABB cull test.
Dep: cd::math only.

```cpp
auto cam = cd::camera::auto_frame_aabb(scene.bbox_min, scene.bbox_max);
OrbitController orbit;
orbit.sync_from_camera(cam);
orbit.update_auto(cam, dt);
auto vp = cd::camera::view_projection(cam, aspect);
auto f  = cd::camera::extract_frustum(cam, aspect);
if (test_aabb(f, mn, mx) == CullResult::kOutside) skip();
```

8 test (matrix derivation, auto-framing, orbit roundtrip, AABB test
positive/negative). hello_cube ve hello_gltf bu helper'a refactor edildi
(önce inline `look_at` + `perspective` çağrıları vardı).

### Sample headless mode (W12.1)

`samples/common/SampleRuntime.hpp` (header-only) — `parse_runtime(argc,
argv)` `--headless N` ve `--no-spin` flag'lerini parse eder. Her sample
ana loop'ta `runtime.should_continue(frame_idx)` çağırıp false ise
`window.request_close()` der.

```cpp
auto rt = cd::sample::parse_runtime(argc, argv);
while (true) {
  if (!rt.should_continue(frame_idx)) window.request_close();
  ...
}
```

CMake katmanı: `add_library(cd_sample_common INTERFACE)` ile common
include path, her cd_add_sample'a `cd::sample_common` dep eklendi.

CI'nin smoke-test job'u artık `hello_cube.exe --headless 3` çağırabilir;
4 GPU sample (cube/gltf/scene_graph/framegraph/obj) doğrulandı.

## Reddedilen alternatifler

* **Profile için Tracy:** Production-grade ama 50+ MB build, kendi
  rendering UI'si. Engine için aşırı — kendi minimal sink ile başla,
  Tracy v2 entegrasyonu opt-in olsun.
* **Camera için GLM:** GLM dependency olur; cd::math zaten var, hot-path
  template'leri inline.
* **Headless için env var (`CD_HEADLESS=N`):** Daha az boilerplate ama
  CI'de `env:` block doldurmak zorunda. CLI flag local debugging için
  de doğal.

## Sonuçlar

* +33 yeni birim test (3 lib × ~10 test ortalaması).
* hello_obj + hello_gltf + hello_cube + hello_scene_graph + hello_framegraph
  artık CI'de headless smoke-test edilebilir.
* `cd::camera` cd::scene + cd::render'a sızıntı olmadan duruyor; tek
  bağımlılığı cd::math.

## Açık sorular

* `cd::profile` GPU-side scope timer ihtiyacı? Vulkan timestamp queries.
  ImGui HUD (W10.3 v2) bu veriyi de göstermeli.
