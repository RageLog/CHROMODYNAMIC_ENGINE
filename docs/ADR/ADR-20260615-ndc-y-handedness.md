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

phase1204 bu mantiktan yola cikip `FrontCounterClockwise`'i terslemisti. Ancak
**parity1224 ampirik RCA bu cikarimin YANLIS oldugunu gosterdi**: negatif-yukseklik
viewport zaten window-space piksellerini Vulkan ile bayt-ozdes uretir, ve her iki
API'nin facing kurali da window-space imzali alana gore tanimlidir. Window
koordinatlari ozdeslesince facing karari da ozdeslesir — descriptor DOGRUDAN
onurlandirildiginde (telafisiz). Terslemek IKINCI bir flip ekler ve facing'i Vulkan'a
gore TERS cevirir: o zaman engine'in FRONT amacladigi ucgen D3D12'de BACK siniflanir
ve `cull = kBack` altinda culling'e ugrar — Vulkan'da gorunur, D3D12'de gorunmez.
ASIL **face-cull parity bug** budur (telafi degil). Ayrinti: Karar 3 GUNCELLEME +
asagidaki "Raw-clip vs authored-convention".

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

3. **D3D12 `FrontCounterClockwise`, engine descriptor'unu DOGRUDAN (telafisiz)
   onurlandirir** — Vulkan `frontFace` ile AYNI polariteyle:

   ```
   // D3D12Device.cpp graphics PSO + mesh-shader PSO
   rs.FrontCounterClockwise =
       (desc.raster.front_face == cd::rhi::FrontFace::kCounterClockwise) ? TRUE : FALSE;
   ```

   Yani `kClockwise -> FALSE`, `kCounterClockwise -> TRUE`. Negatif-yukseklik
   viewport (Karar 2) zaten window piksellerini Vulkan ile bayt-ozdes yapar; her
   iki rasterizer da facing'i window-space imzali alandan belirledigi icin,
   window koordinatlari ozdeslesince facing karari da ozdeslesir — IKINCI bir
   flip GEREKMEZ. Boylece Vulkan'in FRONT siniflandirdigi ucgen D3D12'de de FRONT
   siniflanir; engine cull semantikleri backend-ozdes olur.

   > **GUNCELLEME (parity1224):** Bu maddenin ILK hali (phase1204)
   > `FrontCounterClockwise`'i TERSLEMISTI (`kClockwise -> TRUE`), "negatif-yukseklik
   > window imzali alani flip'ler, o yuzden telafi gerek" el-cikarimina dayanarak.
   > O cikarim Vulkan REFERANSI ile hicbir zaman capraz dogrulanmamisti. parity1224
   > eksik olan CAPRAZ-backend cull-facing testini kurdu
   > (`test_backend_pixel_parity` `CrossBackendCullFacingRealGeom`) ve ampirik
   > olarak olctu: AYNI engine-authored geometri (gercek `cd::math::perspective`
   > proj + `prim.vert` `clip.y = -clip.y` konvansiyonu), BAYT-OZDES piksel kapsami
   > ile, inversion ACIKKEN D3D12'yi Vulkan'a gore TERS yuz siniflamaya itiyordu
   > (`cull = kBack` altinda Vulkan'in cull'ladigini D3D12 gosteriyor ve tersi) —
   > GERCEK bir cull-parity bug. Inversion KALDIRILDI; descriptor dogrudan
   > onurlandiriliyor ve iki backend facing'de bayt-bayt eslesiyor. Bu, Reddedilen
   > alternatifler'in disinda kalan, ampirik olarak kanitlanmis dogru telafidir
   > (bkz. asagidaki "Raw-clip vs authored-convention" aciklamasi).

4. **Cull parity iki test ile kilitlenir**:
   - **CAPRAZ-backend (asil net):** `test_backend_pixel_parity`
     `CrossBackendCullFacingRealGeom` — GERCEK engine geometrisi (gercek proj
     matris + `prim.vert` `clip.y = -clip.y`) Vulkan VE D3D12 uzerinde render
     edilir; `cull = kNone` goruntuleri bayt-ozdes (ayni piksel kapsami) DOGRULANIR
     ve `cull = kBack` / `cull = kFront` altinda merkez-piksel facing karari iki
     backend'de OZDES olmalidir. phase1204 inversion'u geri konursa FAIL eder
     (revert-proof).
   - **INTRA-backend (host-stabil):** `test_d3d12_face_cull_parity` (WARP'ta gercek
     render, Vulkan ICD gerektirmez): ham winding
     `{(0,0.8),(0.8,-0.8),(-0.8,-0.8)}` engine konvansiyonunda **BACK** yuzdur —
     `cull = kBack` ile CULL'lanir, `cull = kFront` ile GORUNUR; reversed winding
     FRONT'tur ve tersi. (parity1224'te bu testin etiketleri duzeltildi: eski hali
     ham winding'i hatali olarak "front-wound" etiketleyip `kBack`'te gormeyi iddia
     ediyordu — Vulkan referansiyla celisen, inversion'a bagli yanlis varsayim.)

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
- `FrontCounterClockwise` = descriptor (telafisiz) eslesmesi negatif-yukseklik
  viewport'una BAGLI bir invariant'tir. Birisi set_viewport'u pozitif-yukseklige
  cevirirse (orn. parity'yi baska bir yolla cozmeye kalkarsa) bu eslesme YANLIS
  olur ve bir winding telafisi gerekir. Bu bagimlilik hem her iki PSO site'indaki
  yorumda hem bu ADR'de belgelenmistir; `CrossBackendCullFacingRealGeom` (capraz)
  ve `test_d3d12_face_cull_parity` (intra) birlikte bu invariant'i kilitler.
- Metal tarafi DA AYNI SEKILDE DUZELTILDI (parity1224, ayni commit): Metal de
  D3D12 ile birebir ayni negatif-yukseklik viewport mekanizmasini kullaniyor
  (`MetalCommandBuffer.mm` originY=y+h, height=-h), yani ayni RCA gecerli. phase1209
  Metal `to_winding` INVERSION'u (bu da phase1204 D3D12 inversion'unu aynaliyordu)
  KALDIRILDI: `MetalPipeline.mm to_winding` artik descriptor'u DOGRUDAN onurlandiriyor
  (`kClockwise -> MTLWindingClockwise`, terslemeden — Vulkan/duzeltilmis-D3D12 ile ayni
  polarite). Metal `.mm` Windows'ta gated-off oldugu icin bu Mac derlemesinde dogrulanir;
  `test_metal_face_cull_parity` (C-METAL-CULL) duzeltilmis beklentiyle (raw winding = BACK,
  reversed = FRONT) Apple donaniminda kilitler. Yani 3 backend de ayni polarite; ertelenen
  veya bilinen-yanlis bir winding kalmadi.

### Raw-clip vs authored-convention (parity1224 acikligi)

Wave-3d "deferred finding"i, AYNI **ham clip-space** ucgenin (proj matrissiz,
dogrudan clip koordinatlariyla yazilmis) iki backend'de `cull = kBack` altinda
TERS facing aldigini gozledi. Bu cozumlendi ve onemli bir noktayi netlestirir:

- **Rasterizer proj matrisini GORMEZ — yalnizca son clip koordinatlarini gorur.**
  Bu yuzden "ham clip-space ucgen" ile "gercek geometriden uretilmis ama AYNI clip
  koordinatlarina dusen ucgen" rasterizer icin OZDESTIR. Dolayisiyla facing farki
  ham/gercek ayriminA bagli DEGILDI — parity1224 RCA bunu gercek
  `cd::math::perspective` + `clip.y = -clip.y` yolundan gecen GERCEK geometriyle
  birebir tekrar uretti (`CrossBackendCullFacingRealGeom`, iki winding, iki cull
  modu, `cull = kNone` goruntuleri bayt-ozdes).
- Yani gozlenen fark **authoring konvansiyonu artefakti DEGIL, gercek bir D3D12
  facing bug'iydi** (phase1204 inversion'u). Inversion kaldirilinca hem ham-clip
  hem gercek-geometri durumlari iki backend'de BAYT-OZDES facing verir.
- Sonuc: cull facing icin gecerli sozlesme **"AYNI window pikselleri => AYNI
  facing karari (ayni `front_face`/`cull_mode` ile)"**; bu, ham veya proj-uretilmis
  geometri ayrimindan BAGIMSIZDIR. Deferral KAPANDI (bug-fixed + `CrossBackend-
  CullFacingRealGeom` ile kilitli), askida birakilmadi.

**Dogrulama (parity1224 — RTX 3080 + WARP, ampirik RCA):**
- `cmake --build --preset ninja-debug` CLEAN (-Werror, 0 yeni clang-tidy).
- **Ampirik RCA:** AYNI gercek geometri iki backend'de render edildi —
  `cull = kNone` goruntuleri VK-vs-DX bayt-ozdes (max diff 0/255); inversion ACIKKEN
  `cull = kBack` altinda VULKAN cull'larken D3D12 gosteriyordu (reverse'te tersi).
  Inversion KALDIRILINCA iki backend tum durumlarda OZDES (VK back=25/DX back=25 vb.).
- `ctest -R backend_pixel_parity` PASS — `CrossBackendCullFacingRealGeom` dahil;
  inversion temp-geri konunca FAIL (revert-proof).
- `ctest -R d3d12_face_cull_parity` PASS (2 test, etiketler duzeltildi: ham winding
  = BACK yuz).
- `d3d12_depth_mrt` + `framegraph_vulkan` + `ddgi_dispatch` + `rhi_caps_conformance`
  PASS (paylasilan D3D12 PSO path'inde regresyon yok).
- chrome golden (`hello_engine --golden-fixture 5 --golden-frames 3`) baseline ile
  BYTE-IDENTICAL (`cmp` ile dogrulandi; Vulkan default path dokunulmadi — fix
  yalnizca D3D12 PSO mapping'i).

## Referanslar

- phase1196 — D16 negatif-yukseklik viewport, byte-exact pixel parity.
- phase1204 — ilk (hatali) `FrontCounterClockwise` inversion + bu ADR'nin ilk hali.
- parity1224 — ampirik capraz-backend RCA; inversion kaldirildi (descriptor dogrudan);
  `CrossBackendCullFacingRealGeom` testi + `test_d3d12_face_cull_parity` etiket duzeltmesi.
- `engine/render/rhi/src/d3d12/D3D12Device.cpp` — `set_viewport` neg-height (~6766-6787);
  parallel-pass rebind neg-height (~6443-6453); graphics PSO `FrontCounterClockwise`
  (~2040, descriptor dogrudan); mesh-shader PSO (~2415, descriptor dogrudan).
- `engine/render/rhi/src/vulkan/VulkanDevice.cpp` `map_front_face` (~486-489),
  rasterization state `frontFace` (~1741 / ~2140); `VulkanCommandBuffer.cpp` set_viewport
  (~566-577, pozitif yukseklik).
- `engine/render/rhi/include/cd/rhi/Descriptors.hpp` RasterState varsayilani (`kClockwise`).
- `engine/render/rhi/tests/test_backend_pixel_parity.cpp` —
  `CrossBackendCullFacingRealGeom` (capraz-backend cull facing, gercek geometri) +
  `Vulkan/D3D12CullFacingInvariant` (intra) + D16 `cull=kNone` capstone.
- `engine/render/rhi/tests/test_d3d12_face_cull_parity.cpp` — intra-backend cull parity
  ag'i (ham winding = BACK yuz, parity1224 duzeltmesi).
- ADR-20260615-metal-backend-completion (Metal flip baglami).
