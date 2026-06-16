# ADR-20260616 — ALL-MODULES-TO-100 BAND 3 / RENDER-FEATURES Kapsam Mührü (3 kütüphane + froxel-sahiplik)

- **Status**: Accepted
- **Date**: 2026-06-16
- **Branch**: dev (HEAD 8fefa06)
- **Deciders**: Cemal TATLI
- **Author**: Developer (BAND 3 render-features subset close-out — froxel-ownership decision + seal-charter-complete + IBL edge-test-topup pass)
- **Related**:
  - `docs/ROADMAP_ALL_MODULES_TO_100.md` (BAND 3 listesi + "100% = IMPLEMENTED+test
    YA DA formally SEALED" honest-rule, §"What 100% MEANS"; lighting_clusters 75
    "DE-DUP with cd::cluster + cd::light::ClusterGrid (3× froxel binning) — pick one
    owner", ibl 72 "unify with cd::ibl_gpu; verify bakers on-GPU", cluster 70
    "DE-DUP (see lighting_clusters) — consolidate or delete the duplicate"; §Cross-band
    notes "De-dup froxel clustering … resolve ownership ONCE when the first of them is
    worked (Band 3), the others then collapse")
  - `docs/PROJECT_COMPLETION_STATUS.md` §5 (engine/render FEATURES baseline %'leri +
    Basis sütunu: lighting_clusters 75 "clip-space sphere binning + AABB-sphere tight
    test + prefix-sum + glslang cluster-cull CS w/ dispatch; 13 tests; ⚠ overlaps
    cd::cluster", ibl 72 "CPU bakers — split-sum BRDF LUT/irradiance/prefiltered
    specular/equirect→cube; 9 tests; GPU side in cd::ibl_gpu", cluster 70 "RHI two-pass
    count/write compute + CPU reference + PBR lookup; 17 tests; ⚠ duplicates
    cd::lighting_clusters froxel assign" + §5 systemic caveat "froxel light-cluster
    binning is duplicated 3× (cluster ↔ lighting_clusters ↔ light::ClusterGrid)")
  - `docs/ADR/ADR-20260616-band3-render-core-scope.md` + `docs/ADR/ADR-20260616-band3-foundation-scope.md`
    (kardeş band-mühür ADR'ları, aynı şablon: decide-the-cross-cut + impl-the-clean-gap
    + seal-the-rest + promote-on-need + targeted-edge-topup)
- **Scope guard**: Bu ADR YALNIZ kapsam-karar dokümanıdır. Mühürlenen maddeler
  "deferred-by-design"dır — açık (open) sayılmazlar. Her madde "ihtiyaç doğunca
  promote" çıkış kapısı taşır. Çapraz-kesen froxel-de-dup için TEK sahip seçildi
  (§5 Froxel-ownership); güvenli + golden-byte-identical bir konsolidasyon BU PASS'te
  mümkün olmadığı için (üç kopya FARKLI public API + ayrı tüketici/test yüzeyi taşıyor)
  duplikasyon **tracked-consolidation** olarak mühürlendi + migration planı yazıldı, böylece
  light::ClusterGrid'in Band-4 konsolidasyonu PRE-DECIDED. engine/render/ibl/tests/ +
  bu ADR DIŞINA DOKUNULMADI; engine/render/light (Band 4) + ibl_gpu (Band 6) + rhi/ +
  samples/ + hello_* + % docs DEĞİL. Chrome golden BYTE-IDENTICAL.

---

## 1. Bağlam

BAND 3 (70–79%) render-features alt-kümesi 3 kütüphaneyi 100%'e taşır + standing
3× froxel-clustering de-dup çapraz-kesenini çözer: cd::lighting_clusters (75 —
clip-space sphere binning + tight AABB-vs-sphere closest-point test + prefix-sum +
glslang `kClusterCullCS` GPU dispatch, 13 test) + cd::cluster (70 — RHI two-pass
count/write compute + CPU reference simulator + PBR lookup GLSL, 17 test) + cd::ibl
(72 — CPU split-sum bakers: BRDF LUT / irradiance / prefiltered specular /
equirect→cube, 9 test). lighting_clusters ve cluster AYNI froxel light-cluster
binning'i FARKLI public API ile iki kez uyguluyor; light::ClusterGrid (Band 4,
68) üçüncü — daha basit, data-only — bir kopya.

Roadmap honest-rule'una göre bir modül 100%'tür ancak HER dokümante boşluk iki
terminal durumdan birindeyse: (a) IMPLEMENTED + test (küçük/net/tüketicisi olan),
VEYA (b) bir paragraflık kapsam-ADR'ı ile formally SEALED (promote-on-need
tetikleyicisiyle). "TODO/placeholder" üçüncü durumu kalamaz.

Bu pass'in bölünme çizgisi:
- **froxel de-dup** çapraz-keseni: TEK sahip seçildi (lighting_clusters — tek
  PRODUKSİYON tüketicisi olan, en sıkı algoritmaya + gerçek .cpp + GPU dispatch'e
  sahip kopya). Güvenli + golden-byte-identical bir konsolidasyon (forward/delete)
  BU PASS'te mümkün DEĞİL (üç kopya farklı public API + ayrı tüketici/test taşıyor;
  cluster'ın kendi GPU-parity-test değeri var, silmek test kaybı + golden-riski
  olur) → duplikasyon **tracked-consolidation** olarak SEALED + migration planı
  (§5) yazıldı.
- **lighting_clusters** ve **cluster**: ikisi de fonksiyonel + derin test-edilmiş;
  named gap'leri (de-dup) → sahiplik kararıyla terminal duruma geçti. Yeni davranış
  eklenmedi (golden koruması).
- **ibl**: charter-complete (GPU tarafı cd::ibl_gpu, Band 6) → SEALED; ama
  bakerların gerçek test-edilmemiş edge dalları (single-mip roughness-0 ternary,
  num_mips clamp, empty-image/empty-cubemap early-out, equirect pole row-clamp,
  per-dominant-axis cubemap sampling) vardı → +6 hedefli edge test eklendi.

---

## 2. Karar — kütüphane başına bir bölüm

### 2.1 cd::lighting_clusters (render/lighting_clusters) — 75 → 100  [SEALED froxel-owner + tracked-consolidation]

- **Bağlam**: STATIC lib (`src/LightingClusters.cpp` 313 + `src/DispatchPass.cpp`).
  `Clusterer::assign(span<PointLight world>, float[16] view_proj)` → clip-space
  sphere binning + log-z slice + tight per-cluster **AABB-vs-sphere closest-point
  test** (`cluster_sphere_overlap`, over-assignment'i kıran gerçek tight test) +
  prefix-sum LightAssignment. `DispatchPass` Sprint-2 GPU yolu: `kClusterCullCS`
  glslang compute + cluster_table/light_indices SSBO + ceil(dim/8) dispatch.
  `PUBLIC_DEPS cd::rhi`, `PRIVATE_DEPS cd::shader` (restir_di/ddgi pattern).
  13 gtest (8 CPU + 5 dispatch). **PRODUKSİYON TÜKETİCİSİ var**: editor
  `cd::editor::panel_light_editor::LightEditor` `set_lights(span<PointLight>)` +
  `set_grid(ClusterGrid)` ile bu lib'in tipleri üzerinden çalışıyor.
- **Karar (DECIDE froxel owner — brief §Froxel-ownership; bkz. §5)**:
  **cd::lighting_clusters = froxel-clustering SAHİBİ.** cluster ve light::ClusterGrid
  bu sahibe migrate edilecek (light Band 4'te, §5 migration planı). Konsolidasyon
  BU PASS'te golden-byte-identical biçimde güvenle YAPILAMADIĞI için (cluster'ın
  ayrı public API'si + GPU-parity test değeri + zero-consumer durumu) duplikasyon
  **tracked-consolidation** olarak SEALED. lighting_clusters'ın kendisi
  charter-complete: CPU + GPU yolları IMPLEMENTED + test-dolu, davranış DEĞİŞMEDİ.
- **Gerekçe**: lighting_clusters üç kopya arasında sahip-olmaya en uygun: (1) tek
  GERÇEK produksiyon tüketicisi olan (editor LightEditor) — diğer ikisinin link
  tüketicisi YOK (sadece kendi testleri); (2) en SIKI algoritma — gerçek
  AABB-vs-sphere closest-point test (cluster yalnız açısal envelope ile
  over-assign eder; light yalnız tüm XY tile'ları Z-slice başına ekler, hiç XY
  cull yok); (3) gerçek .cpp + GPU `DispatchPass` (cluster'ın GpuPipeline'ı parity
  amaçlı, light header-only data-only); (4) GPU-ready froxel layout (world-space
  PointLight + view_proj matrix API, GPU SSBO'ya birebir map). Davranış eklenmediği
  için golden bayt-aynı.
- **Promote-on-need**: cluster + light::ClusterGrid'in lighting_clusters'a fiili
  migrasyonu (cluster'ın 3 GLSL tüketicisinin — cluster_pbr lookup, cluster_gpu
  dispatch — froxel API'sine port'u; light::ClusterGrid'in çağıranlarının
  Clusterer'a geçişi) gerçek bir Forward+ render-yolu tüketicisi (hello_engine'in
  many-light sahnesini froxel-cull'a bağlayan entegrasyon) + ayrı migration-ADR ile
  gelir. Clusterer/LightAssignment/DispatchPass yüzeyi sahip olarak sabit kalır.

### 2.2 cd::cluster (render/cluster) — 70 → 100  [SEALED tracked-consolidation-duplicate]

- **Bağlam**: 4 alt-hedef. `cd::cluster` (INTERFACE — `ClusterGrid.hpp` view-space
  angular/depth API + `ReferenceCompute.hpp` GLSL-parity CPU simulatörü) +
  `cd::cluster_gpu` (STATIC — `GpuPipeline.cpp` 506, Vulkan compute iki-pass
  count/prefix-sum/write + readback, ReferenceOutput parity) + `cd::cluster_pbr`
  (INTERFACE — Forward+ PBR fragment `#include` GLSL kontratı) + `cd::cluster_fx`
  (INTERFACE umbrella). 17 gtest (13 core + 4 pbr). **PRODUKSİYON LİNK TÜKETİCİSİ
  YOK**: hiçbir CMake hedefi cd::cluster*'a bağlı değil; sadece kendi testleri
  (`cd_test_cluster`/`_fx`/`_pbr`) link ediyor. lighting_clusters'taki tek referans
  bir naming-discipline YORUMU (#include değil).
- **Karar (DECIDE consolidate-or-delete — brief §cluster)**: cluster, froxel
  sahibinin (§2.1 lighting_clusters) **takip-edilen-duplikatı** olarak SEALED.
  SİLİNMEDİ + lighting_clusters'a transparent forward EDİLMEDİ. Gerekçe (brief
  "IF a safe consolidation exists that keeps golden BYTE-IDENTICAL … DO it; ELSE …
  DOCUMENT + SEAL"): güvenli konsolidasyon BU PASS'te yok çünkü —
  - **Farklı public API**: cluster `ClusterConfig{cells, fov_y, aspect, near, far}`
    + view-space angular (`atan2`) binning; lighting_clusters
    `ClusterGrid{tiles, near, far}` + view_proj matrix + NDC binning. Transparent
    forward bir API-köprüsü ister (angular ↔ matrix dönüşümü), bu yeni davranış +
    yeni test yüzeyi getirir, "minimal/golden-safe" değildir.
  - **Ayrı değer**: cluster'ın `GpuPipeline` + `ReferenceCompute` GPU-CPU parity
    kapağı (cluster_assign.comp'un host-doğrulaması) gerçek bir test varlığı; silmek
    17 testi + GPU-parity güvencesini kaybettirir.
  - **Zero-consumer + golden-risk**: cluster'ı silmek/değiştirmek hiçbir produksiyon
    tüketicisini etkilemez AMA install export-set + cluster_fx umbrella + 3 test
    binary'sini kırar; lib'i olduğu gibi bırakmak golden'ı garanti bayt-aynı tutar.
  → Duplikasyon **tracked-consolidation** olarak SEALED; migration planı §5'te.
- **Gerekçe**: cluster fonksiyonel + 17-test-dolu + parity-doğrulanmış; named gap'i
  (de-dup) sahiplik kararıyla terminal duruma geçti. Davranış/test değiştirilmedi →
  golden bayt-aynı + 17 test yeşil kalır.
- **Promote-on-need**: cluster'ın 3 GLSL kontrat tüketicisinin (cluster_pbr lookup,
  cluster_gpu dispatch, cluster_fx umbrella) lighting_clusters froxel API'sine
  fiili port'u → migration-ADR (§5). Eğer cluster'ın angular-binning + GPU-parity
  test'i bir gün gerçekten gereksizse delete-pass ayrı bir temizlik-ADR'ı ile gelir
  (bu pass'te golden-riski + test-kaybı nedeniyle YAPILMADI).

### 2.3 cd::ibl (render/ibl) — 72 → 100  [SEALED charter-complete-CPU-baker + edge-test-topup]

- **Bağlam**: INTERFACE header-only lib (`cd::core` + `cd::math` only). CPU
  split-sum bakers: `bake_brdf_lut` (Hammersley + GGX importance sample + Smith G,
  scale/bias RG LUT) + `convolve_irradiance` (hemisphere quadrature, cos-weighted
  Lambert + sin-θ Jacobian) + `prefilter_specular` (per-mip GGX-IS environment
  radiance, Karis V=R=N) + `equirect_to_cube`/`sample_equirect` (atan2/asin equirect
  projeksiyon + bilinear) + `cube_uv_to_world_dir`/`sample_cubemap_dir` (dominant-axis
  face seçimi + bilinear). 9→15 gtest.
- **Karar (DECIDE unify-with-ibl_gpu / verify-on-GPU — brief §ibl)**: ibl
  **charter-complete CPU-baker olarak SEALED**. ibl bilinçli olarak SADECE CPU
  bake tarafıdır (README: "CPU baker; companion library cd::ibl_gpu uploads results
  to RHI textures"); GPU yükleme/doğrulama cd::ibl_gpu'nun (Band 6, 48 — 0 test)
  charter'ıdır → "verify bakers on-GPU" Band-6 cd::ibl_gpu işidir, bu pass'in DEĞİL
  (ibl_gpu SCOPE EXCLUSION). ibl↔ibl_gpu sınırı: ibl `CubeMapRgbF`/`BrdfLut`/
  `PrefilteredSpecularCube` POD'ları üretir (host RAM); ibl_gpu bunları RHI cubemap/
  prefiltered/BRDF-LUT texture + sampler'a yükler (float→half, barrier'lar). Birleştirme
  (unify) = ibl_gpu'nun ibl POD'larını doğrudan tüketmesi — zaten öyle (README +
  HelloIbl boot-bake); bir kod-merge GEREKMEZ (iki ayrı katman: CPU-bake vs
  GPU-upload, doğru ayrım). Bu pass yalnız gerçek test-edilmemiş baker dallarına +6
  edge test ekledi:
  - **PrefilteredSpecular.SingleMipUsesRoughnessZeroBranch**: `num_mips <= 1 ? 0.0F`
    ternary'si (aksi hâlde `(num_mips-1)==0` bölme) — önceki tek prefilter testi
    `mips=3` ile bu dalı hiç koşmamıştı; roughness-0 (mirror) sonlu+pozitif.
  - **PrefilteredSpecular.RequestedMipsClampToMax**: `std::min(num_mips,
    kMaxSpecularMips)` clamp'i — fixed-size mips array overrun'a karşı (num_mips >
    cap → mip_count == cap).
  - **EquirectToCube.EmptyImageSamplesToBlack**: `sample_equirect` width/height==0
    early-out (→ {0,0,0}).
  - **EquirectToCube.PoleSamplingClampsRowAndStaysFinite**: straight-up/down dir →
    v 0/1 satır sınırı → `clamp_y` top+bottom kenar dalı; θ=±π/2'de NaN/wraparound
    yok + row-gradient ile pole satırları distinct+sonlu.
  - **Cubemap.EmptyCubemapSamplesToBlack**: `sample_cubemap_dir` face_size==0
    early-out (prefilter/irradiance bakerların dejenere-input güvencesi).
  - **Cubemap.SampleDirHitsEachDominantAxisFace**: x/y/z dominant-axis dallarının
    ÜÇÜ de (her iki işaret) — 6 yüz unique tint + merkez-dir read-back (önceki
    cubemap testi yalnız +X yüzü round-trip ediyordu).
- **Gerekçe**: ibl CPU-baker charter'ı TAM (4 split-sum adımı IMPLEMENTED + temel
  test-dolu); GPU doğrulama ibl_gpu'nun (Band 6) işi → SEAL. Eklenen 6 test gerçek
  boşluktu: single-mip ternary, mip-clamp, empty-image/cubemap early-out, pole
  row-clamp, ve per-dominant-axis sampling önceki 9 test tarafından hiç
  tetiklenmemişti. W8-AW kalibre IBL bake parametreleri (env 128 / spec 128·6·32 /
  diff 16·16 / brdf 64·256) + irradiance float-counter loop'ları (cert-flp30-c
  NOLINT) DOKUNULMADI — eklenen testler küçük dejenere fixture'larla correctness
  dallarını sınar, bake çıktısını DEĞİŞTİRMEZ ("DON'T regenerate IBL bake" kuralı).
- **Promote-on-need**: ibl_gpu'nun on-GPU baker-parity doğrulaması (CPU bake ↔ GPU
  compute-shader bake'in golden-image/SSIM eşitliği) Band 6 cd::ibl_gpu işidir +
  ayrı ADR ister. ibl CPU baker public yüzeyi (bake_brdf_lut/convolve_irradiance/
  prefilter_specular/equirect_to_cube) sabit kalır.

---

## 3. Reddedilen alternatifler

- **cluster'ı silmek / lighting_clusters'a transparent forward etmek (seal yerine)**:
  cluster'ın ayrı public API'si (angular ClusterConfig vs view-proj ClusterGrid) bir
  API-köprüsü ister → yeni davranış + yeni test yüzeyi + golden-riski; cluster'ın
  17-test GPU-parity kapağı silinirse test-kaybı + cluster_fx/install export-set
  kırılır. RED — "ELSE DOCUMENT + SEAL as tracked-consolidation" clause'u (brief)
  tam bunun için; golden bayt-aynı kalır.
- **froxel sahibini cluster seçmek**: cluster'ın produksiyon link-tüketicisi YOK
  (sadece kendi testleri); lighting_clusters editor LightEditor tarafından tüketiliyor.
  cluster yalnız açısal-envelope ile over-assign eder (gerçek tight AABB cull yok);
  lighting_clusters gerçek closest-point AABB-vs-sphere test yapar. RED — sahip en
  sıkı + tek-produksiyon-tüketicisi-olan + gerçek-.cpp+GPU-dispatch kopya olmalı.
- **froxel sahibini light::ClusterGrid seçmek**: light::ClusterGrid en BASİT kopya
  (header-only, data-only, hiç XY cull yok — Z-slice başına TÜM XY tile'ları ekler,
  fixed-size 32-cell overflow), Band 4'te + lighting_clusters'tan daha az yetenekli.
  RED — sahip Band 3'te + en kapasiteli kopya olmalı; light ona migrate EDER (§5).
- **ibl'i ibl_gpu ile kod-merge etmek / GPU baker-parity'yi bu pass'te eklemek**:
  ibl CPU-bake, ibl_gpu GPU-upload — doğru katman ayrımı (merge katman ihlali olur).
  On-GPU parity ibl_gpu'nun (Band 6, SCOPE EXCLUSION) işi + RHI device + golden-image
  test-harness ister. RED — ibl charter-complete SEAL; sınır §2.3'te dökümante.
- **W8-AW IBL bake parametrelerini / float-counter irradiance loop'larını
  değiştirmek**: bunlar chrome-mirror golden'a kalibre + cert-flp30-c NOLINT'li
  ("DON'T regenerate IBL bake" marathon kuralı). RED — eklenen testler dejenere
  fixture correctness'ini sınar, bake davranışını DEĞİŞTİRMEZ → golden bayt-aynı.
- **Üç kütüphanenin charter-complete yüzeyini pad etmek**: lighting_clusters
  CPU+GPU yolu + cluster core/pbr + ibl temel bake zaten round-trip/parity test-doluydu.
  Yapay test "add tests ONLY for a genuinely-untested branch … don't pad" kuralına
  aykırı. RED — yalnız gerçekten dokunulmamış ibl baker dalları (single-mip ternary,
  mip-clamp, empty-early-out, pole-clamp, per-axis-sampling) hedeflendi.
- **engine/render/light'ı (Band 4) / ibl_gpu'yu (Band 6) / rhi'ı / samples'ı /
  hello_*'ı / % docs'u düzenlemek**: kapsam DIŞI (brief: SCOPE EXCLUSION — ONLY
  engine/render/{lighting_clusters,cluster,ibl}/ + docs/ADR/). light::ClusterGrid'in
  fiili migrasyonu §5 migration planında PRE-DECIDED ama Band 4 işidir. RED.

## 4. Sonuçlar

- (+) 3/3 BAND-3 render-features alt-küme kütüphanesi honest-rule terminal durumuna
  geçti: lighting_clusters SEALED froxel-OWNER (charter-complete CPU+GPU, tek
  produksiyon-tüketicili en sıkı kopya); cluster SEALED tracked-consolidation-duplicate
  (fonksiyonel + 17-test + GPU-parity korundu, migrate-later); ibl SEALED
  charter-complete-CPU-baker (GPU tarafı ibl_gpu/Band 6). Hiçbir kütüphanede
  placeholder/TODO yok (zaten yoktu).
- (+) **FROXEL OWNERSHIP DECISION (çapraz-kesen, brief'in ana item'ı)**: TEK sahip =
  **cd::lighting_clusters**. Güvenli golden-byte-identical konsolidasyon bu pass'te
  mümkün olmadığı için (üç kopya farklı public API + ayrı test/tüketici) duplikasyon
  **tracked-consolidation** olarak DÖKÜMANTE + SEALED; migration planı §5'te → Band 4
  light::ClusterGrid konsolidasyonu PRE-DECIDED (light Clusterer'a migrate eder).
- (+) 6 yeni IBL edge test, gerçek test-edilmemiş baker dalında:
  PrefilteredSpecular single-mip-roughness-0-ternary + mip-clamp, EquirectToCube
  empty-image-early-out + pole-row-clamp, Cubemap empty-early-out + per-dominant-axis-
  sampling. Hepsi edge/negative/contract + fail-on-revert; anti-flakiness korundu
  (deterministik dejenere fixture'lar, sleep_for YOK). cd_test_ibl 9→15 case, yeşil.
- (+) Chrome golden BYTE-IDENTICAL (fixture #5, baseline ile `cmp` eşit). Üç
  kütüphanede de SADECE ibl test-dosyası değişti + ADR (lib header/src davranışı
  aynı, lighting_clusters/cluster hiç dokunulmadı) → hello_engine render-yolu bayt-aynı.
  Build -Werror temiz, 0 yeni clang-tidy WAE class (eklenen test kodu:
  emplace yok-gereği/push_back yok, using-namespace yok, unused-using yok).
- (−) Mühürler cluster+light::ClusterGrid'in lighting_clusters'a FİİLİ migrasyonunu
  ve ibl_gpu on-GPU baker-parity doğrulamasını bu pass'te ÜRETMEZ; ilki Band 4
  light + migration-ADR ile, ikincisi Band 6 ibl_gpu + ADR ile gelir. Kabul: BAND 3
  render-features alt-kümesi "decide-the-cross-cut + seal-tracked-consolidation +
  charter-complete-seal + IBL-edge-topup" karakterinde — duplikasyon fonksiyonel,
  sadece tek-sahibe-henüz-collapse-edilmemiş (golden-safe migration consumer-bağlı).

---

## 5. Froxel-ownership decision (çapraz-kesen — light::ClusterGrid B4 pre-decision)

### Sahip (OWNER): **cd::lighting_clusters**

Üç froxel light-cluster binning kopyası — tek sahip seçim matrisi:

| Kriter | lighting_clusters (75) | cluster (70) | light::ClusterGrid (68, B4) |
|---|---|---|---|
| Produksiyon link-tüketici | **EVET** (editor LightEditor) | YOK (yalnız kendi testleri) | YOK (yalnız kendi testi) |
| XY cull sıkılığı | **tight AABB-vs-sphere closest-point** | açısal envelope (over-assign) | YOK (Z-slice başına TÜM XY) |
| Gerçek .cpp | EVET (LightingClusters.cpp + DispatchPass.cpp) | kısmen (GpuPipeline.cpp; core header-only) | YOK (header-only) |
| GPU dispatch | **EVET** (`kClusterCullCS` DispatchPass) | parity-pipeline (GpuPipeline) | YOK (data-only) |
| GPU-ready layout | **EVET** (world PointLight + view_proj → SSBO) | view-space angular | fixed-size 32-cell + overflow |
| Test | 13 (CPU+dispatch) | 17 (core+pbr, GPU-parity) | light suite içinde |
| Band | **3** | 3 | 4 |

→ **cd::lighting_clusters** sahip: tek produksiyon-tüketicisi olan, en sıkı XY cull'lı,
gerçek .cpp + GPU dispatch + GPU-ready froxel layout taşıyan kopya.

### Konsolidasyon durumu: TRACKED-CONSOLIDATION (functional, migrate-later)

Güvenli golden-byte-identical konsolidasyon BU PASS'te uygulanmadı çünkü üç kopya
FARKLI public API + ayrı tüketici/test yüzeyi taşıyor; transparent forward/delete
yeni davranış + golden-riski + test-kaybı getirirdi. Üçü de fonksiyonel kalır;
duplikasyon takip-edilen-konsolidasyon olarak mühürlendi.

### Migration planı (promote-on-need; light::ClusterGrid B4 PRE-DECIDED)

1. **cluster → lighting_clusters** (zero-consumer, düşük risk): cluster'ın 3 GLSL
   kontrat tüketicisini (cluster_pbr `cluster_lookup_glsl` Forward+ fragment helper,
   cluster_gpu `GpuPipeline` two-pass dispatch, cluster_fx umbrella) lighting_clusters
   froxel API'sine (DispatchPass `kClusterCullCS` + LightAssignment) port et. cluster'ın
   `ReferenceCompute` GPU-CPU parity testini lighting_clusters dispatch testine taşı.
   Tamamlanınca cluster + alt-libs delete (ayrı temizlik-ADR; install export-set +
   cluster_fx umbrella güncellemesi). Tetikleyici: ikinci bir froxel tüketicisi doğunca
   ya da maintenance-hazard temizliği planlanınca.
2. **light::ClusterGrid → lighting_clusters** (Band 4, PRE-DECIDED bu ADR ile):
   light::ClusterGrid (header-only, data-only, XY-cull-yok, fixed-32-cell) Band 4'te
   ya (a) lighting_clusters::Clusterer'a delegate eden ince bir adapter olur (light'ın
   `assign(light_index, Light, view_space_pos)` imzası korunur, içeride Clusterer'a
   forward), ya da (b) çağıranları doğrudan Clusterer'a taşınır + ClusterGrid.hpp
   deprecate edilir. light'ın Band-4 hedefi roadmap'te zaten "move ClusterGrid into
   the single froxel owner; add .cpp + tests" — bu ADR o sahibi (lighting_clusters)
   ve yönü (light → owner, owner ≠ light) KESİNLEŞTİRİR. light Band 4 işçisi bu §5'i
   pre-decision olarak okur; froxel-owner'ı yeniden tartışmaz.

Golden her iki migrasyonda da bayt-aynı KALMALI (fixture #5 cmp baseline) — migration
davranış-eşdeğer olmalı (aynı cluster→light-set ataması) ya da consumer'ı olmayan
ölü-API temizliği olmalı.

---

## Varsayımlar

- Brief'in "100% RULE per lib" yorumu: çapraz-keseni (froxel de-dup) bir kez karar
  bağla (tek sahip), güvenli golden-safe konsolidasyon varsa yap yoksa
  tracked-consolidation olarak seal + migration planı yaz; charter-complete'i seal +
  gerçek edge boşluğunu test et — bu pass'in bölünme çizgisi (band3-foundation +
  band3-render-core ADR ile aynı). Üç kopya farklı public API + cluster zero-consumer
  + golden-riski → konsolidasyon DOCUMENT+SEAL (DO değil); ibl charter-complete +
  GPU tarafı Band 6 → SEAL + IBL edge-topup.
- "DE-DUP — pick one owner" (roadmap lighting_clusters+cluster ifadesi): owner =
  lighting_clusters (tek produksiyon-tüketicili + en sıkı + GPU-dispatch'li kopya);
  brief'in "likely cluster (RHI two-pass GPU path) or lighting_clusters — whichever is
  more complete/consumed" yönlendirmesi → "consumed" (editor) + "complete" (tight AABB
  + .cpp + GPU dispatch) ikisi de lighting_clusters'a işaret etti. cluster'ın "RHI
  two-pass" yolu parity-test amaçlı (produksiyon değil) → consumed kriteri belirleyici.
- "unify with cd::ibl_gpu; verify bakers on-GPU" (roadmap ibl ifadesi): unify = zaten
  var (ibl_gpu ibl POD'larını tüketiyor — README + HelloIbl boot-bake; kod-merge
  katman ihlali olurdu); "verify on-GPU" Band 6 cd::ibl_gpu işidir (0-test lib,
  SCOPE EXCLUSION) → ibl tarafı charter-complete SEAL + sınır §2.3'te dökümante,
  on-GPU doğrulama ibl_gpu promote-on-need.
- "scope exclusion" kuralı uygulandı: TEK kod değişikliği
  engine/render/ibl/tests/test_ibl.cpp (+6 edge test) + bu docs/ADR/ dosyası.
  lighting_clusters + cluster header/src/test DOKUNULMADI (sahiplik kararı dökümantasyon —
  davranış değişmedi); engine/render/light (Band 4) + ibl_gpu (Band 6) + rhi/ +
  samples/ + hello_* + % docs DOKUNULMADI.
- Golden byte-identical doğrulaması: yalnız ibl test dosyası değişti (header binary
  davranışı aynı) + lighting_clusters/cluster hiç dokunulmadı → hello_engine rendering
  yolu etkilenmez; fixture #5 capture baseline ile bayt-bayt eşit doğrulandı
  (`cmp baseline.png b3rf.png` → identical, b3rf.png silindi).

## Sonraki

- **Band 4 cd::light** (68 → 100): §5 migration planı adım 2'yi uygula —
  light::ClusterGrid'i lighting_clusters::Clusterer'a (sahip) delegate eden adapter
  ya da çağıran-migrasyonu + ClusterGrid.hpp deprecate; froxel-owner ZATEN KARARLI
  (bu ADR §5), yeniden tartışılmaz. light .cpp + tests ekle (roadmap B4 hedefi).
- **cluster delete-pass** (promote-on-need): §5 adım 1 — cluster'ın 3 GLSL tüketicisi
  lighting_clusters'a port edildikten + ReferenceCompute parity testi taşındıktan sonra
  cluster + cluster_gpu/pbr/fx delete (install export-set + umbrella temizliği) ayrı
  temizlik-ADR ile. İkinci froxel tüketici ya da maintenance-hazard temizliği tetikler.
- **Band 6 cd::ibl_gpu** (48 → 100, 0-test): ibl CPU bake ↔ ibl_gpu GPU compute-bake
  parity'sini golden-image/SSIM ile doğrula (ibl charter §2.3'te tanımlı sınır) + RHI
  upload helper'larını on-GPU test et; ibl POD yüzeyi sabit (geriye-uyumlu).
