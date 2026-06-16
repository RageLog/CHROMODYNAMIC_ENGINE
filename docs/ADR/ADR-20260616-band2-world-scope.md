# ADR-20260616 — ALL-MODULES-TO-100 BAND 2 / WORLD Kapsam Mührü (16 kütüphane)

- **Status**: Accepted
- **Date**: 2026-06-16
- **Branch**: dev (HEAD 03ae03b)
- **Deciders**: Cemal TATLI
- **Author**: Developer (BAND 2 world group close-out — impl-where-clean + test-deepening + kapsam mührü pass)
- **Related**:
  - `docs/ROADMAP_ALL_MODULES_TO_100.md` (BAND 2 listesi + "100% = IMPLEMENTED+test
    YA DA formally SEALED" honest-rule, §"What 100% MEANS"; world named gap:
    audio/audio_spatial non-Windows backend verification §BAND 2 satır)
  - `docs/PROJECT_COMPLETION_STATUS.md` §6 (engine/world baseline %'leri + Basis
    sütunu; grup rollup ~81%, "Mature and uniform — ~660 gtests cluster-wide,
    all libs ship real impl ... caveats are all documented")
  - `docs/ADR/ADR-20260616-band1-scope.md` + `ADR-20260616-band2-foundation-scope.md`
    + `ADR-20260616-band2-game-scope.md` + `ADR-20260616-band2-render-core-scope.md`
    + `ADR-20260616-band2-render-features-scope.md` (kardeş band-mühür ADR'ları,
    aynı şablon: seal-with-promote-on-need + targeted-topup)
- **Scope guard**: Bu ADR YALNIZ kapsam-karar dokümanıdır. Mühürlenen maddeler
  "deferred-by-design"dır — açık (open) sayılmazlar. Her madde "ihtiyaç doğunca
  promote" çıkış kapısı taşır. Bu pass'te SIFIR yeni lib implementasyonu yapıldı
  (16 kütüphanenin çekirdeği zaten production-grade'di); yalnız gerçekten
  test-edilmemiş load-bearing dalı olan 2 kütüphaneye (net SACK/out-of-order/RTT,
  world_container deactivate-survivor) hedefli edge test eklendi + bu mühür kayda
  geçirildi. samples/ + hello_* + % docs'a + diğer gruplara DOKUNULMADI.
  (NOT: sample_framework engine/world altında bir world lib'dir — samples/ dizini
  DEĞİL — ve kapsam İÇİNDEDİR.)

---

## 1. Bağlam

BAND 2 (80–89%) world grubu 16 kütüphaneyi 100%'e taşır: ecs 88, scene 88,
anim 85, anim_ik 85, gameplay_input_binding 85, gameplay_time 85, input 85,
net 85, physics 85, net_lobby 82, world_container 82, audio 80, audio_spatial
80, net_matchmaker 80, physics_soft_body 80, sample_framework 80.
(net_session_replay 78, physics_jolt 78, audio_dsp_fx 75, physics_vehicle 75,
particle_system 65 bu grubun DIŞINDA — band3/band4.)

Bu, baseline raporunun "mature and uniform, ~660 gtests cluster-wide, all libs
ship real impl" dediği gruptur: her kütüphanenin substantive bir .cpp/header-
inline impl'i, dokümante bir formatı, akademik atıfları ve adanmış gtest
binary'leri var; tüm "stub/TODO" hit'leri dokümante fallback yollarındadır,
sessiz boşluk değil. Yani çoğu için 100% = charter-complete'i + dokümante
design-scope'u kabul eden bir mühür + YALNIZ gerçek bir yüzey-boşluğu olan
yerde hedefli test-topup.

Roadmap honest-rule'una göre bir modül 100%'tür ancak HER dokümante boşluk iki
terminal durumdan birindeyse: (a) IMPLEMENTED + test (küçük/net/tüketicisi olan),
VEYA (b) bir paragraflık kapsam-ADR'ı ile formally SEALED (promote-on-need
tetikleyicisiyle). "TODO/placeholder" üçüncü durumu kalamaz.

Bu pass'in bölünme çizgisi: 16 kütüphanenin tamamı charter-complete idi. Tek
adı geçen feature-boşluğu audio'nun non-Windows backend doğrulamasıydı — ve
inceleme bunun bir impl-boşluğu DEĞİL bir platform-host-doğrulama boşluğu
olduğunu gösterdi (§2.12 detay): CoreAudio (AudioUnit) ve ALSA (snd_pcm)
backend'leri zaten GERÇEK, tam implementasyonlardır — `#if __APPLE__` /
`#if __linux__` ile platform-gated'dir; bu Windows host'ta `return {}` fabrika
olarak derlenirler (doğru). Yani named gap "Win-first SEAL + real-but-platform-
gated backend'leri + derin-test-edilmiş HRTF/mix yolunu kayda geçir" ile çözülür.

Geri kalan 15'te yalnız gerçekten test-edilmemiş load-bearing dalı olan 2
kütüphaneye odaklı test eklendi: **net** ReliableChannel'in SACK gap-fill +
out-of-order receiver-window + RTT-ölçüm dalları (built-in loopback in-order
teslim ettiği için ulaşılamayan dallar), ve **world_container** LevelStreamer'in
`deactivate()` persistent-survivor dalı + inactive no-op early-return. Geri kalan
yüzey zaten round-trip + negative + boundary + degenerate testleriyle doluydu,
pad EDİLMEDİ.

---

## 2. Karar — kütüphane başına bir bölüm

### 2.1 cd::ecs (world/ecs) — 88 → 100

- **Bağlam**: ~2.1k LOC hdr-inline (sparse-set World 346 + Scheduler 407 +
  ArchetypeWorld 543 side-layer + SystemGraph + QueryCache structural-version);
  179 .cpp; 77 gtest / 5 dosya (test_ecs + test_query_cache + test_scheduler +
  test_archetype_world + test_sphere_query). Named candidate (brief):
  "change-detection/scheduler depth".
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi. Yeni test eklenmedi.
  **SEALED**: charter tam.
- **Gerekçe**: "change-detection" diye ayrı bir alt-sistem YOK; bu, QueryCache'in
  structural-version mekanizmasıdır ve zaten 8 adanmış testle kapalı
  (StructuralVersionBumpsWhenNewComponentTypeAppears, IsStaleDetectsLate-
  ComponentRegistration, AutoRefreshIsNoOpWhenVersionUnchanged dahil).
  Scheduler 11 testle (write-before-read order, conflict-chain, parallel-tick ==
  sequential, thread-pool-backed parity dahil) kapalı. Dual storage (sparse-set
  + archetype) round-trip add/remove + chunk-overflow + 16KiB-target testli.
  Pad etmek "don't pad" kuralına aykırı olurdu.
- **Promote-on-need**: Bevy-tarzı per-component `Changed<T>`/`Added<T>` tick-
  versioned change-detection, observer/hook reaction sistemi, ya da relationship/
  hierarchy component'leri gerçek bir gameplay/editor tüketicisiyle + ayrı ADR
  ile gelir; World/Scheduler/ArchetypeWorld/QueryCache yüzeyi sabit kalır.

### 2.2 cd::scene (world/scene) — 88 → 100

- **Bağlam**: ~2.1k LOC / 21 hdr (Scene graph + Serializer 314 + BinarySerializer
  259 + Frustum/SpatialHash/Light/Trigger); 132 .cpp; 86 gtest. (Ayrıca
  test_scene_loader + test_scene_ingest ayrı binary'ler.)
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi (graph parent/child
  + transform propagation + JSON ve binary serialize round-trip + frustum/spatial-
  hash query + destroy-node child-cascade). Yeni test eklenmedi. **SEALED**:
  charter tam.
- **Gerekçe**: Scene-graph + iki serializer ailesi + spatial yapıların yüzeyi
  zaten round-trip + negative testle kapalı; pad olurdu. (deserialize_scene_with
  hook'u world_container §2.11 LevelStreamer tarafından tüketiliyor — köprü
  zaten test-dolu.)
- **Promote-on-need**: Prefab/nested-scene instancing, delta/diff serialize, ya
  da streaming-friendly chunked scene format gerçek bir büyük-world tüketicisiyle
  + ayrı ADR ile gelir; Scene + Serializer API yüzeyi sabit kalır.

### 2.3 cd::anim (world/anim) — 85 → 100

- **Bağlam**: ~1.7k LOC hdr-inline (DualQuat/Kavan skinning, StateMachine,
  BlendTree2, PoseBlend, AdditiveBlend, BoneMask, CurveTrack, Skeleton,
  GpuSkinning host-side data prep); 61 gtest (test_anim + test_dual_quat).
  Named candidate (brief): "DQS/blend boundary".
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi. Yeni test eklenmedi.
  **SEALED**: charter tam.
- **Gerekçe**: DQS (Kavan dual-quaternion) yüzeyi zaten boundary-dolu: blend
  weight-0/weight-1 endpoint'leri, antipodality-flip cancellation-önleme, LBS-vs-
  DQS hacim-koruma (90° elbow), Mat4 round-trip. Blend yüzeyi de boundary-dolu:
  BlendTree2 zero/full/half-weight + mismatched-size reject, AdditiveBlend
  zero/full + size-mismatch reject, PoseBlend lerp + filter-skip + additive,
  StateMachine blend-completes-after-duration. İki ayrı binary'de kapalı; pad
  olurdu.
- **Promote-on-need**: GPU-side skinning dispatch (host data prep zaten var,
  RHI dispatch cd::rhi'nin işi), motion-matching/pose-search, ya da IK-anim
  layered pass (cd::anim_ik köprüsü) gerçek bir animation-runtime tüketicisiyle
  + ayrı ADR ile gelir; DualQuat/BlendTree/PoseBlend yüzeyi sabit kalır.

### 2.4 cd::anim_ik (world/anim_ik) — 85 → 100

- **Bağlam**: 492 LOC CCD solver + joint limits (Sprint-2); 294 hdr; 11 gtest
  (reach-converge, out-of-reach best-effort, single-joint, empty-chain no-crash,
  loose-threshold-faster, config-override, unconstrained==Sprint1, constrained-
  stays-within-limits, knee-preset-blocks-backward, multi-constrained-together).
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi. Yeni test eklenmedi.
  **SEALED**: charter tam.
- **Gerekçe**: CCD convergence + best-effort + degenerate (target-at-root, empty-
  chain) + joint-limit clamp (knee backward-bend block dahil) yüzeyi zaten 11
  testle kapalı; pad olurdu. FABRIK alternatif solver'ı baseline'da "not present"
  olarak dokümante — bir feature, boşluk değil.
- **Promote-on-need**: FABRIK solver (CCD'ye alternatif convergence profili),
  pole-vector/twist constraint, ya da full-body IK rig gerçek bir character-anim
  tüketicisiyle + ayrı ADR ile gelir; CcdSolver + ChainSettings yüzeyi sabit.

### 2.5 cd::gameplay_input_binding (world/gameplay_input_binding) — 85 → 100

- **Bağlam**: 269 LOC ActionMap (axis/button binding, threshold heuristics,
  multi-device, rebind, callback-once-not-continuous); 245 hdr; 11 gtest.
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi. Yeni test eklenmedi.
  **SEALED**: charter tam.
- **Gerekçe**: Bind/unbind/rebind + axis-float + multi-action-same-key + multi-
  device-same-action + duplicate-binding-reject + cleared-map + callback-once
  yüzeyi zaten 11 testle kapalı; pad olurdu.
- **Promote-on-need**: Context/layer stack (gameplay vs UI vs menu), runtime
  rebinding-UI persistence (cd::config köprüsü), ya da combo/chord sequence
  (cd::input KeyChord köprüsü) gerçek bir input-settings tüketicisiyle + ayrı ADR
  ile gelir; ActionMap yüzeyi sabit kalır.

### 2.6 cd::gameplay_time (world/gameplay_time) — 85 → 100

- **Bağlam**: 82 LOC Time.cpp (game clock + scale + pause + per-channel); 154 hdr;
  10 gtest (initial-zero, tick-advances, pause/resume, scale-2x/0.5x, frame-index-
  even-when-paused, multi-channel-independent, negative-dt-reject, negative-scale-
  clamp, reset).
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi. Yeni test eklenmedi.
  **SEALED**: charter tam.
- **Gerekçe**: Clock/scale/pause/multi-channel + negatif-dt/scale defensive
  yüzeyi zaten 10 testle kapalı; pad olurdu. Baseline'ın "fixed-step" notu game-
  clock'un scale/pause davranışını niteler — ayrı bir fixed-step accumulator
  alt-sistemi YOK (o, FramePacer'ın cd::time/cd::foundation tarafındaki işi);
  burada bir boşluk değil.
- **Promote-on-need**: Fixed-timestep accumulator + interpolation-alpha (Gaffer-
  tarzı), rewind/replay clock, ya da network-synced sim clock gerçek bir
  gameplay-sim tüketicisiyle + ayrı ADR ile gelir; GameClock channel API sabit.

### 2.7 cd::input (world/input) — 85 → 100

- **Bağlam**: 818 LOC / 9 hdr (Axis/Hold/DoubleClick/KeyChord/Gamepad/MouseDrag
  header-inline FSM) + thin 38-LOC pump; 38 gtest.
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi. Yeni test eklenmedi.
  **SEALED**: charter tam.
- **Gerekçe**: Axis dead-zone/normalize + hold-timer + double-click-window +
  key-chord sequence + gamepad-stick + mouse-drag FSM yüzeyi zaten 38 testle
  (header-inline FSM'lerin her geçişi) kapalı; pad olurdu.
- **Promote-on-need**: Haptic/rumble feedback, touch/gesture multi-finger, ya da
  raw-input HID device-enumeration gerçek bir platform-input tüketicisiyle + ayrı
  ADR ile gelir; FSM detector yüzeyi sabit kalır.

### 2.8 cd::net (world/net) — 85 → 100  [test-topup + SEALED]

- **Bağlam**: ~2.4k LOC hdr-inline (ReliableChannel/AckWindowChannel 390 +
  Retransmit 566 + QosDispatcher + SnapshotReconciler + Delta + Prediction + RLE)
  + real UdpConnection 254 (winsock); 89 gtest / 4 dosya. Named candidate (brief):
  "reliable-channel retransmit edge".
- **Karar**: Çekirdek IMPLEMENTED + zaten test edildi. Bu pass'te 2 gerçek
  test-edilmemiş load-bearing AckWindowChannel dalı kapandı:
  **OutOfOrderArrivalBuffersUntilGapFills** (frame'ler 3,1,2 sırasıyla teslim →
  receiver 3'ü inbox'ta tutar, gap dolunca sıralı flush → `flush_delivery_` gap-
  hold + `handle_frame_` ack-window shift dalı), ve
  **SelectiveAckDischargesGapAndMeasuresRtt** (receiver seq 1 ve 3'ü ack'ler,
  seq 2 in-flight → sender SACK ile 1+3'ü pending'den düşürür, YALNIZ 2'yi tutar
  → `discharge_pending_`'in `ack_bits` selective-ack dalı; ayrıca first-transmit
  ack'te `last_rtt()` ölçümü). **SEALED**: geri kalan yüzey (round-trip, in-order
  delivery, cumulative-ack drain, retransmit-after-RTO, retransmit-exhaust-drop,
  duplicate-no-double-deliver) zaten test-dolu.
- **Gerekçe**: SACK gap-fill, out-of-order receiver-window ve RTT-ölçüm dalları
  reliable-transport'un ÇEKİRDEK değer dallarıydı (paket reorder + selective loss
  altında doğruluk) ve doğrudan iddia EDİLMEMİŞTİ — built-in loopback strictly
  in-order teslim ettiği için ulaşılamıyorlardı. Topup, test-only deterministik
  bir `ReorderTransport` (IConnection impl) ekledi: test frame teslim sırasını
  elle kontrol eder → sleep YOK, timing-race YOK (anti-flakiness korundu). Lib
  src/include'una DOKUNULMADI (yeni implementasyon yok; mevcut dallar zaten
  doğruydu, sadece test-edilmemişti).
- **Promote-on-need**: Congestion-control (BBR/Cubic), MTU-discovery/fragmentasyon,
  ya da encryption/DTLS gerçek bir online-multiplayer tüketicisiyle + ayrı ADR
  ile gelir; AckWindowChannel + UdpConnection yüzeyi sabit kalır.

### 2.9 cd::physics (world/physics) — 85 → 100

- **Bağlam**: ~900 LOC / 14 collision primitive (Aabb/Obb/Capsule/Sphere/Ray/
  Triangle/Sweep/Inertia + IPhysicsWorld); 194 .cpp; 55 gtest.
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi (her primitif çifti
  intersect + sweep/CCD + inertia-tensor + ray-cast). Yeni test eklenmedi.
  **SEALED**: charter tam.
- **Gerekçe**: 14 collision primitifinin pairwise + sweep + inertia yüzeyi zaten
  55 testle kapalı; pad olurdu. Tam rigid-body dinamiği + constraint solver
  cd::physics_jolt'un (band3, build-flag-gated Jolt 5.x) işi — bilinçli ayrım,
  bu kütüphane collision-primitive katmanıdır.
- **Promote-on-need**: GJK/EPA convex-convex, continuous-collision broad-phase
  (BVH/sweep-and-prune), ya da contact-manifold generation gerçek bir physics-
  runtime tüketicisiyle + ayrı ADR ile gelir; primitive intersect API sabit.

### 2.10 cd::net_lobby (world/net_lobby) — 82 → 100

- **Bağlam**: 464 src (Lobby 168 + Packet 132 + SocketTransport 133); 507 hdr;
  20 gtest / 3 dosya (test_lobby + test_lobby_socket_transport + test_packet_
  transport).
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi (lobby create/join/
  leave/close + packet serialize round-trip + socket transport send/recv).
  Yeni test eklenmedi. **SEALED**: charter tam.
- **Gerekçe**: Lobby lifecycle + packet wire-format + socket transport yüzeyi
  zaten 3 binary'de 20 testle kapalı; pad olurdu.
- **Promote-on-need**: Host-migration, lobby-list discovery/browsing, ya da
  voice-chat channel gerçek bir online-lobby tüketicisiyle + ayrı ADR ile gelir;
  Lobby + Packet + SocketTransport yüzeyi sabit kalır.

### 2.11 cd::world_container (world/world_container) — 82 → 100  [test-topup + SEALED]

- **Bağlam**: header-only 946 LOC / 7 (LevelStreamer 188 residency + ProjectIo
  363 JSON + Level/Layer/Project/World/LayerMember); 19→20 gtest. Named gap
  (baseline): "v1.7 level-switch streaming".
- **Karar**: Çekirdek IMPLEMENTED + zaten test edildi. Bu pass'te 1 gerçek
  test-edilmemiş LevelStreamer dalı kapandı:
  **DeactivateKeepsPersistentOfActiveLevelAndNoOpWhenInactive** —
  mevcut SwitchDestroys... testi `deactivate()`'i YALNIZ "active level hiçbir
  şeyi persistent işaretlemiyor → her şey ölür" dalında kapsıyordu; bu test
  iki test-edilmemiş dalı kapatır: (a) **persistent-survivor dalı** (active
  level "Keep" layer'ını persistent işaretler → o entity `deactivate()`'i
  SAĞ-KALIR ve tracked kalır, Default entity ölür → `survivors.push_back` satırı),
  ve (b) **inactive no-op early-return** (activate'ten önce `deactivate()` →
  crash YOK, inactive + sıfır-tracked kalır → `!active_valid_ return` dalı).
  **SEALED**: geri kalan yüzey (activate switch + persistent survival across
  switch + bad-level/missing-file reject + ProjectIo round-trip/schema-reject/
  atomic-write + LayerMember rename-migrate) zaten test-dolu.
- **Gerekçe**: `deactivate()` persistent-survivor dalı ve inactive no-op residency-
  yönetiminin load-bearing dallarıydı (level-switch'in unload yarısı — survivor
  korunmazsa persistent NPC/UI/player silinir) ve doğrudan iddia EDİLMEMİŞTİ.
  Topup deterministik bir temp .cdscene yazar + iki dalı doğrular; sleep YOK.
  Lib header'ına DOKUNULMADI (yeni implementasyon yok; dal zaten doğruydu).
- **Promote-on-need**: Distance-based chunk streaming (header banner: "builds on
  the same tracking later"), async-load level-switch (load-screen-free), ya da
  level-LOD/sublevel hierarchy gerçek bir open-world tüketicisiyle + ayrı ADR ile
  gelir; LevelStreamer + Project/Level/Layer yüzeyi sabit kalır.

### 2.12 cd::audio (world/audio) — 80 → 100  [SEALED Win-first + real-platform-gated backends]

- **Bağlam**: ~1.6k src / 6 backend (WASAPI 543 + CoreAudio 302 + ALSA 275 +
  FileSink + Null + Native dispatcher) + Mixer/DspGraph/HRTF/reverb/limiter/
  compressor/pan/pitch DSP zinciri; 86 gtest. Named gap (roadmap): "non-Windows
  backend verification (CoreAudio/ALSA) or seal Win-first + HRTF dataset test".
- **Karar**: **SEALED Win-first**. İnceleme named-gap'in bir IMPL boşluğu DEĞİL
  bir PLATFORM-HOST doğrulama boşluğu olduğunu netleştirdi → SEAL doğru terminal
  durum. Yeni impl/test eklenmedi (yüzey zaten 86 testle dolu).
- **Gerekçe (üç gözlem)**:
  1. **CoreAudio/ALSA STUB DEĞİL — gerçek implementasyon, platform-gated**:
     CoreAudioBackend.cpp `#if __APPLE__` altında tam bir AudioUnit
     (DefaultOutput) render-callback mikser implementasyonudur (clip lifecycle +
     voice + mono→stereo mix + master-volume, OS'in high-prio IO thread'inde);
     AlsaBackend.cpp `#if __linux__` altında tam bir snd_pcm push-loop
     implementasyonudur (dedicated worker thread + writei + recover). İkisi de
     non-host platformda `return {}` fabrika olarak derlenir (Wave 34/79/80
     contract, doğru). Yani bu Windows host'ta runtime-doğrulanamaz olmaları
     bir kod-boşluğu değil, donanım/OS-host yokluğudur — RHI Metal-on-Mac
     HW-gated doğrulamasıyla simetrik (BAND 0 notu).
  2. **HRTF/mix tested path DERİN**: Positional (ITD/ILD/distance/constant-power),
     PositionalSource (prime/process/ITD-delay/reset), Surround (mono→7.1, FC/FR/
     rear favouring, no-zero-channel), AnalyticalHRTF (symmetric/L-R delay/unit-
     impulse), HrtfConvolver (impulse-reproduces-coeff/reset/size-mismatch-noop),
     DspGraph (gain/lowpass/highpass/chain-order/reset), FirReverb/SimpleReverb,
     Limiter/Compressor/LowPass/PanLaw/PitchShift/Mixer/Voice — 86 testle kapalı.
     audio_spatial (§2.13) ek 23 testle HRTF mix yolunu pekiştirir.
  3. **WASAPI Win-verified**: WasapiPushStream create/push/destroy + invalid-
     arg-reject + unknown-stream-reject testleri `#if _WIN32` altında (CI'da
     device yoksa GTEST_SKIP). NativeBackend dispatcher KindMatchesHostPlatform
     ile Windows'ta WASAPI||NullFallback, diğer host'ta NullFallback doğrular.
- **Not (stale comment, kapsam-dışı düzeltme)**: test_audio.cpp:194 yorumu
  "CoreAudio/ALSA ... are stubs" der — bu, backend'ler gerçek impl'e
  yükseltildikten sonra (Wave 79/80) güncellenmemiş bir YORUM'dur, davranış değil
  (test mantığı doğru: non-host'ta `return {}` → null beklenir). Yorum
  düzeltmesi dokunulmamış-satır olduğundan ve davranışı etkilemediğinden bu
  pass'te EDİLMEDİ; doğru durum bu ADR'de (authoritative scope record) kayıtlı.
- **Promote-on-need**: macOS/Linux host'ta CoreAudio/ALSA backend'lerinin
  device-out runtime doğrulaması (operatör/self-hosted-CI HW doğunca, RHI
  Metal-on-Mac doğrulamasıyla aynı sınıf), MeasuredHRTF (gerçek SOFA/IRCAM
  dataset, bugün AnalyticalHRTF), ya da WASAPI-exclusive/low-latency mode gerçek
  bir audio-runtime/host doğunca + ayrı ADR ile gelir; IAudioBackend +
  Native dispatcher + DSP yüzeyi sabit kalır.

### 2.13 cd::audio_spatial (world/audio_spatial) — 80 → 100

- **Bağlam**: 705 src (HrtfMixer 437 + AudioSpatial 268); 23 gtest / 2 dosya
  (test_hrtf_mixer + test_audio_spatial). HrtfMixer OpenAL-opsiyonel
  (CD_AUDIO_SPATIAL_HAS_OPENAL gate).
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi. Yeni test eklenmedi.
  **SEALED**: charter tam.
- **Gerekçe**: ILD pan (left/right/front-balanced) + distance attenuation (inner-
  unity/outer-zero) + Doppler (toward-raises/away-lowers pitch) + multi-source-
  independent + add/remove/update lifecycle + HrtfMixer init/source-mgmt/shutdown-
  reinit round-trip yüzeyi zaten 23 testle kapalı; pad olurdu. OpenAL gate-test'li
  (HasOpenALFlagIsBooleanInt) — bir feature-gate, boşluk değil.
- **Promote-on-need**: Gerçek OpenAL-Soft HRTF backend runtime doğrulaması (gate
  açıkken), occlusion/obstruction ray-cast (cd::physics/scene köprüsü), ya da
  ambisonics/B-format decode gerçek bir 3D-audio tüketicisiyle + ayrı ADR ile
  gelir; HrtfMixer + AudioSpatial yüzeyi sabit kalır.

### 2.14 cd::net_matchmaker (world/net_matchmaker) — 80 → 100

- **Bağlam**: 302 .cpp (SkillScorer + FillStrategy, phase784); 283 hdr; 11 gtest.
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi (lobby lifecycle +
  skill-within/outside-delta + region-respected + prefer-most-populated + empty-
  lobby-no-constraints + skill-delta-penalised + history-avoidance-penalised +
  fast-fill-first-match + balanced-strategy region-distance). Yeni test eklenmedi.
  **SEALED**: charter tam.
- **Gerekçe**: Skill-based matchmaking + fill strategy + region + history-avoidance
  yüzeyi zaten 11 testle kapalı; pad olurdu.
- **Promote-on-need**: Elo/Glicko/TrueSkill rating update (bugün statik skill
  scorer), ticket-based queue + backfill, ya da cross-region latency-estimation
  gerçek bir matchmaking-service tüketicisiyle + ayrı ADR ile gelir; SkillScorer
  + FillStrategy yüzeyi sabit kalır.

### 2.15 cd::physics_soft_body (world/physics_soft_body) — 80 → 100

- **Bağlam**: 451 .cpp PBD/Verlet (Sprint-1) + self-collision (Sprint-2: spatial-
  hash + impulse); 259 hdr; 10 gtest.
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi. Yeni test eklenmedi.
  **SEALED**: charter tam.
- **Gerekçe**: Rope-falls-under-gravity + pinned-particle + distance-constraint-
  preserve + more-iterations-stabilise + apply-force + zero-dt-noop + both-pinned-
  no-NaN (Sprint-1) ve two-close-particles-repel + rope-self-fold-no-penetrate +
  100-particles-under-1ms (Sprint-2 self-collision) yüzeyi zaten 10 testle kapalı;
  pad olurdu. (Anti-flakiness: deterministik step-count, sleep yok; perf-test
  wall-clock budget.)
- **Promote-on-need**: FEM/co-rotational (PBD'ye alternatif), tearing/cutting,
  ya da two-way rigid-coupling gerçek bir destruction/cloth tüketicisiyle + ayrı
  ADR ile gelir; PBD solver + self-collision yüzeyi sabit kalır.

### 2.16 cd::sample_framework (world/sample_framework) — 80 → 100  [intentionally-thin SEALED]

- **Bağlam**: 109 src (App lifecycle 87 + run 22); 156 hdr; 5 gtest (boot-frame-
  shutdown single-frame, multi-frame-pump frame-index-advance, request-shutdown-
  breaks-loop-early, boot-failure-skips-pump-still-shuts-down, run-template-entry-
  point-drives-app). Roadmap named: "seal intentionally-thin harness scope".
  (NOT: bu engine/world altında bir kütüphanedir — samples/ dizini DEĞİL — ve
  kapsam İÇİNDEDİR.)
- **Karar**: Çekirdek IMPLEMENTED + zaten test edildi. Yeni test eklenmedi.
  **SEALED**: bilinçli-thin app-harness charter'ı tam.
- **Gerekçe**: App lifecycle (boot→pump→shutdown) + frame-index ilerleme +
  early-shutdown + boot-failure graceful + run<App>() entry-point template
  yüzeyi zaten 5 testle kapalı. Roadmap bunu açıkça "intentionally-thin harness"
  olarak niteler → genişletmek bir scope-değişimi olurdu, pad değil. (M2A
  skeleton, MEMORY Mega-Marathon A+B referansı.)
- **Promote-on-need**: Window/input/RHI device bootstrap entegrasyonu (bugün
  saf lifecycle), arg-parsing/config-load, ya da hot-reload-aware sample-loop
  gerçek bir multi-sample harness tüketicisiyle + ayrı ADR ile gelir; App +
  run<App>() yüzeyi sabit kalır.

---

## 3. Reddedilen alternatifler

- **CoreAudio/ALSA'yı bu pass'te "verify a non-Win backend if cleanly doable"
  ile runtime-doğrulamak**: bu Windows host'ta cleanly-doable DEĞİL — backend'ler
  zaten gerçek impl, platform-gated; macOS/Linux donanımı + audio-device + (CI
  için) self-hosted runner gerektirir (RHI Metal-on-Mac HW-gated doğrulamasıyla
  aynı sınıf). RED — brief'in alternatif clause'u ("OR SEAL Win-first + the
  HRTF/mix tested path") tam bunun için; SEAL seçildi, HRTF/mix yolu 86+23 testle
  zaten dolu doğrulandı.
- **16 charter-complete kütüphanenin yüzeyini pad etmek**: ecs/scene/anim/
  anim_ik/gameplay_input_binding/gameplay_time/input/physics/net_lobby/net_match-
  maker/physics_soft_body/sample_framework/audio_spatial yüzeyi zaten round-trip
  + negative + boundary + degenerate test-dolu (baseline "mature and uniform,
  ~660 gtests, all libs ship real impl"); yapay test eklemek "topup ONLY where a
  real surface gap exists … don't pad" kuralına aykırı. RED — yalnız 2
  kütüphanede (net SACK/out-of-order/RTT, world_container deactivate-survivor)
  gerçek test-edilmemiş load-bearing dal kapandı.
- **test_audio.cpp:194 stale "stubs" yorumunu düzeltmek**: dokunulmamış satır +
  davranışı etkilemiyor (test mantığı doğru) → "don't chase pre-existing in
  untouched lines" kuralı. RED — doğru durum bu ADR'de kayıtlı; yorum düzeltmesi
  ayrı bir docs/comment-pass'in işi.
- **net'in src/include'una dokunmak (SACK/out-of-order dallarını "düzeltmek")**:
  bu dallar zaten DOĞRU implementasyondu, sadece built-in loopback üzerinden
  test-edilemiyorlardı. RED — fix değil, test-erişilebilirliği gerekiyordu;
  topup test-only ReorderTransport ile çözüldü, lib binary'si bayt-aynı.
- **% docs'u / samples'ı (dizin) / hello_*'i / diğer grupları düzenlemek**:
  kapsam DIŞI (brief: "work ONLY in engine/world/<lib>/ + docs/ADR/").
  RED. (sample_framework KARIŞTIRILMAMALI: o engine/world altında bir lib, kapsam
  İÇİNDE.)

## 4. Sonuçlar

- (+) 16/16 BAND-2 world kütüphanesi honest-rule terminal durumuna geçti:
  çekirdek IMPLEMENTED+test, audio named-gap'i (non-Win backend) bir impl-boşluğu
  DEĞİL platform-host-doğrulama boşluğu olarak doğru-teşhis edilip Win-first
  SEALED (+ real-but-platform-gated CoreAudio/ALSA + derin HRTF/mix kayda
  geçirildi), geri kalan charter-complete maddeler dürüst gerekçeyle SEALED,
  promote-on-need çıkış kapısıyla. Hiçbir kütüphanede placeholder/TODO kalmadı.
- (+) Sıfır yeni lib implementasyonu. 3 yeni test, 2 kütüphanede gerçek
  test-edilmemiş load-bearing dalda: net +2 (out-of-order-buffer-gap-fill,
  SACK-discharge-gap + RTT-measure), world_container +1 (deactivate persistent-
  survivor + inactive-noop). Hepsi edge/contract + fail-on-revert; anti-flakiness
  korundu (yeni testlerde sleep_for YOK; net deterministik ReorderTransport ile
  manuel teslim-sırası + manuel tick-timestamp, world_container deterministik
  temp-.cdscene inject).
- (+) Her mühür "promote-on-need" tetikleyici taşır → "deferred-by-design"
  açık-boşluk sayılmaz ama genişleme yolu nettir. net özelinde dallar zaten
  doğruydu (topup yalnız test-erişimi açtı); audio özelinde promote = HW-host
  doğunca runtime device-out doğrulaması.
- (−) Mühürler audio non-Win runtime-doğrulama, ecs Bevy-tarzı change-detection,
  anim_ik FABRIK, net_matchmaker Elo-update gibi feature/doğrulamaları bu pass'te
  ÜRETMEZ; tüketici/HW/profil doğunca ayrı ADR'larla gelir. Kabul: BAND 2 world
  grubu "seal + targeted-topup" karakterinde (baseline'ın "mature and uniform,
  all libs ship real impl" cluster'ı) — büyük feature-item'lar (asset streamer
  decode, froxel de-dup, editor) bu grup DIŞINDA (asset/render-features/ui).

---

## Varsayımlar

- Brief'in "100% RULE per lib" yorumu: küçük/net/tüketicisi-olan boşluğu
  implement et, gerçekten design-scope/large-future/HW-gated olanı seal et — bu
  pass'in bölünme çizgisi (band1 + band2-foundation/game/render-core/render-
  features ADR'larıyla aynı). World grubunda implement-edilebilir net kod-boşluğu
  ÇIKMADI (audio named-gap = platform-host-doğrulama, impl değil) → pass "seal +
  targeted-topup" oldu.
- audio "non-Windows backend verification … if cleanly doable" testi: Windows
  host'ta cleanly-doable DEĞİL (gerçek-ama-platform-gated backend + HW/OS-host
  yokluğu); brief'in alternatif clause'u "OR SEAL Win-first + the HRTF/mix tested
  path" → SEAL seçildi, HRTF/mix derin-test doğrulandı.
- "Topup ONLY where a real surface gap exists, don't pad" kuralı: 14 charter-
  complete kütüphanenin round-trip+negative+boundary+degenerate yüzeyine
  DOKUNULMADI; yalnız net'in SACK/out-of-order/RTT dalları (built-in loopback'in
  ulaşamadığı) ve world_container'ın deactivate-survivor + inactive-noop dalları
  (mevcut testin kapsamadığı) gibi DOĞRUDAN-İDDİA-EDİLMEMİŞ load-bearing dallar
  hedeflendi.
- "samples/hello_*/% docs/diğer-grup dokunma" kuralı uygulandı: tüm değişiklikler
  engine/world/<lib>/tests (2 dosya: net + world_container) + docs/ADR (bu dosya)
  altında. 16 kütüphanenin src/include'larına dokunulmadı (yeni implementasyon
  yok → lib binary'leri bayt-aynı). sample_framework world-lib olarak kapsam
  içinde değerlendirildi (samples/ dizini ile karıştırılmadı).
- Golden byte-identical doğrulaması: world kütüphaneleri rendering yolunu
  etkilemez (yalnız 2 test-dosyası değişti, lib binary'leri bayt-aynı); fixture
  #5 capture (3-frame) baseline.png ile `cmp` bayt-bayt EŞİT doğrulandı, temp
  çıktı silindi.

## Sonraki

- BAND 2'nin geri kalan grupları (asset/ui ~24 kütüphane) bu mühür şablonunu
  (seal-with-promote-on-need + targeted-topup) tekrar kullanabilir; gerçek
  feature-item'lar o gruplarda (cd::framegraph aliasing render-core'da zaten
  ADR'lı; asset streamer decode band6; editor band4).
- Mühürlenen world maddelerinin promote tetikleyicileri: audio non-Win runtime
  doğrulaması için macOS/Linux audio-host (self-hosted CI, RHI Metal-on-Mac ile
  aynı sınıf); net congestion-control/encryption için online-multiplayer; ecs
  change-detection için Bevy-tarzı observer tüketicisi; anim GPU-skinning dispatch
  için RHI render entegrasyonu; world_container chunk-streaming için open-world
  tüketicisi; physics tam-dinamiği için cd::physics_jolt real-path default
  (band3); net_matchmaker rating-update için matchmaking-service.
- audio_dsp_fx (75), physics_jolt (78), physics_vehicle (75), net_session_replay
  (78), particle_system (65) bu grubun BAND-3/BAND-4 üyeleri; band-2 world pass'ine
  dahil DEĞİL (roadmap %80 eşiğinin altında / partial-tier).
