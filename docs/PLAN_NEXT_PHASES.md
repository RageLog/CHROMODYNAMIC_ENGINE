# CHROMODYNAMIC — Sonraki Fazlar (Aşırı Detaylı Plan)

> Durum: **122/122 cd:: kütüphanesi gerçek %100** (commit `34d9e303`).
> Bu belge, motorun 100'den SONRA gideceği yolu fazlara böler. Her faz:
> **Hedef · Ön-koşul · Adımlar (dosya ref'leriyle) · Kabul kriteri (gate) · Risk · Efor**.
> Tahmini efor: S=saatler, G=günler, H=haftalar.
>
> **Sıralama mantığı:** Faz 0 (temizlik, hemen) → Faz 1 (Metal pariteleri, en büyük) →
> Faz 2 (Phase-2 mimari) → Faz 3 (sürüm/paketleme) → Faz 4 (görsel/içerik cilası).
> Faz 0 ve Faz 3 birbirinden bağımsız; Faz 1 ve Faz 2 paralel ilerleyebilir.

---

## GENEL KALİTE KAPISI (her faz, her checkpoint)

Hiçbir satır bu kapıyı geçmeden "yeşil" sayılmaz:

1. **Derleme:** `cmake --build --preset ninja-debug -- -k 0` → 0 hata, `-Wall -Werror`.
2. **Testler:** `ctest --preset ninja-debug --output-on-failure` → %100, gerçek Vulkan+D3D12 cihazları dahil.
3. **Golden bayt-bayt aynı:** `cd_test_rt_chrome_sphere_reflects_geometry`, `cd_test_hello_engine_golden_cli`, `cd_test_sample_sponza_golden`.
4. **clang-tidy WAE = 0:** `clang-tidy -p build/ninja-base --header-filter ...` ile ~209 kural temiz.
5. **Her yeşil checkpoint = ANINDA commit.** Tag/push YOK (kullanıcı açıkça istemedikçe).
6. **Agent disiplini:** agent'lar EDIT-ONLY; build/test kapısını orkestratör tek noktadan çalıştırır.
   İstisna: iteratif GPU/cihaz debug'ı (build erişimi verilebilir, ör. Dawn/Metal cihaz testleri).

---

# FAZ 0 — Temizlik & Sağlamlaştırma (hemen, düşük risk)

**Hedef:** Marathon + bug-hunt sonrası kalan küçük borçları kapatıp tabanı sağlamlaştırmak.
**Ön-koşul:** Yok. Hemen başlanabilir.

### 0.1 — WgpuContract format-kod uzlaştırma  (S, düşük risk)
- **Sorun:** [WgpuContract.hpp](../engine/ui/renderer_webgpu/include/cd/ui/renderer_webgpu/WgpuContract.hpp)
  içindeki `texture_format::*` / `vertex_format::*` sayısal kodları "spec-stable" diye
  belgelenmiş ama bu Dawn sürümünün enum değerleriyle birebir tutmuyor; Submitter şu an
  isimle (`to_wgpu_vertex_format`/`to_wgpu_color_format`) eşliyor.
- **Adım:** Kodları kurulu Dawn enum'larıyla doğrula; ya yorumları "isimle eşlenir" diye düzelt
  ya da kodları gerçek değerlere getir. Mevcut host-side testlerin bu kodları pinlediğine dikkat —
  testleri de güncelle.
- **Kabul:** Dawn-OFF default build yeşil + Dawn-ON readback testi yeşil + host contract testleri pin'li.

### 0.2 — ImGui sürüm yorumları 1.92'ye güncelle  (S, kozmetik)
- **Sorun:** [Context.cpp:194-195](../engine/imgui_backend/src/Context.cpp) ve test başlıkları
  "ImGui 1.90+/1.91.9, lazy CreateFontsTexture" diyor; gerçek vendored sürüm **1.92.9 WIP**
  (dinamik font-atlas + `RendererHasTextures`). Stale-sürüm tuzağı.
- **Adım:** Doc-only güncelleme; davranış değişikliği yok.

### 0.3 — Build/Dawn opt-in dökümantasyonu  (S)
- **Adım:** [docs/BUILDING.md](BUILDING.md) içine "WebGPU/Dawn nasıl açılır" bölümü:
  `vcpkg install --x-feature=webgpu` + `cmake --preset ninja-base -DVCPKG_MANIFEST_FEATURES=webgpu -DCD_ENABLE_WEBGPU=ON`.
  Dawn DLL'inin test exe'sinin yanına POST_BUILD kopyalandığını not et.
- **Kabul:** Temiz makinede adımları izleyen biri Dawn-ON tier'ı kurabilmeli.

### 0.4 — LOW-severity bug-hunt bulguları (opsiyonel, dürüstçe ertelenebilir)
Bug-hunt'ta CLEAN çıkan ama LOW not edilen iki iyileştirme — düzeltme zorunlu değil, isteğe bağlı:
- **particle_system:** compaction bloğunu integrate/expire döngüsünden SONRAYA al
  ([ParticleSystem.cpp](../engine/world/particle_system/src/ParticleSystem.cpp) ~137-142) →
  aynı-tick ölen emitter'ı bir tick erken geri kazanır. + cap-skip accumulator semantiğini pinleyen test.
- **texture_streamer:** budget-underflow "self-heal" edge'i (alloc fail sonrası) — dökümante, fix opsiyonel.

### 0.5 — Pre-existing kırmızı taraması (sağlamlaştırma, S-G)
- **Mantık:** `rhi_vulkan` gate'ten hep hariçti ve gizli bir kırmızı barındırıyordu (phase1272'de bulundu).
  Aynı sınıf başka gizli kırmızı var mı?
- **Adım:** TÜM test ikililerini (hariç tutulanlar dahil) gerçek cihazlarda bir kez koş; ctest'e
  kayıtlı olmayan/disabled testleri grep'le; hepsini yeşile getir veya dürüstçe SKIP-gate'le.
- **Kabul:** `ctest -N` listesi = gerçekten koşulan + her biri PASS/SKIP, sessiz hariç-tutma yok.

**FAZ 0 GATE:** Tam suite yeşil (iki config), golden bayt-aynı, WAE 0. 1 commit.

---

# FAZ 1 — Metal Backend Paritesi (DÜŞÜK ÖNCELİK — donanım-bloke, en büyük cephe, çok-haftalık)

> **Öncelik kararı (2026-06-22):** Metal bu Windows host'unda doğrulanamaz (sadece edit-only).
> Bu yüzden DÜŞÜK ÖNCELİK; Faz 2'den SONRA, Apple sanal-makine/bulut sağlanınca yürütülür.
> Shader yolu = **cd::gluon SL→MSL**; köprü = **Metal-cpp (C++)**, fallback ObjC++ (kararlar yukarıda).

**Hedef:** Metal RHI'yi Vulkan/D3D12 paritesine çekmek — "3 backend tam parite" standing direktifinin
son ayağı (Vulkan TAM → D3D12 olgun → **Metal**).
**Ön-koşul (KRİTİK):** Bir **Apple makinesi veya Apple CI runner** (macOS + Xcode + Metal SDK).
Bu Windows host'unda Metal **derlenemez/doğrulanamaz** → kanıt-temelli kural gereği zorlanamaz.
**Bu faz, donanım sağlanana kadar BLOKE.** Aşağısı, donanım gelince izlenecek alt-faz haritası.

> **Mevcut Metal durumu:** [engine/render/rhi/src/metal/](../engine/render/rhi/src/metal/) ~1.800 satır
> (Vulkan'ın ~%22'si), ~57 stub/`kNotImplemented`. İskelet var, et yok.

### 1.0 — Donanım & CI ön-koşulu  (G, dış bağımlılık)
- macOS runner (self-hosted Mac mini/Studio veya bulut macOS CI).
- vcpkg macOS triplet (`arm64-osx` / `x64-osx`) + Metal-cpp veya doğrudan Objective-C++ köprü.
- **Karar gerekiyor (sen):** Fork-A (Metal-cpp header-only) mu yoksa ObjC++ `.mm` köprü mü?
  ADR §8.5'te "READY-TO-START pending user Fork-A sign-off" notu var → bu kararı netleştir.

### 1.1 — Cihaz & komut altyapısı  (H)
- `MTLDevice`/`MTLCommandQueue`/`MTLCommandBuffer` → cd::rhi `IDevice`/`ICommandBuffer` map.
- `MTLSharedEvent` → timeline semaphore paritesi (Vulkan timeline ile aynı kontrat).
- **Kabul:** Vulkan'daki `VulkanDevice.*` temel testlerinin Metal karşılığı yeşil (cihaz oluştur,
  command buffer begin/end, submit, wait_idle, fence/event).

### 1.2 — Pipeline & shader  (H)
- `MTLRenderPipelineState`/`MTLComputePipelineState` + `MTLLibrary` (MSL'den).
- **Shader yolu kararı (sen):** SPIRV-Cross → MSL mı, yoksa yeni **cd::gluon (SL)** → MSL backend mi?
  (cd::gluon phase1133'te aktif edildi; Metal MSL emit eklenebilir.)
- **Kabul:** "compiled-GLSL-triangle" Metal eşi çiziyor + golden parity (FLIP/SSIM) Vulkan ile eşleşiyor.

### 1.3 — Descriptor / argument buffer & bindless  (H)
- Metal argument buffers (Tier 2) → bindless texture dizisi (Vulkan VK_EXT_descriptor_indexing eşi).
- **Hafıza notu:** bindless'in 7-katmanlı checklist'i Vulkan'da silent device-lost'a yol açmıştı
  ([[vulkan-bindless-multi-layer-checklist]]); Metal'de argument-buffer encode + residency set + heap
  aynı dikkatle ele alınmalı.
- **Kabul:** chrome-sphere RT yansımasının Sponza texture detayı Metal'de de görünüyor (parity golden).

### 1.4 — Ray tracing  (H)
- `MTLAccelerationStructure` (BLAS/TLAS) + intersection function tables / Metal ray query.
- **Hafıza notu:** BLAS multi-geometry cap'i scene prim count ile eşleşmeli
  ([[blas-geo-cap-match-scene-prim-count]]) — Metal BLAS build'inde de aynı tuzak.
- **Kabul:** RT yansıma/gölge golden'ları Metal'de Vulkan ile parity.

### 1.5 — Readback & parity capstone  (G)
- Image readback API (D3D12'de phase X4-B'de kapandı: src_state + de-pitch) Metal eşi.
- **Kabul:** D3D12 parity_m4 testinin Metal karşılığı + cross-backend pixel-parity matrisi yeşil.

**FAZ 1 GATE (donanım varken):** Metal test ikilileri gerçek Apple GPU'da yeşil; cross-backend
golden parity (Vulkan==D3D12==Metal, FLIP eşiği altında); `kNotImplemented` sayısı yalnızca dürüst
donanım-yetenek-kapıları kalacak şekilde düşürülmüş; her alt-faz ayrı commit + ADR.

---

# FAZ 2 — Phase-2 Mimari (paralel, GPU-yoğun)

**Hedef:** [docs/ROADMAP_PHASE_2.md](ROADMAP_PHASE_2.md)'deki X-serisi + CPU-demo'ların gerçek GPU dispatch'e taşınması.
**Ön-koşul:** Vulkan (var). Bazı alt-işler D3D12 parity ister (var).

### 2.1 — X5: On-disk shader hot-reload  (G-H)
- ADR phase ~991-1000'de yazıldı; implementasyonu tamamla: dosya-izleyici → recompile → pipeline reload.
- **Tuzak (hafıza):** `USE_ON_DISK_SHADERS=ON` exe-yanı kopya POST_BUILD relink-only; shader-only edit
  stale bırakır → sahte byte-identical golden ([[ondisk-shader-golden-staleness]]). Golden öncesi
  SOURCE==EXE-yanı force-sync.
- **Kabul:** shader dosyasını editle → uygulama canlı yeniden derleyip yansıtıyor; stale-golden tuzağı testle kapalı.

### 2.2 — GI sample wiring: CPU-demo → gerçek GPU dispatch  (H)
- DDGI / ReSTIR-DI / ReSTIR-GI / NRC: bu kütüphanelerin CPU-referans + sealed-GPU-dispatch deseni var.
  hello_engine içindeki CPU-demo'ları gerçek compute dispatch'e bağla (bind layout + dispatch + composite seam).
- **Sıra (önerilen):** önce DDGI (en olgun), sonra ReSTIR-DI, sonra ReSTIR-GI/NRC.
- **Kabul:** her biri için gerçek-GPU golden + perf bütçesi; CPU-referansla sayısal yakınlık testi.

### 2.3 — X1-FU kalan adımlar  (G)
- ROADMAP'teki X1-FU-A/D/C/F'nin tamamlanmamış parçaları (byte-parite kontrolleri).
- **Kabul:** cross-backend byte-parite testleri yeşil.

**FAZ 2 GATE:** Her alt-özellik gerçek-GPU golden + perf bütçe; CPU-referans parity; ADR; commit.

---

# FAZ 3 — Sürüm & Paketleme Mühendisliği

**Hedef:** Motoru dağıtılabilir, sürümlenebilir, kurulabilir hale getirmek.
**Ön-koşul:** Faz 0 (temiz taban). Sürüm etiketi **kullanıcı onayı ister**.

### 3.1 — SemVer + changelog + tag  (S, KULLANICI ONAYI)
- Conventional-commits → changelog üret. `release-manager` ajanı tester+consistency yeşil olmadan tag atmaz.
- **Kabul:** versiyon dosyaları (CMake project VERSION) güncel; tag yalnız senin açık talebinle.

### 3.2 — Paketleme & DLL toplama  (G)
- CPack/NSIS veya InnoSetup. **Dawn DLL** (`webgpu_dawn.dll`) + diğer runtime DLL'leri topla.
- Windows: `windeployqt` benzeri DLL closure; CD_ENABLE_WEBGPU=ON tier için Dawn DLL dahil.
- **Kabul:** temiz makinede installer çalışıyor + örnek uygulama açılıyor (smoke test).

### 3.3 — CI sertleştirme  (G)
- Mevcut: NVIDIA self-hosted CI (phase X3). Ekle: Dawn-ON tier (webgpu readback testi), D3D12 tier,
  (Faz 1 sonrası) macOS/Metal tier.
- **Kabul:** her PR'da çok-backend matrisi yeşil; golden artifact karşılaştırması otomatik.

**FAZ 3 GATE:** İmzalı/çalışır installer + yeşil CI matrisi + (onaylıysa) sürüm etiketi.

---

# FAZ 4 — Görsel & İçerik Cilası (opsiyonel, kullanıcı-güdümlü)

**Hedef:** Source 2 / HL2-Alyx kalite kültürü — polish, designer-tooling, "her özelliğin bir anı".
**Ön-koşul:** [[capture-driven-visual-iteration]] kuralı — görsel buglar kullanıcı capture'ı ister;
varsayıma karşı değil PİKSELE karşı iterasyon.

### 4.1 — Sample konsolidasyonu ≤10 binary  (G)
- Şu an 38 sample (Run 24'te 106→38). Hedef ≤10: hello_engine collapsing-header sprint
  ([[feedback-sample-consolidation-and-per-feature-demo]] — yeni özellik = hello_engine içi panel/log, yeni .exe değil).

### 4.2 — Editör genişletme  (H)
- cd::editor + cd::editor_panel: viewport mouse-drag→gizmo wiring (canlı ImGui + RHI present surface).
  Artık imgui_backend D3D12+Vulkan Context'leri device-tested → bu wiring'in altyapısı hazır.

### 4.3 — Görsel-kalite kuyruğu  (kullanıcı capture'ına bağlı)
- [[feedback-visual-quality-queue-run28]] + sonrası: kullanıcının ekran görüntüsüyle gelen bug'lar,
  her biri dedicated fixture + before/after capture ile.

---

# RİSK & BAĞIMLILIK ÖZETİ

| Faz | En büyük risk | Azaltma |
|-----|---------------|---------|
| 0 | Düşük | Hızlı, izole; her madde ayrı commit |
| 1 (Metal) | **Apple donanımı yok → BLOKE** | CI/donanım sağla; Fork-A kararı netleştir |
| 2 | GPU dispatch debug zorluğu | Build-erişimli iteratif agent; CPU-referans parity |
| 3 | Dawn DLL closure + imzalama | DLL POST_BUILD copy zaten kurulu; smoke test |
| 4 | Görsel bug "varsayıma karşı" iterasyon | capture-driven kural; dedicated fixture |

# KARARLAR (verildi — 2026-06-22)
1. **Metal shader yolu → cd::gluon SL→MSL.** Tek shader kaynak dili → tüm backend'ler
   (write-once, en yüksek abstraction, "rahat kullanım"). SPIRV-Cross transpile yolu REDDEDİLDİ;
   Metal MSL emit, mevcut cd::gluon SL altyapısına bir backend olarak eklenecek.
2. **Metal köprü → Metal-cpp (C++) BİRİNCİ tercih, çaresi yoksa ObjC++ `.mm` fallback.**
   (Kullanıcı: "ilk tercihim hep C++, çaresi yoksa objC++.")
3. **Apple ortamı → sanal makine veya bulut macOS** (self-hosted fiziksel Mac değil).
4. **Sürüm etiketi → HÂLÂ YOK.** Kullanıcı açıkça isteyene kadar tag atılmaz.
5. **Öncelik → Metal DÜŞÜK ÖNCELİK.** Metal bu Windows host'unda doğrulanamaz (sadece edit-only
   geliştirme), bu yüzden **Faz 2 (Phase-2 GPU, Vulkan'da doğrulanabilir) Faz 1'den ÖNCE** gelir.
   Faz 0 (temizlik) hemen; Metal donanım/CI gelene kadar bekler.

---

## ÖNERİLEN SIRA (kararlar sonrası)
1. **Faz 0** (hemen, hepsi doğrulanabilir, dış bağımlılık yok) — taban sağlamlaştırma.
2. **Faz 2** (Phase-2 GPU; Vulkan'da doğrulanabilir) — X5 shader hot-reload + GI sample wiring.
3. **Faz 3** (sürüm/paketleme; tag YOK) — istendiğinde.
4. **Faz 1 (Metal)** — DÜŞÜK ÖNCELİK; Apple sanal-makine/bulut sağlanınca. O zamana kadar
   yalnız edit-only (doğrulanamaz) → kanıt-temelli kural gereği aktif yürütülmez.

> Hazır olduğunda "Faz 0'a başla" veya "Faz X.Y'yi aç" de; planın o kısmını alıp paralel agent'larla
> yürütüp her yeşil checkpoint'te commit'lerim.
