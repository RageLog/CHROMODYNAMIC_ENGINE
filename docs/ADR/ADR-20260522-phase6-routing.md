# ADR-20260522 — Phase 6 routing: deferred items inventory

## Bağlam

Phase 5 marathon (Waves 23-35) kapanışında bir dizi iş **bilinçli olarak
ertelendi** çünkü:

- Donanım/CI eksikliği (macOS, Linux audio CI runner yok),
- Risk yoğunluğu (concurrency, lifetime invariantları için ek bench/sanitizer
  süresi gerekiyor),
- Kapsam: temel grafik/render özellikleri (Forward+/clustered/volumetric,
  mobile/Vulkan-portability/Metal/D3D12 RHI) ayrı bir mimari sprint hak ediyor.

Bu ADR, ertelenen işleri **Phase 6** etiketi altında tek yerde toplar
ve hangi prereq'leri beklediklerini belgeler. Kapsam dondurma değil —
hangi iş hangi sınıra giriyor: "wave bağımsız", "sample/CI prereq",
"mimari ADR prereq".

## Karar

Aşağıdaki iş kümeleri Phase 6 (v0.6.0+) altında **tek bir mimari sprint
çatısı** içinde yürütülür. Her küme kendi ADR'sini Phase 6 açılışında
yazar — bu ADR ise sadece **kapı listesi**:

### Grafik / Render

| İş | Phase 5 durumu | Phase 6 prereq |
|---|---|---|
| Forward+ / clustered light culling | Yok (basic deferred mevcut) | Mimari ADR + cluster grid + tile light list compute shader |
| Volumetric fog / atmospheric scattering | Yok | Forward+ üzerine kurulu, ek volume texture + ray marcher |
| Mobile / portability RHI (Metal, D3D12) | Sadece Vulkan + RHI abstraction var | Metal IRConverter + D3D12 root signature mapping |
| Full perceptual image diff (FLIP / SSIM) | `cd::imgdiff` pixel-exact var (Wave 33) | Spatial Gaussian + CSF model, ground-truth dataset |
| GPU IBL bakes (irradiance + radiance pre-convolve) | CPU baseline `cd::ibl` (Wave 31) | Compute shader port; bench vs. CPU baseline |

### Audio

| İş | Phase 5 durumu | Phase 6 prereq |
|---|---|---|
| CoreAudio (macOS) implementation | Stub factory (Wave 34) | macOS CI runner + AudioUnit DefaultOutput entegrasyonu |
| ALSA (Linux) implementation | Stub factory (Wave 34) | Linux CI runner + snd_pcm headless test env |
| 3D positional audio + HRTF | Yok (yalnızca 2D mixer) | DSP pipeline ADR + IR convolver |

### Networking

| İş | Phase 5 durumu | Phase 6 prereq |
|---|---|---|
| Retransmit + ACK loop (real UDP reliability) | `ChannelMux` wire+ordering (Wave 32) | Backoff policy ADR + congestion control model |
| Real UDP `IConnection` backend | Loopback var (Wave 26 öncesi), Mux Wave 32 | OS socket wrap (winsock / BSD) + NAT punch test rig |
| WebRTC / SteamNetworkingSockets bridge | Yok | Vendor değerlendirme ADR'si |

### Plugin / Hot-reload

| İş | Phase 5 durumu | Phase 6 prereq |
|---|---|---|
| Native file watcher (inotify / ReadDirectoryChangesW / FSEvents) | Poll-driven `HotReloader` (Wave 35) | Platform-specific watch implementation, threaded notification |
| Symbol-level diff (hot-patch fonksiyon) | Yok | LLVM JIT veya livepp benzeri çözüm ADR'si |

### Diğer

| İş | Phase 5 durumu | Phase 6 prereq |
|---|---|---|
| Scripting tier (Lua / WASM / dynamic) | Yok | ADR-013 (i18n/telemetry) sonrası |
| Render thread proven via 7-day soak | `AsyncSubmitN` API var (Wave 30) | TSan green + multi-day stress test |
| Performance regression bench (perf-track CI job) | Manuel benchmark'lar var | Bench harness + flame-graph artifact |

## Reddedilen alternatifler

- **Phase 5'i kapatmadan bunları yapmak:** Marathon hedefi engine
  bütünlüğünü v0.5.0 baseline'a çekmekti. Yeni bağımlılık / yeni mimari
  ADR gerektiren işler **ayrı sprint** hak ediyor — karışık kapsam
  release notes'u bulanıklaştırır.
- **Tüm ertelenen işleri tek tek ADR'lemek:** 14+ iş kalemi var. Her
  birine inception ADR'si yazmak şu noktada noise. Bu ADR "kapı listesi"
  görevi görür; gerçek ADR Phase 6 açılışında küme bazında yazılır.
- **"Phase 6 = v1.0.0":** v1.0.0 = "production ready" iddiası. Phase 6
  kümeleri bittiğinde v0.6.x → v0.9.x tag dönemine girip ardından 1.0.

## Sonuçlar

Phase 6'ya geçişte bu ADR taban liste. Phase 6 açılış komutu:

```
cd docs/ADR
ls ADR-20260522-phase6-*    # bu listenin altındaki küme-ADR'leri açılır
```

Kümeler bağımsız çalışabilir (Forward+ / mobile RHI / scripting → paralel
sprint'ler). Her küme kendi `v0.X.0` minor bump'unu hak eder; v0.6.0
küme öncelik sıralamasına göre dolar.

## Açık sorular

* **Kümeler arası öncelik:** Kullanıcı hangi kümeyi önce ister? Render
  (Forward+ + mobile) en görünür kalite sıçraması; networking en görünür
  feature jump; plugin hot-reload en yüksek developer-experience kazancı.
* **CI hardware mı yoksa cross-compile + emülasyon mu?** macOS / Linux
  audio için fiziksel runner gerekiyor; cross-compile alone yetmiyor.
* **Tek motor mu çatal mı?** Mobile RHI eklendiğinde Vulkan vs. Metal
  branch budget'i ikiye katlıyor; "tek RHI çatısı" stratejisi ADR-001'i
  yeniden değerlendirmeyi gerektirebilir.
