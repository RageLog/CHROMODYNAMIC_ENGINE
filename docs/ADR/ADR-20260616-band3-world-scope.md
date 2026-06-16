# ADR-20260616 — ALL-MODULES-TO-100 BAND 3 / WORLD Kapsam Mührü (4 kütüphane)

- **Status**: Accepted
- **Date**: 2026-06-16
- **Branch**: dev (HEAD d1e2bc6)
- **Deciders**: Cemal TATLI
- **Author**: Developer (BAND 3 world subset close-out — impl-where-clean + edge-test-deepening + kapsam mührü pass)
- **Related**:
  - `docs/ROADMAP_ALL_MODULES_TO_100.md` (BAND 3 listesi + "100% = IMPLEMENTED+test
    YA DA formally SEALED" honest-rule, §"What 100% MEANS"; world named gap'leri:
    net_session_replay "broaden the SRPK surface + tests", physics_jolt "make the
    real Jolt path the default-tested config", audio_dsp_fx "implement FDN reverb
    tuning + SIMD", physics_vehicle "lateral/Pacejka tire model" §BAND 3 tablo)
  - `docs/PROJECT_COMPLETION_STATUS.md` §6 (engine/world baseline %'leri + Basis
    sütunu: net_session_replay 78, physics_jolt 78, audio_dsp_fx 75,
    physics_vehicle 75; grup rollup ~81%, "caveats are all documented")
  - `docs/ADR/ADR-20260616-band2-world-scope.md` +
    `docs/ADR/ADR-20260616-band3-foundation-scope.md` (kardeş band-mühür ADR'ları,
    aynı şablon: impl-the-named-gap + seal-the-rest + promote-on-need)
  - `docs/ADR/ADR-20260530-jolt-physics-integration.md` (Jolt vendor-boundary +
    backend-gating policy — bu ADR §2.2 onu BAND-3 honest-rule terminal durumuna
    mühürler)
- **Scope guard**: Bu ADR YALNIZ kapsam-karar dokümanıdır. Mühürlenen maddeler
  "deferred-by-design"dır — açık (open) sayılmazlar. Her madde "ihtiyaç doğunca
  promote" çıkış kapısı taşır. Bu pass'te değişiklikler YALNIZ
  engine/world/{net_session_replay,physics_jolt,audio_dsp_fx,physics_vehicle}/
  (header + src + test + lib/test CMakeLists) + bu docs/ADR/ dosyası altında.
  samples/ + hello_* + diğer world lib'leri + diğer gruplar + % docs'a
  DOKUNULMADI. (physics_vehicle public header'a 2 yeni VehicleState alanı —
  yaw_rate_rad_s + lateral_accel_ms2 — eklendi; salt ekleme, mevcut alan
  değişmedi → tek tüketici cd::editor::panel_vehicle_editor geriye-uyumlu
  derlendi + testi geçti, panel'in kendisine DOKUNULMADI.)

---

## 1. Bağlam

BAND 3 (70–79%) world alt-kümesi 4 kütüphaneyi 100%'e taşır: cd::net_session_replay
(SRPK record/replay binary format, Sprint-1 complete, küçük yüzey) +
cd::physics_jolt (gerçek Jolt 5.x backend `CD_PHYSICS_JOLT_REAL` + vendor yoksa
Euler stub fallback, build-flag-gated) + cd::audio_dsp_fx (header-only DSP
filtreleri + FDN reverb + anchor .cpp) + cd::physics_vehicle (bicycle-model
longitudinal + Jolt WheeledVehicle delegasyonu; lateral/Pacejka ertelenmişti).

Roadmap honest-rule'una göre bir modül 100%'tür ancak HER dokümante boşluk iki
terminal durumdan birindeyse: (a) IMPLEMENTED + test (küçük/net/tüketicisi olan),
VEYA (b) bir paragraflık kapsam-ADR'ı ile formally SEALED (promote-on-need
tetikleyicisiyle). "TODO/placeholder" üçüncü durumu kalamaz.

Bu pass'in bölünme çizgisi: dört kütüphanenin de named gap'i incelendi ve her
biri için en dürüst terminal durum seçildi:
- **net_session_replay**: charter (record/replay round-trip) zaten tamdı; yüzey
  genişletmesi yerine `load_from_file`'ın GERÇEKTEN test-edilmemiş hata
  dallarına (version-mismatch, truncated-record-body, missing-file) hedefli
  edge test eklendi → IMPLEMENTED(test) + SEALED.
- **physics_jolt**: "make the real Jolt path the default-tested config" named
  gap'i bu ortamda ZATEN sağlanmış durumda — Jolt vcpkg/FetchContent ile
  resolve oluyor, `ninja-base` `CD_PHYSICS_JOLT_REAL=1` derliyor,
  `is_stub_backend()` false; mevcut 13 test zaten gerçek Jolt'a karşı koşuyor.
  Eksik olan tek şey bunu KİLİTLEYEN bir invariant testiydi → eklendi + gating
  SEALED.
- **audio_dsp_fx**: "implement FDN reverb tuning" named gap'i ZATEN
  implement edilmişti (Schroeder/Freeverb FDN: RT60-türetilmiş feedback + karşılıklı-
  asal comb/allpass delay tuning); header banner'ı yanlış olarak hâlâ "Sprint-1
  stub: passthrough" diyordu. Banner düzeltildi + gerçekten test-edilmemiş
  feedback-stability dalı (impuls-tail finite + decay) için test eklendi; SIMD
  perf promote-on-need olarak SEALED.
- **physics_vehicle**: "lateral tire model" küçük+net+testlenebilir bir
  implementasyon fırsatıydı → kinematic-bicycle lateral terim (yaw_rate +
  lateral_accel) IMPLEMENTED + test; Pacejka slip-angle tyre modeli
  promote-on-need olarak SEALED.

---

## 2. Karar — kütüphane başına bir bölüm

### 2.1 cd::net_session_replay (world/net_session_replay) — 78 → 100  [IMPLEMENTED(test) + SEALED]

- **Bağlam**: 230 .cpp + 149 hdr; Recorder (record_packet → steady_clock
  timestamp → SRPK binary save) + Replayer (load + magic/version doğrulama +
  time-gated next_packet cursor). 7 gtest. Named gap (roadmap): "broaden the
  SRPK surface + tests". Mevcut testler round-trip + dry/time-gate + reset +
  empty + bad-magic'i kapatıyordu AMA `load_from_file`'ın üç gerçek hata dalı
  test edilmemişti: version != kVersion, per-record `read_pod` mid-record
  başarısızlığı (truncated body), ve `!is_open()` (missing file).
- **Karar**: SRPK formatı/yüzeyi charter-complete kabul edildi (record/replay v1
  tam) → yüzey genişletmek yerine gerçekten test-edilmemiş dallar **IMPLEMENTED
  (test)** + **SEALED**. 7→10 gtest: `VersionMismatchRejected` (doğru magic +
  version=2 → reddedilir; test 5'in bad-magic dalı bu noktaya hiç ulaşmıyordu),
  `TruncatedRecordBodyRejected` (header count=3 ama gövde boş → ilk timestamp
  read fail; load false döner VE Replayer temiz/empty kalır), `MissingFileRejected`
  (var olmayan path → !is_open guard). Üçü de deterministik sentetik dosya
  üzerinde, sleep_for YOK.
- **Gerekçe**: SRPK küçük, kapalı bir debug-capture formatı; "broaden the
  surface" bu pass'te yapay yüzey-şişirme olurdu (don't pad). Gerçek değer
  gerçekten test-edilmemiş HATA dallarını kilitlemektedir — bu dallar bozuk/
  kısmi dosyaya karşı koruyucudur (ağ-debug capture'ları crash/kesinti sonrası
  truncated olabilir). fail-on-revert.
- **Promote-on-need**: SRPK v2 (per-packet CRC, endian-bswap BE platform için,
  zstd payload sıkıştırma, ya da streaming-load çok-GB replay için) gerçek bir
  tüketici ihtiyacıyla + version-bump + ayrı ADR ile gelir; v1 magic/version/
  count/record layout sabit kalır.

### 2.2 cd::physics_jolt (world/physics_jolt) — 78 → 100  [REAL PATH DEFAULT-TESTED + SEALED]

- **Bağlam**: 1164 .cpp (JoltWorldReal `JPH::PhysicsSystem` + JoltWorldStub
  semi-implicit Euler) + 414 hdr PIMPL. 13 gtest. CMake 3-tier backend
  çözümü: (1) vcpkg find_package(Jolt), (2) FetchContent v5.5.0, (3) Euler
  stub fallback (`CD_PHYSICS_JOLT_FORCE_STUB=ON` ya da vendor yok). Compile
  define: `CD_PHYSICS_JOLT_REAL=1` (Jolt linked) ya da `CD_PHYSICS_JOLT_STUB=1`
  (fallback). Named gap (roadmap): "make the real Jolt path the default-tested
  config (currently build-flag-gated)".
- **Karar**: Named gap bu ortamda ZATEN sağlanmış — `ninja-base` build'inde
  Jolt vcpkg/FetchContent ile resolve oluyor, `cd_physics_jolt` ve
  `cd_test_jolt_world` `CD_PHYSICS_JOLT_REAL=1` ile derleniyor,
  `is_stub_backend()` **false** döndürüyor, yani mevcut 13 test ZATEN gerçek
  `JPH::PhysicsSystem`'e karşı koşuyor (default CI config). Eksik olan tek şey
  bu invariant'ı kilitleyen testti → **eklendi**:
  `BackendSelectionMatchesBuildDefinition` compile-define ile runtime
  `is_stub_backend()`'i çapraz-doğrular (`CD_PHYSICS_JOLT_REAL` tanımlıysa
  is_stub==false ZORUNLU, `CD_PHYSICS_JOLT_STUB` tanımlıysa is_stub==true
  ZORUNLU). Define lib'de PRIVATE kalır; test'e yalnız seçici token CMake'te
  mirror edilir (lib CMakeLists `_CD_JOLT_TEST_DEFINE` → test CMakeLists
  `target_compile_definitions`). 13→14 gtest. Build-flag-gated-real +
  tested-stub-fallback tasarımı **SEALED** (audio platform-gating ile simetrik).
- **Gerekçe**: Gerçek Jolt yolu zaten default-derlenip default-test ediliyordu;
  named gap "build-flag-gated" ifadesi vendor-yok CI senaryosunu kasteder —
  o senaryoda stub fallback doğru davranıştır (network-izole CI build/test
  edebilsin diye). İki yol da geçerli + test edilebilir; doğru terminal durum
  ikisini de DÜRÜSTÇE belgeleyip seçim-invariant'ını kilitlemektir. Invariant
  test'i, gerçek-yolun stub'a sessizce düşmesini (örn. vendor resolve bozulup
  kimse fark etmeden stub'a fallback) fail-on-revert yapar. JPH-symbol PIMPL
  sınırı (ADR-20260530 §B) korunur — yalnız seçici makro token'ı test'e sızar,
  Jolt tipleri sızmaz.
- **Promote-on-need**: NVIDIA/Linux self-hosted CI'da `CD_PHYSICS_JOLT_FORCE_STUB`
  ile her İKİ yolu da aynı CI run'ında matrix-build/test etmek (real + stub
  parite), ya da gerçek-Jolt restitution/CCD/character-controller derinliğini
  (şu an stub-yolu impulse-API ile temsil ediliyor) tam JPH solver testleriyle
  kapatmak — gerçek bir CI-hardware / fizik-feature ihtiyacıyla + ayrı ADR ile
  gelir.

### 2.3 cd::audio_dsp_fx (world/audio_dsp_fx) — 75 → 100  [SEALED + IMPLEMENTED(test); banner fix]

- **Bağlam**: header-only DSP (BiquadCoeffs/LowPass/HighPass cookbook filtreleri
  + DelayLine ring) + anchor .cpp'de Reverb FDN. 11 gtest. Named gap (roadmap):
  "implement FDN reverb tuning + SIMD (deferred)". Bulgu: FDN reverb tuning
  ZATEN implement edilmişti — Schroeder/Freeverb-style network: 4 paralel
  low-pass feedback comb (RT60-türetilmiş feedback gain `g = 10^(-3*D/(RT60*Fs))`,
  karşılıklı-asal `next_prime`-yuvarlanmış comb delay tuning, damping
  rolloff) → 2 series allpass diffuser → wet/dry mix. AMA header banner'ı hâlâ
  yanlışlıkla "Reverb — Sprint-1 stub: passthrough" diyordu ve .cpp banner'ı
  anchor'ı "deliberately empty" olarak tanımlıyordu (ikisi de eski/yanlış).
- **Karar**: FDN tuning matematiği charter-complete kabul edildi → **SEALED**
  (scalar DSP v1). Yanlış header + .cpp banner'ları gerçek FDN tasarımını
  tarif edecek şekilde düzeltildi (yalnız yorum; kod davranışı değişmedi).
  Gerçekten test-edilmemiş feedback-stability dalı için **IMPLEMENTED(test)**:
  `ImpulseTailIsFiniteAndDecays` (en kötü-durum: room_size=0.95 + damping=0.1
  → en uzun RT60 → feedback gain'leri 1'e en yakın; ~1s impuls-response:
  HER sample finite ZORUNLU — kararsız döngü NaN/blow-up üretirdi — VE geç
  pencere RMS'i erken pencere RMS'inden küçük ZORUNLU — ağ decay ediyor,
  sürekli değil). 11→12 gtest, sleep_for YOK. SIMD perf promote-on-need
  olarak SEALED.
- **Gerekçe**: Named gap'in "FDN tuning" yarısı zaten yapılmıştı; geriye kalan
  "SIMD" perf optimizasyonudur (matematik doğru, sadece daha hızlı değil) →
  honest-rule (b) promote-on-need tam bunun için. Mevcut testler room-size/
  dry/reset/wet'i kapatıyordu ama feedback-gain STABİLİTESİNİ (gerçek bir
  doğruluk/güvenlik özelliği — kararsız FDN duyulabilir patlama üretir) hiç
  assert etmiyordu; en-kötü-durum stability testi gerçek boşluğu kapatır.
  Banner-düzeltmesi mis-baselining'i önler (sonraki okuyucu "stub passthrough"
  yorumuna bakıp kütüphaneyi eksik sanmasın — phase1045/rhi-banner dersinin
  aynısı).
- **Promote-on-need**: SSE/AVX/NEON SIMD comb+allpass filter bank (per-frame
  binlerce voice mixlenirken), ya da geç-yansıma için daha zengin FDN topolojisi
  (Jot/Stautner-Puckette feedback-matrix, modulated delay line) gerçek bir
  audio-perf profili / kalite ihtiyacıyla + ayrı ADR ile gelir; Reverb::configure/
  process/reset API yüzeyi sabit kalır.

### 2.4 cd::physics_vehicle (world/physics_vehicle) — 75 → 100  [IMPLEMENTED + SEALED]

- **Bağlam**: 638 src bicycle model (longitudinal: gear-ratio torque chain +
  flat+taper torque curve + aero/rolling drag + brake + semi-implicit Euler) +
  Jolt WheeledVehicle delegasyonu (use_jolt → JoltAdapter). 16 gtest (3 binary:
  vehicle + vehicle_jolt + vehicle_real_jolt). Named gap (roadmap):
  "lateral/Pacejka tire model (only longitudinal today)".
- **Karar**: Lateral terimin küçük+net+testlenebilir kısmı **IMPLEMENTED** +
  test; tam Pacejka **SEALED**. Kinematic-bicycle lateral response eklendi:
  iki yeni VehicleState alanı (`yaw_rate_rad_s`, `lateral_accel_ms2`) + iki
  private helper (`wheelbase()` chassis-uzunluğundan türetilen 0.85*length floor'lu
  dingil-mesafesi; `compute_lateral(v, out)` → δ = steer*max_steer,
  `yaw_rate = v*tan(δ)/L`, `lateral_accel = v*yaw_rate = v²*tan(δ)/L`). Hem
  bicycle hem Jolt-fall-through tick yolu lateral'i populate eder (Jolt yolunda
  read-back hız ile). 16→18 gtest (+2 vehicle binary'sinde):
  `SteeringAtSpeedProducesYawAndLateralAccel` (düz sürüş → yaw≈0; hızda sağa
  steer → pozitif yaw + santripetal lateral accel; kapalı-form `a_lat = v*yaw`
  çapraz-doğrulaması) + `LateralIsSignSymmetricAndScalesWithSpeed` (sol/sağ
  simetri + hız arttıkça aynı steer için daha büyük yaw — linear v faktörü).
  Pacejka slip-angle tyre modeli **SEALED** promote-on-need. Salt-ekleme
  header değişikliği → tek tüketici (cd::editor::panel_vehicle_editor)
  geriye-uyumlu derlendi + testi geçti.
- **Gerekçe**: Kinematic-bicycle lateral terim küçük (algebraic, tek tick'te
  geçerli, integrasyon gerektirmez), net (standart ders-kitabı formülü), ve
  deterministik+testlenebilir — honest-rule (a) tam bunun için. Tam Pacejka
  "Magic Formula" slip-angle/slip-ratio tyre modeli ise terrain-contact-normal
  + tyre-load-transfer + per-wheel slip durumu gerektirir (gerçek lastik grip
  limiti, drift, lock-up) — o, Jolt WheeledVehicleController'ın kendi solver'ı
  (gerçek-Jolt yolu zaten onu kullanır) ya da ayrı bir CPU Pacejka modülü
  işidir; CPU bicycle-model katmanında over-engineering olurdu. Kinematic terim
  yaw_rate/lateral_accel'i (kamera-shake, tyre-load tahmini, grip-limit kontrolü
  için yeterli) doğru üretir, Pacejka'ya graceful promote yolu açıktır.
- **Promote-on-need**: Pacejka Magic-Formula lateral force (slip-angle →
  Fy = D*sin(C*atan(B*α - E*(B*α - atan(B*α))))), tyre-load transfer, per-wheel
  slip-ratio/lock-up, ve full 2-DOF (yaw + lateral velocity) bisiklet-model
  integrasyonu gerçek bir racing-sim / drift-gameplay ihtiyacıyla + ayrı ADR
  ile gelir; VehicleConfig/VehicleState/Vehicle API yüzeyi geriye-uyumlu
  genişler (mevcut alanlar sabit).

---

## 3. Reddedilen alternatifler

- **net_session_replay'in SRPK yüzeyini yapay genişletmek** (per-packet
  metadata, yeni record tipleri) gerçek tüketici ihtiyacı olmadan: don't-pad
  kuralına aykırı; SRPK küçük kapalı bir debug-capture formatı. RED — yalnız
  gerçek test-edilmemiş hata dalları kilitlendi + format charter-complete
  mühürlendi.
- **physics_jolt için real-yolu "henüz default değil" diye seal etmek**: bu
  ortamda ZATEN default-derlenip default-test ediliyor (`CD_PHYSICS_JOLT_REAL=1`,
  is_stub==false, 13 test gerçek JPH'ye karşı). Seal etmek yanlış-baseline
  olurdu. RED — invariant test ile gerçek-yol-default KİLİTLENDİ; gating
  (real-default + stub-fallback) belgelendi.
- **physics_jolt define'ını PUBLIC yapmak** (test makro görebilsin diye): lib'in
  derleme yüzeyini gereksiz genişletir; bunun yerine CMake'te yalnız seçici
  token test target'a mirror edildi (PIMPL JPH-symbol sınırı korunur). RED.
- **audio_dsp_fx FDN'i "deferred" diye seal etmek**: FDN tuning ZATEN
  implement'ti (RT60-feedback + asal-delay tuning); sadece banner yanlıştı.
  Seal-as-deferred yanlış-baseline olurdu. RED — banner düzeltildi, matematik
  mühürlendi, stability testi eklendi, SIMD promote-on-need ayrıldı.
- **physics_vehicle'a tam Pacejka tyre modeli implement etmek**: terrain-contact-
  normal + tyre-load + per-wheel slip gerektirir; CPU bicycle-model katmanında
  over-engineering + gerçek-Jolt yolu zaten kendi solver'ını kullanıyor. RED —
  kinematic-bicycle lateral terim (küçük/net/testlenebilir) implement edildi,
  Pacejka promote-on-need mühürlendi.
- **% docs'u / samples'ı / panel_vehicle_editor'ı düzenlemek**: kapsam DIŞI
  (brief: SCOPE EXCLUSION — ONLY engine/world/{net_session_replay,physics_jolt,
  audio_dsp_fx,physics_vehicle}/ + docs/ADR/). RED — panel yalnız geriye-uyumlu
  yeniden-derlendi (salt-ekleme header), kaynağına dokunulmadı.

## 4. Sonuçlar

- (+) 4/4 BAND-3 world alt-küme kütüphanesi honest-rule terminal durumuna
  geçti: net_session_replay (3 edge test + format mührü), physics_jolt
  (real-default invariant test + gating mührü), audio_dsp_fx (banner fix +
  stability test + FDN mührü), physics_vehicle (kinematic lateral terim impl +
  2 test + Pacejka mührü). Hiçbir kütüphanede placeholder/yanlış-banner kalmadı.
- (+) net_session_replay artık truncated/version-mismatch/missing-file'a karşı
  fail-on-revert kilitli (+3 test); bozuk capture'lara karşı koruma doğrulandı.
- (+) physics_jolt'un gerçek-Jolt-default-test invariant'ı artık kilitli (+1
  test): real-yol stub'a sessizce düşerse fail eder. Backend-gating dürüstçe
  belgelendi (real-default + stub-fallback simetrisi).
- (+) audio_dsp_fx banner'ı artık gerçek FDN tasarımını anlatıyor (mis-baselining
  önlendi); en-kötü-durum feedback-stability (impuls-tail finite + decay) artık
  test ediliyor (+1 test) — kararsız-FDN regresyonu fail-on-revert.
- (+) physics_vehicle gerçek bir lateral-dinamik kazandı: yaw_rate +
  lateral_accel kinematic-bicycle formülünden, hem CPU hem Jolt yolunda (+2
  test). Salt-ekleme → tek tüketici (editor panel) geriye-uyumlu.
- (+) Toplam +7 yeni test (3+1+1+2), hepsi edge/contract + anti-flakiness
  uyumlu (sleep_for YOK; deterministik sentetik dosya/impuls/tick).
- (+) Her mühür "promote-on-need" tetikleyici taşır → genişleme yolu (SRPK v2,
  dual-backend CI matrix, SIMD filter bank, Pacejka tyre) nettir ama bugün
  dead-code/over-engineering olmaz.
- (−) Mühürler SRPK v2, gerçek-Jolt çözücü-derinlik testleri, SIMD reverb, ya
  da Pacejka tyre modelini bu pass'te ÜRETMEZ; tüketici/CI/perf ihtiyacı
  doğunca ayrı ADR'larla gelir. Kabul: BAND 3 world alt-kümesi "implement-the-
  small-clean-named-gap + seal-the-rest" karakterinde.

---

## Varsayımlar

- Brief'in "100% RULE per lib" yorumu: documented gap → IMPLEMENTED+tested
  (küçük/net) VEYA SEALED — bu pass'te net_session_replay/audio_dsp_fx test-
  tarafı + physics_vehicle lateral implement edildi, physics_jolt named gap
  ZATEN sağlanmış olarak invariant-test ile kilitlendi (band2/band3-foundation
  ADR'larıyla aynı bölünme çizgisi).
- physics_jolt: `ninja-base` build'inde Jolt vcpkg/FetchContent ile resolve
  olur ve `CD_PHYSICS_JOLT_REAL=1` derlenir (CMakeCache `CD_PHYSICS_JOLT_FORCE_STUB
  OFF` + `CD_DISABLE_VCPKG OFF` doğrulandı; impl-Debug.ninja DEFINES'ta
  `CD_PHYSICS_JOLT_REAL=1` doğrulandı; `is_stub_backend()` false). Vendor-yok
  bir CI'da invariant test'in `#elif CD_PHYSICS_JOLT_STUB` dalı is_stub==true
  assert eder — iki config'te de fail-on-revert.
- physics_vehicle wheelbase: ayrı bir authoring alanı yerine chassis_dimensions
  uzunluğundan türetildi (0.85*length, 0.5m floor) — VehicleConfig yüzeyini
  genişletmemek için; gerçek dingil-mesafesi authoring gerekirse promote-on-need
  bir WheelConfig/VehicleConfig alanı eklenebilir (geriye-uyumlu).
- VehicleState'e 2 alan eklenmesi salt-eklemedir: mevcut alanlar/sıra değişmedi
  → tek tüketici cd::editor::panel_vehicle_editor geriye-uyumlu derlendi
  (build temiz) + testi geçti (cd_test_editor_panel_vehicle_editor PASS); panel
  kaynağına DOKUNULMADI (kapsam dışı).
- "scope exclusion" kuralı uygulandı: tüm değişiklikler engine/world/
  {net_session_replay,physics_jolt,audio_dsp_fx,physics_vehicle}/ (header + src +
  test + lib/test CMakeLists) + bu docs/ADR/ dosyası altında. samples/ + hello_* +
  diğer world lib'leri + diğer gruplar + % docs'a dokunulmadı.
- Golden byte-identical doğrulaması: bu 4 world kütüphanesi hello_engine
  rendering yolunu etkilemez (audio/net/physics; physics_vehicle yalnız salt-
  ekleme + editor-panel'i etkiler, hello_engine render'ı değil); fixture #5
  capture baseline ile bayt-bayt eşit doğrulandı.

## Sonraki

- BAND 3'ün geri kalan world-dışı kütüphaneleri (render umbrella, async_submit,
  ui_font, ai::squad, ui_a11y, lighting_clusters, ibl, ui umbrella, mesh_shader,
  cluster, texture_compress, material_authoring, texture_synth) bu mühür
  şablonunu (impl-the-small-clean-named-gap + seal-the-rest + promote-on-need)
  tekrar kullanabilir; bu grubun büyük çapraz-kesen item'ı 3× froxel-clustering
  de-dup'ıdır (cluster ↔ lighting_clusters ↔ light::ClusterGrid).
- Bu pass'in mühürlenen promote tetikleyicileri: net_session_replay için SRPK v2
  (CRC/bswap/compress); physics_jolt için dual-backend CI matrix + gerçek-Jolt
  çözücü-derinlik testleri; audio_dsp_fx için SIMD comb/allpass filter bank;
  physics_vehicle için Pacejka Magic-Formula tyre + full 2-DOF yaw/lateral
  integrasyonu (racing-sim / drift-gameplay ihtiyacıyla).
