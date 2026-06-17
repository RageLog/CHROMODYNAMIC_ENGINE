# ADR-20260616 — ALL-MODULES-TO-100 BAND 4 / RENDER-FEATURES Kapsam Mührü (4 kütüphane + light::ClusterGrid B4 konsolidasyon kararı)

- **Status**: Accepted
- **Date**: 2026-06-16
- **Branch**: dev (HEAD 42855ac)
- **Deciders**: Cemal TATLI
- **Author**: Developer (BAND 4 render-features subset close-out — CPU-reference-v1 + GLSL-v1 seal + promote-on-need RHI-dispatch trigger + edge-test-topup + false-banner correction pass)
- **Related**:
  - `docs/ROADMAP_ALL_MODULES_TO_100.md` (BAND 4 listesi: light 68 "move ClusterGrid
    into the single froxel owner; add .cpp + tests", volumetric 66 "real .cpp + RHI
    dispatch (header-only GLSL today)", atmosphere 62 "bake all 4 LUTs (only 1 today)
    + RHI dispatch", denoise 60 "real OIDN backend (pass-through stub today) +
    .cpp/RHI"; §"What 100% MEANS" honest-rule; §Cross-band notes "De-dup froxel
    clustering … resolve ownership ONCE when the first of them is worked (Band 3),
    the others then collapse")
  - `docs/PROJECT_COMPLETION_STATUS.md` §5 (render FEATURES baseline: light 68
    "attenuation, Planckian color-temp, CSM split/matrix, ClusterGrid (data-only);
    18 tests; ⚠ ClusterGrid overlaps lighting_clusters; no .cpp", volumetric 66
    "VolumetricFog 445 inject/integrate/composite froxel GLSL + clouds + fog; 42
    tests; header-only, no .cpp/RHI dispatch", atmosphere 62 "Hillaire CPU
    transmittance-LUT baker (40-step) + GLSL CS; 6 tests; only 1 of 4 promised LUTs
    baked, no RHI dispatch", denoise 60 "edge-aware a-trous (Dammertz) CPU + GLSL CS;
    6 tests; OIDN backend is explicit pass-through stub; no .cpp/RHI")
  - `docs/ADR/ADR-20260616-band3-render-features-scope.md` §5 (FROXEL OWNERSHIP
    decision — owner = cd::lighting_clusters; light::ClusterGrid'in Band-4
    konsolidasyon YÖNÜ PRE-DECIDED: light → owner, owner ≠ light)
  - kardeş band-mühür ADR'ları (aynı şablon: decide-the-cross-cut + impl-the-clean-gap
    + seal-the-rest-as-vN + promote-on-need + targeted-edge-topup)
- **Scope guard**: Bu ADR YALNIZ kapsam-karar + edge-test-topup + false-banner-fix
  dokümanıdır. Mühürlenen maddeler "deferred-by-design"dır — her biri "ihtiyaç
  doğunca promote" çıkış kapısı taşır. Dört kütüphane de CPU-reference + GLSL-string
  + header-only karakterinde; gerçek GPU `.cpp`/RHI dispatch bir TÜKETİCİ-entegrasyon
  meselesidir (cd::ddgi / cd::restir_di pattern'i) → dürüst terminal durum CPU-ref-v1'i
  MÜHÜRLEMEK + gerçek dokunulmamış dalları test etmektir. light::ClusterGrid B3 §5
  pre-decision'ı uygulanır (SEAL data-only-v1, owner = lighting_clusters). engine/render/
  {light,volumetric,atmosphere,denoise}/ + bu ADR DIŞINA DOKUNULMADI: lighting_clusters/
  cluster/ibl (B3) + ibl_gpu (B6) + rhi/ + samples/ + hello_* + % docs DEĞİL. Chrome
  golden BYTE-IDENTICAL (fixture #5 cmp baseline).

---

## 1. Bağlam

BAND 4 (60–69%) render-features alt-kümesi 4 kütüphaneyi 100%'e taşır:
cd::light (68), cd::volumetric (66), cd::atmosphere (62), cd::denoise (60). Dördü de
INTERFACE/header-only: CPU referans matematiği + gömülü GLSL compute-string'leri
taşır; gerçek GPU dispatch (`.cpp` + RHI command buffer) yoktur. Roadmap'in B4
hedefleri ("real .cpp + RHI dispatch", "bake all 4 LUTs", "real OIDN backend") bir
TÜKETİCİ render-yolu (hello_engine'in fog/sky/denoise pass'lerini RHI'a bağlaması)
gerektirir — bu cd::ddgi / cd::restir_di'da uygulanan "library = CPU-ref + GLSL;
renderer = dispatch" katman ayrımıdır.

Roadmap honest-rule'una göre bir modül 100%'tür ancak HER dokümante boşluk iki
terminal durumdan birindeyse: (a) IMPLEMENTED + test (küçük/net), VEYA (b) bir
paragraflık kapsam-ADR'ı ile formally SEALED (promote-on-need tetikleyicisiyle).
"TODO/placeholder" üçüncü durumu kalamaz. Bu pass'in bölünme çizgisi:

- **CPU-reference + GLSL-v1 SEAL**: dördünün de matematiği + GLSL kernel'i doğru +
  test-dolu → CPU-ref-v1 SEALED; gerçek `.cpp`/RHI dispatch promote-on-need
  (tüketici render-yolu bağlandığında).
- **light::ClusterGrid çapraz-kesen**: B3 §5 PRE-DECIDED — owner = lighting_clusters,
  light → owner (owner ≠ light). Bu pass'te B3 ADR'ının önceden "zero-consumer"
  varsaydığı light::ClusterGrid'in ASLINDA bir produksiyon tüketicisi (hello_engine
  lights-panel stats: `main.cpp` cluster_grid + HelloShowcaseProbes) olduğu
  bulundu → adapter/forward golden-riski + scope-ihlali (samples/ DIŞI) getirir →
  **SEAL data-only-v1** (B4 brief'in "ELSE SEAL" dalı), migration owner'a
  PRE-DECIDED kalır, logic DUPLİKE EDİLMEZ.
- **edge-test-topup**: dört kütüphanede de gerçek test-edilmemiş dallar vardı →
  hedefli +22 edge test (light +12, volumetric +3, atmosphere +4, denoise +3).
- **false-banner-fix**: dört README'de de derlenmeyen/var-olmayan API'ye atıf yapan
  yanıltıcı banner'lar vardı (denoise `create_atrous_filter`/`apply()`; light
  `kelvin_to_linear_rgb` + type-olmayan-Attenuation/CascadedShadow; atmosphere
  var-olmayan TransmittanceLut/MultiScatteringLut/SkyViewLut header+type'ları;
  volumetric var-olmayan VolumetricGrid/FogPass/CloudPass + "<3 test" stale TODO) →
  düzeltildi (çalışan path'i yanlış-tanıtan banner'ları onar, brief kuralı).

---

## 2. Karar — kütüphane başına bir bölüm

### 2.1 cd::light (render/light) — 68 → 100  [SEALED CPU-data-v1 + ClusterGrid-data-only-v1 + edge-topup]

- **Bağlam**: INTERFACE header-only (`cd::core` + `cd::math` + `cd::asset`). Beş header:
  `Light.hpp` (112-byte std140 POD + directional/point/spot/rect_area ctor'ları),
  `Attenuation.hpp` (Frostbite windowed inverse-square + cone + lumens/lux dönüşümleri,
  free function), `CascadedShadow.hpp` (Practical Split Scheme + tight ortho fit + cascade
  build, free function), `ColorTemperature.hpp` (Krystek/CIE-1931 CCT→linear-sRGB),
  `ClusterGrid.hpp` (16×9×24 view-space froxel grid, **data-only** — XY cull yok,
  fixed-32-cell overflow). `.cpp` YOK (header-only, INTERFACE lib). 18 → 30 gtest.
- **Karar (DECIDE light::ClusterGrid → owner; B3 §5 uygula)**:
  - **light::ClusterGrid = SEALED data-only-v1.** B3 §5 owner = cd::lighting_clusters'ı
    uygular. B3 ADR light::ClusterGrid'i "zero-consumer (yalnız kendi testi)" varsaydı;
    bu pass'te ASLINDA bir produksiyon tüketicisi (hello_engine `main.cpp:5265/8186`
    cluster_grid + `HelloShowcaseProbes.hpp:1247` lights-panel stats: `assign()` +
    `cluster_count()` per-frame) olduğu bulundu. Bu yüzden:
    - **Thin-adapter (lighting_clusters::Clusterer'a forward)** GÜVENLE YAPILAMAZ:
      light `assign(light_index, Light, view_space_pos)` view-space + tüm-XY-tile API'si
      vs Clusterer `assign(span<PointLight world>, view_proj[16])` world-space + tight
      AABB API'si — bir angular↔matrix köprüsü ister, bu `cluster_hits` sayısal
      çıktısını DEĞİŞTİRİR (Clusterer XY cull eder, light etmez), hello_engine
      panel-stat davranışını + potansiyel golden'ı kırar, ve hello_engine'e dokunmayı
      (SCOPE DIŞI: samples/, hello_*) gerektirir.
    - → **SEAL data-only-v1** (B4 brief: "ELSE SEAL light::ClusterGrid as data-only-v1
      with the documented migration to the owner; do NOT duplicate logic further").
      Logic DUPLİKE EDİLMEDİ; davranış DEĞİŞMEDİ → golden bayt-aynı.
  - **Geri kalan (Attenuation / CascadedShadow / ColorTemperature / Light POD)** =
    charter-complete CPU-data-v1 SEALED. Hepsi IMPLEMENTED + test-dolu, gerçek
    test-edilmemiş dallar +12 edge test ile kapatıldı (aşağıda).
- **Eklenen edge testler (gerçek test-edilmemiş dal)**:
  - Attenuation: non-positive-range early-out, window-clamp d>range (no anti-light leak),
    cone degenerate-denom binary fallback, spot malformed-cone→point fallback,
    lux-directional identity.
  - ColorTemperature: <1000K / >15000K clamp branch, 2222K/4000K y-branch +4000K
    x-branch boundary non-negativity sweep.
  - CascadedShadow: cascade_count>kMaxCascades clamp, lambda=0 (uniform) vs lambda=1
    (log) bracket-blend.
  - ClusterGrid: overflow-counted-not-dropped (fixed-32 push_ overflow path),
    point-light-covers-all-XY-in-Z-slices (data-only no-XY-cull contract pin —
    migration anchor), slice_z_range inverts slice_of (GPU reconstruction inverse).
- **Gerekçe**: light tamamen header-only data + math; "add .cpp" sadece bir tüketici
  RHI dispatch'i gerektirseydi anlamlı olurdu (bu lib UBO/SSBO POD üretir, dispatch
  tüketicinin işi — cluster grid'i hello_engine zaten SSBO'ya map etmeden, sadece
  panel-stat için CPU'da tüketiyor). ClusterGrid B3 §5 owner'a migrate eder; bu pass
  data-only-v1'i mühürler + migration'ı davranış-anchor test'lerle korur.
- **Promote-on-need**: light::ClusterGrid'in fiili lighting_clusters::Clusterer'a
  migrasyonu (hello_engine tüketicisinin Clusterer API'sine port'u + panel-stat
  davranış-eşdeğerliği doğrulaması + golden re-baseline) bir Forward+ many-light
  render-yolu entegrasyonu + ayrı migration-ADR ister; tüketici samples/ kapsamında
  olduğu için bu render-features pass'inin DIŞINDADIR. Light POD/Attenuation/
  CascadedShadow/ColorTemperature public yüzeyi sabit kalır.

### 2.2 cd::volumetric (render/volumetric) — 66 → 100  [SEALED CPU-math+GLSL-v1 + froxel-edge-topup]

- **Bağlam**: 4 INTERFACE target (`cd::volumetric` core + `_fog` + `_clouds` + `_fx`
  umbrella). `VolumetricFog.hpp` (445 LOC Wronski-2014 160×90×64 froxel: `inject_cell`
  scattering+extinction, `integrate_view_ray` front-to-back march, `froxel_to_view`/
  `view_to_froxel` koordinat dönüşümü, `slice_to_view_z` quadratic warp, `slice_thickness`,
  `beer_lambert` + 3 GLSL kernel `kVolFogInjectCS`/`kVolFogIntegrateCS`/
  `kVolFogCompositeCS`) + `clouds/Clouds.hpp` + `fog/Fog.hpp` + legacy
  `render/volumetric/Fog.hpp` ray-march. Header-only, `.cpp`/RHI dispatch YOK. 42 → 45 gtest.
- **Karar (DECIDE real .cpp + RHI dispatch — brief §volumetric)**: volumetric
  **CPU-math + GLSL-v1 olarak SEALED**. Froxel inject/integrate/composite matematiği +
  3 GLSL kernel doğru + test-dolu; CPU "truth"'u GLSL ile push-constant-besleme yoluyla
  byte-equivalent (header banner'ında dökümante). Gerçek `.cpp` RHI dispatch
  (image3D allocate + 3-pass compute submit + depth/scene composite) bir TÜKETİCİ
  render-yolu (hello_engine'in fog pass'i) işidir — cd::ddgi/restir_di pattern'i.
  Bu pass froxel'in gerçek test-edilmemiş dallarına +3 edge test ekledi:
  view-to-froxel-outside-frustum (caller-clamp out-of-range contract),
  slice-thickness-grows-with-depth (+ thickness toplamı = near→far span),
  integrate-front-to-back-occlusion (heavy extinction → tail slices contribute ~0).
- **Gerekçe**: Wronski froxel pipeline charter'ı TAM (inject/integrate/composite +
  HG phase + Beer-Lambert IMPLEMENTED + test-dolu); GPU dispatch tüketici işi.
  Eklenen 3 test önceki 42'nin dokunmadığı out-of-frustum + slice-thickness-monotonicity
  + front-to-back-occlusion dallarını sınar; bake/davranış DEĞİŞTİRMEZ.
- **Promote-on-need**: gerçek `.cpp` + RHI dispatch (FroxelGrid → image3D upload,
  3 compute pass submit, scene composite) hello_engine fog-pass entegrasyonu + ayrı
  ADR ister. inject_cell/integrate_view_ray/coord-transform/GLSL public yüzeyi sabit.

### 2.3 cd::atmosphere (render/atmosphere) — 62 → 100  [SEALED transmittance-LUT-v1 + 4-LUT-plan + LUT-edge-topup]

- **Bağlam**: INTERFACE header-only (`cd::math`). `Atmosphere.hpp` (183 LOC):
  `Parameters` (Hillaire-2020 Earth profilleri: Rayleigh/Mie/ozone + Mie-g),
  `bake_transmittance_lut` (40-step CPU optical-depth integral → view-ray transmittance
  LUT), `rayleigh_phase` + `henyey_greenstein`, `kTransmittanceCS` GLSL CS. Hillaire'in
  4 LUT'undan SADECE 1'i (transmittance) bake-edilmiş; multi-scatter/sky-view/aerial
  yok, RHI dispatch yok. 6 → 10 gtest.
- **Karar (DECIDE bake all 4 LUTs — brief §atmosphere)**: atmosphere
  **transmittance-LUT-v1 olarak SEALED**, 4-LUT-plan dökümante. 2. LUT'u (multi-scatter)
  bake etmek "small+clean+testable" DEĞİL: Hillaire multi-scatter, transmittance-LUT
  lookup'ı + küre üzerinde spherical-harmonics integrasyon ister (referans-doğrulama
  olmadan correctness-riski) → SEAL + promote-on-need. Transmittance LUT'unun gerçek
  test-edilmemiş dalları +4 edge test ile kapatıldı: zenith-vs-horizon path-length
  gradient, Rayleigh-blue-redder-at-horizon spectral ordering, nadir-grazing-path
  finite (max(disc,0) guard), ozone-layer-green-absorption (ozone term katkısı).
- **4-LUT plan (Hillaire 2020; promote-on-need)**:
  1. **transmittance** (view-zenith × altitude) — DONE (`bake_transmittance_lut` +
     `kTransmittanceCS`).
  2. **multi-scattering** (zenith × altitude, isotropic 2nd+ bounce) — promote-on-need;
     transmittance-LUT lookup + spherical integral; tüketici sky-pass + referans
     doğrulaması ister.
  3. **sky-view** (azimuth × zenith) — promote-on-need; transmittance + multi-scatter
     LUT'larına bağlı.
  4. **aerial-perspective** (screen xy × depth froxels) — promote-on-need; volumetric
     froxel grid'e benzer 3D LUT.
- **Gerekçe**: transmittance LUT charter'ı TAM (40-step integral + spektral RGB +
  ozone/Mie/Rayleigh profilleri IMPLEMENTED + test-dolu); LUT 2-4 + RHI dispatch bir
  full-sky tüketici render-yolu işi. Eklenen 4 test önceki 6'nın dokunmadığı
  mu-column-gradient + spectral-ordering + nadir-finite + ozone-contribution dallarını
  sınar; bake çıktısını DEĞİŞTİRMEZ.
- **Promote-on-need**: LUT 2-4 bake'i + RHI compute dispatch (image2D/image3D upload +
  4-pass CS submit) bir analytical-sky tüketici (cd::material::AnalyticalSkyMaterial'ın
  sky-pass entegrasyonu) + ayrı ADR ister. Parameters/bake_transmittance_lut/phase
  public yüzeyi sabit.

### 2.4 cd::denoise (render/denoise) — 60 → 100  [SEALED a-trous-v1 + OIDN-stub + a-trous-edge-topup + banner-fix]

- **Bağlam**: INTERFACE header-only (`cd::math`). `Denoise.hpp` (210 LOC):
  `edge_weight` (Dammertz-2010 edge-stop: color/normal/depth), `atrous_iteration`
  (5×5 kernel, step 2^it), `denoise_atrous` (5-iter SVGF-default multi-pass) +
  `kAtrousCS` GLSL CS — bu A-TROUS YOLU GERÇEK + ÇALIŞIR. `denoise_oidn` =
  açık pass-through stub (input unchanged; CD_ENABLE_OIDN + OIDN dep olmadan).
  6 → 9 gtest.
- **Karar (DECIDE real OIDN backend — brief §denoise)**: denoise **a-trous-v1 +
  OIDN-stub olarak SEALED**. A-trous CPU + GLSL referansı charter-complete + test-dolu;
  OIDN backend bir HARİCİ DEP (Intel OpenImageDenoise 2.x via vcpkg/FetchContent +
  CD_ENABLE_OIDN) gerektirir → gerçek body promote-on-need. A-trous'un gerçek
  test-edilmemiş dalları +3 edge test ile kapatıldı: edge-weight-normal-discontinuity
  (geometry edge-stop branch), atrous-preserves-sharp-depth-edge (per-tap reject path),
  atrous-zero-iterations-returns-copy (degenerate-settings path).
- **False-banner-fix**: README "Usage example"'ı var-olmayan `create_atrous_filter(...)`
  + `denoiser->apply()` API'sini gösteriyordu (çalışan free-function `denoise_atrous`
  path'ini YANLIŞ tanıtan banner) → gerçek API'ye düzeltildi + OIDN-stub durumu açıkça
  işaretlendi (brief: "Correct any false 'stub' banner that misrepresents the working
  a-trous path").
- **Gerekçe**: a-trous charter'ı TAM (Dammertz edge-stop + 5-iter SVGF spatial +
  GLSL IMPLEMENTED + test-dolu); OIDN gerçek-entegrasyonu OIDN dep + CD_ENABLE_OIDN
  gerektirir. Eklenen 3 test önceki 6'nın dokunmadığı normal-edge-stop + depth-edge-
  preservation + zero-iter dallarını sınar.
- **Promote-on-need**: gerçek OIDN backend (CD_ENABLE_OIDN + OIDN 2.x vcpkg/FetchContent
  dep + `cd/denoise/OidnBackend.cpp` non-empty body + device buffer/filter wire-up) bir
  path-trace tüketici + ayrı entegrasyon-ADR ister. denoise_atrous/edge_weight/
  denoise_oidn (API-stable stub) public yüzeyi sabit.

---

## 3. Reddedilen alternatifler

- **light::ClusterGrid'i lighting_clusters::Clusterer'a thin-adapter ile forward
  etmek (seal yerine)**: light view-space + tüm-XY API'si vs Clusterer world-space +
  tight-AABB API'si bir angular↔matrix köprüsü ister → `cluster_hits` sayısal çıktısı
  DEĞİŞİR (Clusterer XY cull eder), hello_engine panel-stat davranışını + golden'ı
  riske atar, ve hello_engine'e (SCOPE DIŞI) dokunmayı gerektirir. RED — B4 brief'in
  "ELSE SEAL data-only-v1, do NOT duplicate logic further" dalı tam bunun için.
- **light'a .cpp eklemek**: light tamamen header-only data + math POD üretir; tek
  tüketicisi (hello_engine) cluster grid'i CPU panel-stat için tüketiyor, RHI dispatch
  için değil. ".cpp + tests" ancak bir RHI-dispatch tüketicisi olsaydı anlamlı; bu lib
  charter'ı UBO/SSBO POD üretmek, dispatch tüketicinin işi. RED — header-only doğru
  katman; .cpp yapay olurdu.
- **atmosphere'da 2. (multi-scatter) LUT'u bu pass'te bake etmek**: Hillaire
  multi-scatter, transmittance-LUT lookup + küre-üzeri spherical integral ister
  (referans-doğrulama olmadan correctness-riski) → "small+clean+testable" değil. RED —
  SEAL transmittance-v1 + 4-LUT-plan dökümante + promote-on-need; LUT 2-4 bir full-sky
  tüketici ister.
- **volumetric/atmosphere/denoise'a gerçek RHI dispatch .cpp eklemek**: üçü de
  CPU-ref + GLSL-string; gerçek dispatch (image allocate + CS submit + barrier) bir
  RHI device + tüketici render-yolu + golden-image test-harness ister (cd::ddgi/
  restir_di pattern'i). RED — library = CPU-ref + GLSL (doğru katman); dispatch =
  renderer (promote-on-need, tüketici-bağlı).
- **OIDN backend'i bu pass'te wire etmek**: Intel OpenImageDenoise 2.x harici dep
  (vcpkg/FetchContent) + CD_ENABLE_OIDN + device buffer interop ister; dep yok. RED —
  a-trous-v1 + OIDN-stub SEAL; gerçek OIDN promote-on-need (dep gelince).
- **Dört kütüphanenin charter-complete yüzeyini pad etmek**: dördü de temel
  matematiği round-trip/regression test-doluydu. Yapay test "add tests ONLY for a
  genuinely-untested branch … don't pad" kuralına aykırı. RED — yalnız gerçekten
  dokunulmamış dallar hedeflendi (+22: attenuation/cct/csm/clustergrid edge'leri,
  froxel out-of-frustum + slice-thickness + occlusion, transmittance spectral/nadir/
  ozone, a-trous normal/depth-edge + zero-iter).
- **lighting_clusters/cluster/ibl (B3) / ibl_gpu (B6) / rhi / samples / hello_* / %
  docs'u düzenlemek**: kapsam DIŞI (brief SCOPE EXCLUSION: ONLY engine/render/
  {light,volumetric,atmosphere,denoise}/ + docs/ADR/). light::ClusterGrid'in fiili
  migrasyonu owner'a PRE-DECIDED ama tüketici samples/ kapsamında. RED.

## 4. Sonuçlar

- (+) 4/4 BAND-4 render-features alt-küme kütüphanesi honest-rule terminal durumuna
  geçti: light SEALED CPU-data-v1 + ClusterGrid-data-only-v1 (B3 §5 owner uygulandı);
  volumetric SEALED CPU-math+GLSL-v1; atmosphere SEALED transmittance-LUT-v1 +
  4-LUT-plan; denoise SEALED a-trous-v1 + OIDN-stub. Hiçbir kütüphanede
  placeholder/TODO yok (a-trous + transmittance + froxel pipeline + light data hepsi
  gerçek + test-dolu; OIDN/LUT-2-4/RHI-dispatch açık promote-on-need tetikleyicili SEAL).
- (+) **light::ClusterGrid B4 KONSOLİDASYON KARARI uygulandı**: B3 §5 pre-decision
  (owner = lighting_clusters) honor edildi; B3'ün "zero-consumer" varsayımı
  DÜZELTİLDİ (hello_engine produksiyon tüketicisi var) → thin-adapter golden-riski +
  scope-ihlali nedeniyle RED, **SEAL data-only-v1** (B4 brief'in açık "ELSE SEAL"
  dalı), logic duplike EDİLMEDİ, migration owner'a PRE-DECIDED kaldı + davranış-anchor
  test (no-XY-cull contract pin) eklendi.
- (+) 22 yeni edge test, gerçek test-edilmemiş dalda (light +12, volumetric +3,
  atmosphere +4, denoise +3). Hepsi edge/negative/contract + fail-on-revert;
  anti-flakiness korundu (deterministik fixture'lar, sleep_for YOK).
  cd_test_light 18→30, volumetric_fog_froxel 20→23, atmosphere 6→10, denoise 6→9.
- (+) 4 README false-banner düzeltildi (çalışan path'i yanlış-tanıtan banner'lar):
  denoise create_atrous_filter→denoise_atrous + OIDN-stub işaretlendi; light
  kelvin_to_linear_rgb→cct_to_linear_rgb + free-function/data-only-ClusterGrid;
  atmosphere var-olmayan TransmittanceLut/MultiScatteringLut/SkyViewLut header+type'ları
  → tek Atmosphere.hpp + 4-LUT-roadmap; volumetric var-olmayan VolumetricGrid/FogPass/
  CloudPass/FogPass.hpp + stale "<3 test" TODO → gerçek FroxelGrid/inject/integrate API.
- (+) Chrome golden BYTE-IDENTICAL (fixture #5, baseline ile `cmp` eşit). Dört
  kütüphanede de SADECE test-dosyaları + README'ler + bu ADR değişti (lib header
  davranışı aynı; ClusterGrid logic duplike edilmedi/değişmedi; hello_engine'e
  dokunulmadı) → render-yolu bayt-aynı. Build -Werror temiz, 0 yeni clang-tidy WAE
  class (eklenen test kodu: emplace yok-gereği/push_back yok, using-namespace yok,
  unused-using yok, explicit-widening cast'lar size-math'te).
- (−) Mühürler dört kütüphanenin gerçek RHI dispatch `.cpp`'sini + atmosphere LUT 2-4'ü
  + OIDN backend'i + light::ClusterGrid'in lighting_clusters'a fiili migrasyonunu bu
  pass'te ÜRETMEZ; hepsi bir tüketici render-yolu entegrasyonu (cd::ddgi/restir_di
  pattern) + ayrı ADR'lerle gelir. Kabul: BAND 4 render-features alt-kümesi
  "seal-CPU-ref-v1 + promote-on-need-dispatch + edge-topup + banner-fix" karakterinde —
  matematik + GLSL doğru + test-dolu, gerçek GPU dispatch tüketici-bağlı.

---

## Varsayımlar

- Brief'in "100% RULE per lib" yorumu: dört kütüphane de CPU-reference + GLSL-string +
  header-only; gerçek GPU `.cpp`/RHI dispatch bir tüketici-entegrasyon meselesidir
  (cd::ddgi/restir_di pattern) → dürüst terminal durum CPU-ref-v1'i SEAL + precise
  "real .cpp RHI dispatch = promote-on-need when a consumer wires it" tetikleyicisi +
  gerçek dokunulmamış dalı topup. Bu band3-render-features ADR ile aynı bölünme çizgisi.
- light::ClusterGrid kararı (brief "EITHER thin adapter IF clean, ELSE SEAL
  data-only-v1"): adapter clean DEĞİL (view-space-all-XY vs world-space-tight-AABB API
  köprüsü → cluster_hits sayısal davranış değişir + hello_engine/golden riski +
  samples/ scope-ihlali) → **SEAL data-only-v1**, owner = lighting_clusters (B3 §5),
  logic duplike edilmedi. B3 ADR'ının "zero-consumer" varsayımı bu pass'te düzeltildi
  (hello_engine main.cpp:5265/8186 + HelloShowcaseProbes.hpp:1247 produksiyon tüketici).
- atmosphere "bake all 4 LUTs" (brief "OR bake a 2nd LUT if small+clean+testable"):
  multi-scatter LUT small+clean+testable DEĞİL (transmittance lookup + spherical
  integral + referans-doğrulama yok) → SEAL transmittance-v1 + 4-LUT-plan dökümante +
  promote-on-need.
- denoise "false stub banner" (brief "Correct any false 'stub' banner that
  misrepresents the working a-trous path"): a-trous path GERÇEK + çalışır (stub değil);
  README'nin `create_atrous_filter`/`apply()` örneği derlenmeyen var-olmayan API idi →
  düzeltildi. OIDN GERÇEKTEN pass-through stub → README'de açıkça öyle işaretlendi.
- "scope exclusion" kuralı uygulandı: kod değişiklikleri YALNIZ engine/render/
  {light,volumetric,atmosphere,denoise}/{tests,README} + bu docs/ADR/ dosyası.
  lighting_clusters/cluster/ibl (B3) + ibl_gpu (B6) + rhi/ + samples/ + hello_* + %
  docs DOKUNULMADI. Lib header/src davranışı (ClusterGrid logic dahil) DEĞİŞMEDİ.
- Golden byte-identical doğrulaması: yalnız test dosyaları + README'ler + bu ADR
  değişti (header binary davranışı aynı, ClusterGrid logic duplike-edilmedi/değişmedi,
  hello_engine'e dokunulmadı) → hello_engine rendering yolu etkilenmez; fixture #5
  capture baseline ile bayt-bayt eşit doğrulandı (`cmp baseline.png b4rf.png` →
  identical, b4rf.png silindi).

## Sonraki

- **Band 4 cd::editor / cd::platform / cd::particle_system / cd::asset::*
  (volumetric/atmosphere/denoise dışındaki B4)**: ayrı pass'ler (editor dominant —
  kendi multi-wave sub-plan'ı).
- **light::ClusterGrid migration** (promote-on-need): hello_engine cluster-grid
  tüketicisini (lights-panel stats) lighting_clusters::Clusterer'a port et (angular↔
  matrix API köprüsü + panel-stat davranış-eşdeğerliği + golden re-baseline) → ayrı
  migration-ADR; tüketici samples/ kapsamında olduğu için render-features pass DIŞI.
- **volumetric/atmosphere/denoise RHI dispatch** (promote-on-need): hello_engine
  fog-pass / sky-pass / path-trace-denoise entegrasyonu gerçek `.cpp` dispatch'i
  (FroxelGrid→image3D, transmittance→image2D + LUT 2-4, a-trous→ping-pong image) +
  ayrı ADR'lerle açar (cd::ddgi/restir_di pattern).
- **atmosphere LUT 2-4** (promote-on-need): multi-scatter → sky-view → aerial-
  perspective bake'leri + RHI dispatch, bir analytical-sky tüketici + referans-
  doğrulama (golden-image/SSIM) ile.
- **denoise OIDN backend** (promote-on-need): CD_ENABLE_OIDN + Intel OIDN 2.x dep +
  OidnBackend.cpp non-empty body, bir path-trace tüketici + entegrasyon-ADR ile.
