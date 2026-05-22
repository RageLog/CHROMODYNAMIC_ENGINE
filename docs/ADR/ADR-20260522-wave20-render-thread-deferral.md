# ADR-20260522 — Wave 20 (S4.4) Render thread: explicit deferral

## Bağlam

Phase 4 closure ADR (Track D) S4.4 render thread sprint'ini şu risk
değerlendirmesi ile listelemişti:

> 1.5 hf, **HIGH risk (deadlock)** · "Methodology: 5-Why + dedicated
> TSan run + 7-day stress test öncesi merge yok."

Wave 20'de bu işin tam implementasyonu yapılacaktı. Ancak:

1. Test paketi şu an 4 compiler × 50 test = 200/200 yeşil. Render
   thread iyi implement edilmezse deadlock potansiyeli VAR ve test
   coverage anlık yakalamayabilir (race condition tipik olarak
   minutes-to-hours stress'te ortaya çıkar).
2. 7-günlük stress test bir oturuma sığmaz; "best-effort" yapmak
   Phase 4 ADR'nin kendi disiplinine aykırı olur.
3. Render thread implementasyonu için gereken primitive — SPSC ring
   buffer — `cd::concurrency::RingBuffer<T>` olarak zaten mevcut ve
   `RingBuffer.SpscStressUnderProducerConsumer` testi ile doğrulanmış.

Bu ADR Wave 20'nin **bilinçli erteleme** kararını kayıt altına alır.

## Karar

S4.4 render thread implementation **S4.4.b** alt-sprint'ine ertelenir
ve şu şartları yerine getirmeden engine'e merge edilmez:

1. **Detailed design doc:** Mevcut `ADR-20260522-render-thread-design`
   FrameContext API'sini formalleştirir; eklenecek: thread join sırası,
   swapchain re-create iptal koşulları, ImGui multi-thread context
   politikası.
2. **safety-integration audit:** Subagent ile düşmanca race / deadlock
   denetimi.
3. **TSan-only test paketi:** Ayrı bir `ninja-base-tsan` preset CI job'ı.
   Minimum 1000 frame submit/end stress.
4. **7-day overnight stress:** Sürekli "submit + present + reset"
   döngüsü, deadlock veya validation error olmadan.
5. **Reviewer onayı:** Substantial concurrency değişiklikleri için
   ek göz.

Bu şartlar sağlanana kadar render thread implementation **dev'e merge
EDİLMEZ**. Tüm sample'lar şu an main thread'de submit yapıyor — bu
fonksiyonel ve test-edilmiş. Tek thread = predictable.

## Reddedilen alternatifler

* **"Hızlı bir POC yap, sonra cilala":** Render thread bir POC değil;
  ya tam doğru çalışır ya da rastgele deadlock üretir. Yarı-doğru
  implementasyon en kötü tip — test'ler yeşil görür ama production'da
  patlar.
* **Yeterli olmayan smoke test:** Smoke harness 23/23 geçiyor ama
  3-frame headless run, multi-thread race condition'ları tetiklemez.
  Bu kategorinin testleri TSan + long-running stress içerir.
* **`std::async`-based one-frame-ahead pattern:** İlginç ama mevcut
  Renderer API'si frame'i begin/end ile sarıyor — bunu birden çok
  thread'e yaymak için temelden refactor gerekir.

## Sonuçlar

| Metric | Wave 19 sonu | Wave 20 sonu |
|---|---|---|
| Engine libraries | 49 | 49 (değişiklik yok) |
| Samples | 28 | 28 |
| ctest binaries | 50 | 50 |
| Tests internally | ~89 | ~89 |
| Açık deferred sprint'ler | S4.4.b | S4.4.b (formalize edildi) |
| Phase 4 closure ilerlemesi | 6/8 sprint | 6/8 sprint + deferral kayıt |

Marathon Phase 4 yol haritası (Phase 4 closure ADR'den):
* ✅ S4.1 PBR — hello_pbr (Wave 14) + hello_gltf'in PBR pipeline'ı
* ✅ S4.2 Animation — hello_anim (Wave 15)
* ✅ S4.3 Parallel ECS — Scheduler::tick_parallel (Wave 17)
* ⏳ **S4.4 Render thread — DEFERRED (this ADR)**
* ✅ S4.5 Audio — FileSinkAudioBackend + hello_audio_play (Wave 16)
* ✅ S4.6 Skybox — hello_skybox analytical atmosphere (Wave 18)
* ✅ S4.7 Editor polish — hello_inspector save/load (Wave 19)
* → S4.8 Release tag — Wave 21

6/8 sprint'in tamamlandı, 1'i bilinçli erteleme, 1'i devam.

## Açık sorular

* Render thread S4.4.b zamanlaması: Phase 5'in ilk işi mi olmalı,
  yoksa Phase 4'ün gerçek "kapanışından" sonra bağımsız bir
  iyileştirme sprint'i mi?
* Mevcut sample'larda görünür perf bottleneck'ler var mı? Yoksa
  render thread esasen "production okur-yazarlık" geliştirmesi olur
  (özel bir scenario'da görülen yavaşlama olmadan).
* CI'a Lavapipe + render-thread stress job: birlikte mi yoksa
  ayrı mı? Lavapipe MSAA / multi-thread Vulkan support'u var (Mesa
  v24+).
