# ADR-20260616 — ALL-MODULES-TO-100 BAND 2 / ASSET+TOOLING (Band-2 subset) Kapsam Mührü (3 kütüphane)

- **Status**: Accepted
- **Date**: 2026-06-16
- **Branch**: dev (HEAD f461b14)
- **Deciders**: Cemal TATLI
- **Author**: Developer (BAND 2 asset+tooling Band-2-subset close-out — son Band-2 grubu; charter-complete kapsam mührü + script error-path test-topup; asset + script + imgdiff)
- **Related**:
  - `docs/ROADMAP_ALL_MODULES_TO_100.md` (BAND 2 listesi + "100% = IMPLEMENTED+test
    YA DA formally SEALED" honest-rule, §"What 100% MEANS"; asset+tooling Band-2
    satırları: cd::script "sandbox/error-path + binding-coverage tests; seal",
    cd::imgdiff (tooling) "seal as complete tool")
  - `docs/PROJECT_COMPLETION_STATUS.md` §2 (engine/asset + runtime + tooling
    baseline %'leri + Basis sütunu; asset 85 "11 sub-targets … real loaders … ~195
    gtests/15 files", script 80 "embedded Lua 5.4 … 41 gtests; no stubs", imgdiff 80
    "real FLIP-lite + SSIM + Gaussian, cited; 32 gtests; golden-image tool")
  - `docs/ADR/ADR-20260616-band1-scope.md` + `docs/ADR/ADR-20260616-band2-foundation-scope.md`
    + `docs/ADR/ADR-20260616-band2-game-scope.md` + `docs/ADR/ADR-20260616-band2-render-core-scope.md`
    + `docs/ADR/ADR-20260616-band2-render-features-scope.md` + `docs/ADR/ADR-20260616-band2-world-scope.md`
    + `docs/ADR/ADR-20260616-band2-ui-scope.md` (kardeş band-mühür ADR'ları, aynı
      şablon: seal-with-promote-on-need + targeted-topup-only-where-genuinely-thin)
- **Scope guard**: Bu ADR YALNIZ kapsam-karar dokümanıdır. Mühürlenen maddeler
  "deferred-by-design"dır — açık (open) sayılmazlar; her madde "ihtiyaç doğunca
  promote" çıkış kapısı taşır. Bu üç kütüphane brief'in adlandırdığı **production +
  tooling set**'tir (80–85%, derin test edilmiş) → to-100 mostly a charter-complete
  SEAL + test-topup ONLY where a real surface gap exists (don't pad). Bu pass'te
  **asset + imgdiff charter-complete (SAF SEAL)**, **script SEAL + 5 error-path/edge
  test-topup** (gerçek bir test-edilmemiş Engine.cpp dalı bulundu). YALNIZ
  engine/asset/ (asset şemsiyesi + core loader alt-target'ları, STREAMER'lar HARİÇ —
  onlar Band 6) + engine/script/ + engine/imgdiff/ + docs/ADR/'a dokunuldu. Asset
  STREAMER'ları (texture/scene/audio_streamer — Band 6), texture_compress/
  material_authoring/shader_cache/vfx_authoring/validator/streamer_pool/runtime
  (alt band'ler), samples/ + hello_* + diğer gruplar + % docs DAHİL hiçbir şeye
  dokunulMADI; chrome golden BYTE-IDENTICAL.

---

## 1. Bağlam

BAND 2 (80–89%) asset+tooling grubunun bu dispatch'in kapsadığı alt-kümesi 3
kütüphaneyi 100%'e taşır: asset (umbrella) 85, script 80, imgdiff (tooling) 80.
(Grubun geri kalan partial/skeleton kütüphaneleri — texture_compress 70,
material_authoring 70, texture_synth 70, shader_cache 65, vfx_authoring 65,
streamer_pool 65, validator 60, runtime 55, ve üç asset STREAMER'ı
texture/scene/audio 45 — daha düşük band'lerdedir [3–6] ve bu dispatch'in AÇIKÇA
DIŞINDADIR. Bilhassa asset STREAMER'ları Band 6'dır; bu mühür asset ŞEMSİYESİ +
core loader'larıdır, streamer'lar DEĞİL.)

Baseline raporunun §2 değerlendirmesi grubun güçlü tarafını net belirtir: "asset
umbrella (real loaders, deeply tested) + script (full Lua)". Bu üç kütüphane o
güçlü çekirdektir:
- **asset** — 11 alt-target (gltf/image/json/ktx2/obj/pak/streaming/cdmesh/cdtex/
  wav) + el-yazımı JSON (517) + tinygltf (760) + BC7; **195 gtest / 15 dosya**
  (her loader + asset altyapısı: registry/refcount/tag/streamer/dependency-graph/
  hot-reload/path-resolver/schema/bundle).
- **script** — vendored Lua 5.4 PIMPL; run_string/file + global get/set + function
  register + numeric/mixed call + instruction-cap sandbox + 877-LOC ECS-world
  binding; **41 gtest** (30 core + 11 binding); no stubs.
- **imgdiff** — header-only golden-image diff tool: FLIP-lite (Andersson 2020) +
  SSIM (Wang 2004) + Gaussian + compare/highlight/passes, cited; **32 gtest**
  (FlipFull 5 / FlipLite 6 / GaussianBlur 5 / ImageDiff 7 / SsimGaussian 4 /
  SsimLite 5).

Roadmap honest-rule'una göre bir modül 100%'tür ancak HER dokümante boşluk iki
terminal durumdan birindeyse: (a) IMPLEMENTED + test (küçük/net/tüketicisi olan),
VEYA (b) bir paragraflık kapsam-ADR'ı ile formally SEALED (promote-on-need
tetikleyicisiyle). "TODO/placeholder" üçüncü durumu kalamaz.

Bu pass'in inceleme bulgusu (kanıt §2'de kütüphane başına):
- **2 charter-complete (SEAL only)**: asset (195 test / 15 dosya — her loader +
  altyapı round-trip + negative test-dolu), imgdiff (32 test — her algo + her hata
  kodu test-dolu).
- **1 SEAL + test-topup**: script — brief'in adlandırdığı "sandbox/error-path +
  binding-coverage tests; seal" ile uyumlu, Engine.cpp'de gerçekten test-edilmemiş
  5 hata-yolu/edge dalı bulundu (nil-global call, numeric/mixed non-matching-return
  fallback, register_function rebind, run_file compile-error) → 5 test eklendi.
- **0 IMPLEMENT**: Grupta gerçek bir kNotImpl/placeholder kod-boşluğu BULUNMADI;
  baseline "script: no stubs", asset "real loaders, deeply tested", imgdiff "complete
  golden-image tool". Üç kütüphane de impl olarak charter-tam.

---

## 2. Karar — kütüphane başına bir bölüm

### 2.1 cd::asset (engine/asset/, umbrella + core loaders) — 85 → 100  [charter-complete SEAL]

- **Bağlam**: 11 alt-target (gltf/image/json/ktx2/obj/pak/streaming/cdmesh/cdtex/
  wav); ~3.5k src — el-yazımı JSON parser (Json.cpp 517) + tinygltf-tabanlı glTF
  loader (GltfLoader/SceneLoader 760) + Image (PNG/JPG/HDR) + ImageWrite + BC7
  encode (Bc7Compress) + KTX2 + OBJ + WAV + CdMesh/CdTex iç-formatları + Pak arşiv.
  Header tarafı asset-runtime altyapısı: AssetRegistry (ref-count + evict),
  AssetId/Tag/TagSet, AsyncStreamer (StreamQueue), DependencyGraph, HotReloadQueue,
  FileWatcher, PathResolver, SchemaRegistry, MemoryCache, BundleMeta, LoadProfile.
  **195 gtest / 15 dosya**: per-loader (gltf alpha-mode+infer+scene_loader, image+
  bc7, json, ktx2, obj, pak, wav, cdmesh, cdtex, streaming) + asset altyapı suite'i
  (test_asset.cpp 59 test: AssetId 3 / AssetRefCount 4 / AssetRegistry(+Evict+_F) 9 /
  AssetTag(+Set) 4 / AsyncStreamer 3 / BundleMeta 5 / DependencyGraph 4 / FileWatcher
  3 / HotReloadQueue 4 / LoadProfile 4 / MemoryCache 4 / PathResolver 4 / SchemaRegistry
  3 / StreamQueue 5). Baseline: "real loaders, deeply tested".
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi. Yeni test eklenmedi.
  **SEALED**: charter tam (umbrella + core loaders).
- **Gerekçe**: asset'in load-bearing yüzeyi — 10 gerçek loader (her biri kendi
  round-trip + malformed-input + boundary testiyle) + asset-runtime altyapısı
  (registry/refcount/evict/streamer/dependency-graph/hot-reload, hepsi test-dolu) —
  zaten 195 testle (15 dosya) kapalı; pad roadmap "don't pad" + brief "topup only a
  genuinely-untested loader branch if one exists" kuralına aykırı olurdu, ve böyle
  bir test-edilmemiş loader dalı BULUNMADI.
- **Brief'in "STREAMER'lar HARİÇ" sınırı**: Bu mühür asset ŞEMSİYESİ + core
  loader'larıdır. Asset STREAMER'ları (texture_streamer/scene_streamer/audio_streamer,
  45%, "real queue + async pool BUT decode is a Sprint-2 placeholder") **Band 6'dır
  ve bu mühürün DIŞINDADIR** — onların gerçek decode-payload boşluğu kendi
  Band-6 dispatch'inde "wire the real decoder behind the done orchestration" ile
  kapatılır. Bu ADR onları sealemez/dokunmaz.
- **Promote-on-need**: Yeni asset formatı (FBX/USD import, DDS/EXR decode), deeper
  glTF feature-coverage (morph-target/sparse-accessor/Draco), ya da asset-runtime
  hot-reload'ın daha geniş dependency-invalidation grafiği gerçek bir asset-pipeline
  tüketicisiyle + ayrı ADR ile gelir; AssetRegistry / IAssetLoader / her loader'ın
  load(...) → Result yüzeyi sabit kalır.

### 2.2 cd::script (engine/script/) — 80 → 100  [SEAL + 5 error-path/edge test-topup]

- **Bağlam**: 1.3k src; vendored Lua 5.4 PIMPL (Engine.cpp + Bindings.cpp) —
  ctor/dtor (luaL_newstate + openlibs), run_string/run_file (loadbufferx/loadfilex +
  pcall, hata→Result domain 0x001C: kCompileError/kRuntimeError/kFileNotFound/
  kAllocFailed), set/get_global (number/string/bool, type-discriminated), last_error,
  register_function (light-userdata trampoline + stable CallbackSlot), call_global +
  call_global_numeric + call_global_mixed (variant-tagged push/pop), instruction-cap
  sandbox (lua_sethook MASKCOUNT), native_state escape-hatch + 877-LOC ECS-world
  binding (Bindings.cpp). **41 gtest** (test_script.cpp 30 + test_lua_bindings.cpp 11):
  ctor-valid / run-simple / arithmetic / compile-error / runtime-error / missing-file /
  stdlib / global set-get (number/string/bool) + script↔C++ global round-trip +
  type-mismatch + unset-global + last_error populate/clear / register-fires +
  call_global + non-function-value-fails / numeric single+multi+args-in-order+
  non-callable / sandbox cap-aborts+clear-allows / mixed round-trip+hetero-tuple+
  false-bool+empty+non-callable. Baseline: "no stubs".
- **Karar**: Çekirdek IMPLEMENTED. Brief'in "sandbox/error-path + binding-coverage
  tests; seal" yönergesiyle, Engine.cpp'de gerçekten test-edilmemiş 5 dal bulundu
  → **5 test eklendi** (test_script.cpp); kalan yüzey **SEALED**.
- **Eklenen 5 test (her biri Engine.cpp'de daha önce test-edilmemiş bir dalı kapatır)**:
  1. `ScriptError.CallGlobalNilGlobalFails` — `call_global` bir HİÇ-SET-EDİLMEMİŞ
     (nil) global üzerinde. Mevcut `CallGlobalOnNonFunctionFails` testi bir SAYI
     değeri (7.0) set ediyordu; nil-global yolu (`lua_getglobal` → nil →
     `lua_isfunction == 0`) ayrı bir veri-durumu, last_error "not callable" doğrular.
  2. `ScriptNumeric.NonNumericReturnSlotFallsBackToZero` — `call_global_numeric`'in
     sonuç-döngüsündeki else-dalı (Engine.cpp:352-353, `lua_isnumber == 0` → 0.0
     push). String dönen fonksiyon ile slot 0.0'a coerce edilir, çağrı reject edilmez.
  3. `ScriptMixed.UnsupportedReturnSlotEncodesAsFalse` — `call_global_mixed`'in
     desteklenmeyen-tip fallback'i (Engine.cpp:416-420, LuaValue variant dışı tip,
     örn. nil → bool(false)). `return 1, nil, 3` ile orta slot false, pozisyonel
     erişim 3 girdi korur (slot drop edilmez).
  4. `ScriptFunctions.RebindSameNameLatestWins` — `register_function` aynı isme
     ikinci bind (Engine.cpp:288 ikinci push_back); iki slot da unique_ptr'la canlı,
     Lua en son binding'e çözer, eski lambda hiç ateşlenmez.
  5. `ScriptEngine.RunFileWithSyntaxErrorIsCompileError` — `run_file`'ın
     compile-error dalı (Engine.cpp:168, `load_rc != LUA_OK && != LUA_ERRFILE`),
     missing-file (kFileNotFound) dalından AYRI. Geçici dosyaya bozuk chunk yazılır,
     kCompileError doğrulanır, dosya temizlenir (anti-flakiness: sleep yok,
     deterministik file IO).
- **Gerekçe**: Brief script için "add sandbox/error-path coverage ONLY if a real
  untested branch (e.g. script error propagation, nil-global, register_function
  edge)" dedi — beş dal da bu kategorinin gerçek örnekleri (Engine.cpp'de mevcut
  ama test-dışı). Bunlar küçük/net/saf-CPU/Lua-VM, tüketici-prerequisite gerektirmez
  → IMPLEMENTED+test (pad değil; her test ayrı bir kod-yolu kapatır). Geri kalan
  yüzey (run/global/sandbox-cap/binding) zaten test-dolu → SEALED.
- **Promote-on-need**: Per-Engine memory cap (header'da "Sprint 7+" olarak not
  edilmiş, instruction-cap'in yanında byte-budget hook'u), Lua table/nested-object
  variant genişlemesi (header "Wave 9+ wave can extend if needed"), ya da coroutine/
  yield tabanlı script-step bütçelemesi gerçek bir script-heavy gameplay tüketicisiyle
  + ayrı ADR ile gelir; Engine'in run_*/global/call_*/register_function/cap yüzeyi
  sabit kalır.

### 2.3 cd::imgdiff (engine/imgdiff/, tooling) — 80 → 100  [tooling-complete SEAL]

- **Bağlam**: header-only ~955 LOC / 4 hdr (ImageDiff 181 + Flip + Ssim + Gaussian);
  golden-image diff aracı — `compare(a,b,tol)` (different-pixels + per-channel
  max-abs-delta + mean-abs + RMSE + PSNR, mse=0→999 sentinel) + `highlight(a,b,tol)`
  (eşleşen piksel %50 koyu, sapan piksel kırmızı, CI-artifact) + `passes(report,
  max)` + FLIP-lite (Andersson 2020) + SSIM (Wang 2004) + Gaussian blur, tümü cited.
  Üç hata kodu (kDimensionMismatch / kEmptyImage / kNullPointer) + kOk. **32 gtest**:
  FlipFull 5 / FlipLite 6 / GaussianBlur 5 / ImageDiff 7 (identical→PSNR-sentinel +
  tolerance-band + per-channel-delta + dim-mismatch + empty + null + highlight) /
  SsimGaussian 4 / SsimLite 5. Baseline: "real FLIP-lite + SSIM + Gaussian, cited;
  golden-image tool".
- **Karar**: Araç tam IMPLEMENTED + zaten derin test edildi. Yeni test eklenmedi.
  **SEALED**: tooling-100 (complete tool, NOT engine-runtime depth).
- **Gerekçe (cross-band notes: "Tooling libs reach 100 by completing+sealing the
  tool, not by engine-runtime depth")**: imgdiff'in load-bearing yüzeyi — compare
  (5 metrik + PSNR-sentinel) + highlight (CI-artifact) + passes + FLIP-lite + SSIM +
  Gaussian + üç hata kodu — zaten 32 testle (her algo + her hata kodu + identical/
  tolerance/per-channel/null/empty/dim-mismatch edge'leri) kapalı; bir araç olarak
  tamamdır. Roadmap'in tooling-100 tanımı ("complete tool") karşılanır; engine-runtime
  derinliği aramak (örn. GPU-tarafı diff) kapsam-aşımı olur. Pad edilecek
  test-edilmemiş yüzey YOK.
- **Promote-on-need**: Tam-perceptual FLIP (Andersson 2021 tam HDR pipeline,
  bugün FLIP-lite), SSIM'in multi-scale (MS-SSIM) varyantı, ya da GPU-tarafı diff
  (büyük golden-set CI hızlandırma) gerçek bir golden-test-pipeline ihtiyacıyla +
  ayrı ADR ile gelir; ImageView / DiffReport / compare/highlight/passes yüzeyi sabit
  kalır.

---

## 3. Reddedilen alternatifler

- **asset umbrella'sının yüzeyini pad etmek**: 11 loader + asset-runtime altyapısı
  zaten 195 testle (15 dosya, her loader round-trip + negative + boundary) kapalı;
  baseline "real loaders, deeply tested". Yapay test "topup only a genuinely-untested
  loader branch if one exists … don't pad" kuralına aykırı; test-edilmemiş loader
  dalı BULUNMADI. RED — saf SEAL, 0 yeni test.
- **asset STREAMER'larını (texture/scene/audio) bu mühürde ele almak/sealemek**:
  brief açıkça "NOT the asset STREAMERS (… Band 6)" + "this is the UMBRELLA + core
  loaders, NOT the streamers". Streamer'ların decode-payload boşluğu (Sprint-2
  placeholder) gerçek bir Band-6 feature-build'idir, bu mühürün kapsamı değil. RED.
- **asset alt-band kütüphanelerini (texture_compress/material_authoring/shader_cache/
  vfx_authoring/validator/streamer_pool/runtime) düzenlemek**: bunlar Band 3–4'te ve
  bu dispatch'in açıkça DIŞINDA. RED.
- **script için Engine.cpp'nin tüm zaten-test-dolu yüzeyine ek test yığmak**:
  run/global/sandbox-cap/binding zaten test-dolu (41 test); yalnız 5 GERÇEKTEN
  test-dışı dal vardı (nil-call + 2 return-fallback + rebind + run_file-compile).
  Sadece o 5'i eklemek brief'in "ONLY if a real untested branch" kuralıyla birebir;
  fazlası pad olurdu. RED (pad) — 5 hedefli test, ne eksik ne fazla.
- **imgdiff'e engine-runtime derinliği (GPU diff / tam-perceptual FLIP) eklemek**:
  imgdiff bir TOOLING kütüphanesidir; roadmap cross-band note "tooling libs reach 100
  by completing+sealing the tool, not by engine-runtime depth". Araç olarak tam +
  32-test. RED — tooling-100 SEAL; tam-perceptual FLIP promote-on-need.
- **engine/asset/'in DIŞINA, engine/script/ + engine/imgdiff/ DIŞINA, ya da
  rhi/samples/hello_*/% docs'a dokunmak**: kapsam DIŞI (brief: "work ONLY in
  engine/asset/ + engine/script/ + engine/imgdiff/ + docs/ADR/"). RED.

## 4. Sonuçlar

- (+) 3/3 BAND-2 asset+tooling (Band-2-subset) kütüphanesi honest-rule terminal
  durumuna geçti: asset + imgdiff çekirdek-IMPLEMENTED + zaten-derin-test
  (charter-complete) + bu mühürle SEALED; script çekirdek-IMPLEMENTED + 5 gerçek
  error-path/edge dalı IMPLEMENTED+test + kalanı SEALED. Hiçbir kütüphanede
  placeholder/TODO terminal-olmayan-durum kalmadı; her mühür promote-on-need çıkış
  kapısı taşır. Bu, Band 2'nin SON grubudur.
- (+) script: 5 hedefli error-path/edge test (nil-global call / numeric+mixed
  non-matching-return fallback / register_function rebind / run_file compile-error)
  Engine.cpp'nin daha önce test-edilmemiş dallarını kapattı — script test sayısı
  41 → 46. asset + imgdiff: 0 kaynak / 0 yeni test (saf SEAL — gerçek bir
  test-edilmemiş loader-dalı / araç-yüzeyi BULUNMADI). Brief'in "topup ONLY where a
  real surface gap exists, don't pad" kuralı tam uygulandı.
- (+) Chrome golden BYTE-IDENTICAL (fixture #5, baseline ile `cmp` eşit). Yalnız bir
  TEST dosyası (engine/script/tests/test_script.cpp) + bu ADR değişti; hiçbir render
  kaynağı/shader/hello_engine binary'si değişmediğinden render-yolu aynen korunur.
  Build -Werror temiz, 0 yeni clang-tidy WAE class (eklenen test satırları C++23 +
  proje konvansiyonuna uygun).
- (−) Mühürler asset yeni-format-import, script per-Engine memory-cap / table-variant,
  imgdiff tam-perceptual-FLIP gibi feature'ları bu pass'te ÜRETMEZ; prerequisite/
  consumer doğunca ayrı ADR'larla gelir. Kabul: bu üç kütüphane baseline'ın güçlü
  çekirdeğidir; brief açıkça "to-100 is mostly a charter-complete SEAL". Grubun
  gerçek feature-build kalanı (asset streamer'ları Band 6, texture_compress/validator/
  vb. alt band'ler) bu dispatch'in dışındadır.

---

## Varsayımlar

- Brief'in "100% RULE per lib" yorumu (band1 + band2 kardeş ADR'larıyla aynı):
  küçük/net/tüketicisi-olan/temiz boşluğu implement et, consumer/prerequisite-
  gerektiren'i seal et. asset + imgdiff'te temiz-implement-edilebilir bir boşluk
  BULUNMADI (charter-tam) → SAF SEAL; script'te 5 gerçek error-path/edge dalı vardı
  → IMPLEMENTED+test, kalan SEALED.
- "Test-topup ONLY where a real surface gap exists / topup only a genuinely-untested
  loader/operator branch if one exists, don't pad" kuralı: asset (10 loader + altyapı,
  195 test) + imgdiff (3 algo + compare/highlight + 3 hata kodu, 32 test) round-trip+
  negative+boundary test-dolu → 0 test. script'te Engine.cpp tarandı, 5 GERÇEKTEN
  test-dışı dal saptandı (nil-call / numeric-fallback / mixed-fallback / rebind /
  run_file-compile) → tam 5 test (pad değil).
- ADR dosya-adı `ADR-20260616-band2-asset-scope.md` + tarih 2026-06-16: band-cohort
  kardeş ADR'larıyla (band1/foundation/game/render-core/render-features/world/ui,
  hepsi 20260616) hizalı; HEAD f461b14 (phase1235 band2-ui seal'lerinin üstü).
- "engine/asset/ (umbrella+core loaders, STREAMER'lar HARİÇ) + engine/script/ +
  engine/imgdiff/ + docs/ADR DIŞINA dokunma" kuralı uygulandı: tek kaynak/test
  değişikliği engine/script/tests/test_script.cpp (+5 test +3 include); tek yeni
  dosya bu ADR. asset STREAMER'ları + alt-band asset libs + samples/ + hello_* +
  diğer gruplar + % docs'a DOKUNULMADI. hello_engine RELINK gerekmedi (hiçbir
  engine header/render binary değişmedi) → golden byte-identical garantisi
  (capture ile de doğrulandı: `cmp baseline.png b2a.png` → identical, b2a.png silindi).

## Sonraki

- BAND 2 asset+tooling'in geri kalan partial/skeleton kütüphaneleri
  (texture_compress 70 / material_authoring 70 / texture_synth 70 / shader_cache 65 /
  vfx_authoring 65 / streamer_pool 65 / validator 60 / runtime 55) DAHA DÜŞÜK
  band'lerde (3–4); kendi band dispatch'lerinde gerçek-impl-derinliği ile ele alınır.
- Asset STREAMER'ları (texture_streamer/scene_streamer/audio_streamer, 45%) Band 6;
  highest-leverage olarak "wire the real WAV/glTF/cdtex decode behind the done
  orchestration" ile birlikte (aynı fix-shape) yapılır — bu üç asset core-loader'ı
  (Image/Wav/GltfLoader/CdTex) o decode-payload'ların REFERANS-loader'ıdır.
- script per-Engine memory-cap promote tetikleyicisi: byte-budget'a ihtiyaç duyan bir
  script-heavy gameplay tüketicisi + table/nested-variant genişlemesi; o landıktan
  sonra ayrı ADR.
- imgdiff tam-perceptual-FLIP / MS-SSIM / GPU-diff: büyük golden-test-pipeline
  ihtiyacı doğunca ayrı ADR.
