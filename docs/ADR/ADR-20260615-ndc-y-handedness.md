# ADR-20260615-ndc-y-handedness

> Engine'in NDC-Y handedness konvansiyonu (Vulkan +Y-asagi authoring) ve bunun
> +Y-yukari backend'lerde (D3D12, Metal) negatif-yukseklikli viewport ile nasil
> eslestirildigi. Negatif-yukseklik viewport'unun winding (front-face) flip'i ve
> bu flip'in nasil telafi edildigi. phase1196 (D16 byte-exact pixel-parity) +
> parity1121 STRAND 2 cull fix/test referansi.

## Baglam

Engine **TEK bir clip-space konvansiyonu** ile authoring yapar: tum sample'lar ve
GLSL shader'lar **Vulkan NDC +Y-asagi** (clip-space `gl_Position.y` arttikca
framebuffer'da ASAGI inilir) duzeninde yazilir. Referans backend Vulkan'dir; bu
yuzden konvansiyon Vulkan-native secilmistir.

Sorun: backend'ler NDC-Y handedness'inde ayrilir.

- **Vulkan**: NDC +Y dogal olarak ASAGI bakar. `set_viewport` POZITIF yukseklik
  kullanir (`VulkanCommandBuffer.cpp:465-476`); NDC +Y direkt framebuffer-asagi'ya
  map'lenir. Authoring konvansiyonu = backend konvansiyonu, telafi gerekmez.
- **D3D12 & Metal**: NDC +Y dogal olarak YUKARI bakar. Ayni clip-space ucgeni
  telafi olmadan **dikey aynalanmis** (vertically mirrored) raster'lanir.

### Kanit 1 — Authoring konvansiyonu (RasterState varsayilani)

`engine/render/rhi/include/cd/rhi/Descriptors.hpp:179-203` `RasterState`:
varsayilan `front_face = FrontFace::kClockwise`. Yorum (183-197) bunu acikca
gerekcelendirir: her sample shader'da bir `clip.y = -clip.y` (veya esdeger)
Y-flip vardir; perspektif bolme sonrasi rasterizer CW ucgenler gorur, ve
`front_face = kClockwise` ile onlari dogru sekilde FRONT olarak siniflandirir.
Bu, back-face culling'in calismasini saglayan varsayilandir. (v0.25.0 oncesi
varsayilan `kCounterClockwise` idi ve her sample `cull = kNone` ile bozuk
winding'i maskeliyordu — v1.0 rollback ADR'sindeki BUG #1/#2/#5.)

### Kanit 2 — D3D12 negatif-yukseklik viewport (phase1196)

`engine/render/rhi/src/d3d12/D3D12Device.cpp:5127-5148` `set_viewport`:

```
v.TopLeftY = vp.y + vp.height;   // alt kenara sabitle…
v.Height   = -vp.height;         // …ve Y'yi flip'le -> +Y NDC asagi gider
```

Ayni negatif-yukseklik, parallel-pass implicit viewport rebind'inde de var
(`D3D12Device.cpp:4882-4895`). Bu, D3D12'yi Vulkan ile **piksel-piksel** eslestirir:
phase1196'da `test_backend_pixel_parity` (D16) D3D12 readback'ini Vulkan
referansina **BYTE-EXACT** yapan fix budur. Metal tarafi ayni mantikla flip'lenir.

### Kanit 3 — Winding flip implikasyonu (parity1121 STRAND 2'nin actigi)

Rasterizer front/back-face kararini **NDC'de degil, window-space'de** (viewport
transform sonrasi) imzali alan (signed area) isaretiyle verir. Negatif-yukseklik
viewport, NDC->window transform'unda Y eksenini tersine cevirir; bu da window-space
imzali alanin isaretini, dolayisiyla rasterizer'in gordugu winding'i **TERS** cevirir.

Sonuc: AYNI clip-space ucgeni Vulkan'da FRONT siniflanirken, D3D12'ye negatif-yukseklik
viewport ile ulastiginda **TERS apparent winding** ile gelir. Eger
`D3D12_RASTERIZER_DESC::FrontCounterClockwise` engine descriptor'unu DOGRUDAN
onurlandirirsa (phase1196 sonrasi kalan eski davranis), engine'in FRONT olarak
amacladigi ucgen (`front_face = kClockwise`) D3D12 tarafinda BACK siniflanir ve
`cull = kBack` ile **culling'e** ugrar — Vulkan'da gorunur, D3D12'de gorunmez.
Bu bir **face-cull parity bug**'idir. phase1196 yorumu ("D3D12 doesn't Y-flip in
NDC so we honor the descriptor directly") phase1196 ONCESI dogruydu; negatif-yukseklik
viewport eklendikten SONRA bayat (stale) ve hatali oldu.

D16 pixel-parity testi bu hatayi yakalayamadi cunku `cull = kNone` kullanir
(`test_backend_pixel_parity.cpp:460`) — winding-agnostic, kasitli olarak cull
parity'sini test ETMEZ. Marathon log bu bosligu isaretledi: "D3D12-face-cull
parity (neg-height winding flip; cull testi YOK)".

## Karar

1. **Authoring konvansiyonu Vulkan +Y-asagi NDC olarak SABITLENIR.** Tum
   engine GLSL'i ve sample'lar bu konvansiyonda yazilir; `RasterState.front_face`
   varsayilani `kClockwise` kalir.

2. **NDC-Y handedness farki, +Y-yukari backend'lerde NEGATIF-YUKSEKLIK VIEWPORT
   ile kapatilir** (D3D12 `set_viewport` + parallel-pass rebind; Metal flip).
   Projeksiyon-matris Y-flip satiri veya per-shader/per-vertex flip DEGIL
   (bkz. Reddedilen alternatifler).

3. **Negatif-yukseklik viewport'unun winding flip'i, D3D12 tarafinda
   `FrontCounterClockwise`'in engine descriptor'una gore MANTIKSAL OLARAK
   TERSLENMESI ile telafi edilir** (parity1121 STRAND 2 fix):

   ```
   // D3D12Device.cpp graphics PSO (1635-1653) + mesh-shader PSO (1951-1956)
   rs.FrontCounterClockwise =
       (desc.raster.front_face == cd::rhi::FrontFace::kClockwise) ? TRUE : FALSE;
   ```

   Yani `kClockwise -> TRUE`, `kCounterClockwise -> FALSE` (flip oncesi
   eslesmenin mantiksal degili). Boylece Vulkan'in FRONT siniflandirdigi ucgen
   D3D12'de de FRONT siniflanir; engine cull semantikleri backend-ozdes olur.

4. **Cull parity bir regresyon ag'i ile kilitlenir**:
   `engine/render/rhi/tests/test_d3d12_face_cull_parity.cpp` (WARP'ta gercek
   render): `front_face = kClockwise` ile front-wound bir ucgen `cull = kBack`
   altinda GORUNUR ve `cull = kFront` altinda CULL'lanir kalir; reversed ucgen
   tersi. Test bug'da FAIL, fix'te PASS eder (bidirectional ispat — bkz. Sonuclar).

## Reddedilen alternatifler

### A — Projeksiyon-matris Y-flip satiri (`proj[1][1] *= -1`)
Filament/bgfx'in bir kismi bunu yapar. Reddedildi cunku: (a) handedness bilgisini
**render-graph/material katmanina sizdirir** — her projeksiyon uretildigi yerde
backend-farkindaligi gerekir; (b) shadow/cubemap/IBL gibi proj-matris uretmeyen
veya birden cok proj kullanan path'lerde kacaklara yol acar; (c) negatif-yukseklik
viewport'la AYNI winding flip'ini zaten uretir, ama RHI sinirinda degil dagitik
sekilde. Tek yerde (set_viewport) kapatmak DAG'i temiz tutar (CLAUDE.md §7).

### B — Per-shader `gl_Position.y = -gl_Position.y` negate
Reddedildi cunku: her shader'a backend-kosullu kod sokar (engine TEK GLSL kaynagi
ilkesini kirar — D16'nin tum degeri "ONE engine GLSL authored ONCE"dir), shader
permutasyonu sisirir, ve yine ayni winding flip'ini telafisiz birakir. Authoring
konvansiyonunu shader'a degil RHI'ye gomeriz.

### C — Per-vertex CPU-side flip (vertex buffer'da y negate / index reverse)
Reddedildi cunku: asset/upload katmanini backend-farkindar yapar, GPU-side
skinning/morph ile cakisir, instancing'i bozar ve bellek/bandwidth ikiye katlar.
RHI-altinda cozulebilecek bir sorunu asset boundary'sine tasir.

### D — D3D12'de POZITIF-yukseklik viewport + ters UV/render hedefi
Reddedildi cunku byte-exact pixel parity'yi (D16) bozar; readback Vulkan'a gore
dikey aynalanir ve golden image diff'leri (FLIP/SSIM) yapay olarak basarisiz olur.

## Sonuclar

**Olumlu:**
- Engine cull semantikleri **backend-ozdes**: `front_face=kClockwise + cull=kBack`
  bir ucgeni Vulkan'da gorunurse D3D12'de de gorunur. Back-face culling artik her
  iki backend'de dogru calisir (sample'lar `cull=kNone` workaround'a muhtac degil).
- Handedness bilgisi **tek bir RHI noktasinda** (set_viewport + PSO rasterizer)
  kapsanir; material/shader/asset katmanlari backend-agnostik kalir.
- D16 byte-exact pixel parity korunur (cull=kNone path etkilenmez); `cull` semantigi
  artik ayri bir testle (`test_d3d12_face_cull_parity`) kapsanir.

**Olumsuz / dikkat:**
- `FrontCounterClockwise` eslesmesi negatif-yukseklik viewport'una BAGLI bir
  invariant'tir. Birisi set_viewport'u pozitif-yukseklige cevirirse (orn. parity'yi
  baska bir yolla cozmeye kalkarsa) bu telafi YANLIS olur. Bu bagimlilik hem her
  iki PSO site'indaki yorumda hem bu ADR'de belgelenmistir; `test_d3d12_face_cull_parity`
  bu invariant'i kilitler (set_viewport degisirse test FAIL eder).
- Metal tarafi ayni mantigi izlemelidir: negatif-yukseklik flip + winding telafisi.
  Metal'in `MTLWinding`/`setFrontFacingWinding` ayari D3D12 ile ayni mantikla
  (descriptor'a gore terslenmis) set edilmelidir; Metal cull parity testi gelecekteki
  bir strand'a birakildi (ROADMAP_PHASE_2 backend-parity mega-marathon kapsaminda).

**Dogrulama (parity1121 STRAND 2):**
- `cmake --build --preset ninja-debug` CLEAN.
- `ctest -R d3d12_face_cull_parity` PASS (2 test); fix temp-revert edildiginde
  4 assertion FAIL (front ucgen kBack ile cull'lanir, kBack/kFront rolleri swap) —
  bidirectional regresyon ag'i ispatli.
- `d3d12_depth_mrt` + `backend_pixel_parity` (D16) + `d3d12_parity_m4` +
  `backend_parity` PASS (paylasilan D3D12 PSO path'inde regresyon yok).
- chrome golden (hello_engine --golden-fixture 5) baseline ile BYTE-IDENTICAL
  (Vulkan default path dokunulmadi).

## Referanslar

- phase1196 — D16 negatif-yukseklik viewport, byte-exact pixel parity.
- `engine/render/rhi/src/d3d12/D3D12Device.cpp` set_viewport 5127-5148; parallel
  rebind 4882-4895; graphics PSO FrontCounterClockwise 1635-1653; mesh PSO 1951-1956.
- `engine/render/rhi/src/vulkan/VulkanDevice.cpp` map_front_face 484-487;
  rasterization state 1707-1715 / 2063-2071. `VulkanCommandBuffer.cpp:465-476`
  set_viewport (pozitif yukseklik).
- `engine/render/rhi/include/cd/rhi/Descriptors.hpp:179-203` RasterState varsayilani.
- `engine/render/rhi/tests/test_d3d12_face_cull_parity.cpp` — cull parity regresyon ag'i.
- `engine/render/rhi/tests/test_backend_pixel_parity.cpp:460` — D16 cull=kNone (parity
  testinin cull'u test ETMEDIGINE dair kanit).
- ADR-20260615-metal-backend-completion (Metal flip baglami).
