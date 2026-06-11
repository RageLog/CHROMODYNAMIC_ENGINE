# CHROMODYNAMIC — Eksikler ve Yol Haritası (2026-06-11)

> **Amaç**: Kullanıcı uzun süre uzakta; bu doküman (a) bekleyen
> kararları, (b) tüm bilinen eksikleri, (c) önerilen yürütme sırasını
> tek yerde toplar. Dönüşte bu dokümandaki numaralı kararları
> cevaplamak yeterli — geri kalanı otonom yürütülebilir.
> **Durum anlığı**: dev branch @ phase 1064, 262/262 test, working
> tree temiz. 186 kütüphane, 34 viewport overlay, editor'de
> sürüklenebilir translate gizmo + undo.

---

## A. Mevcut durum (1 bakışta)

Son koşular (phases 1042-1064) şunları kapattı: `cd::debug_line` +
`cd::debug_draw` kütüphane çifti (CPU batch + GPU renderer, 2 tüketici,
X5 hot-reload'lu); `.clang-tidy` ölü-aile config bug'ı (+117 check
dirildi, 267+85+51 site temizlendi); JobGraph double-submission
deadlock'u + ThreadPool 3 latent defect (coroutine frame leak dahil)
kalıcı fix + regresyon netleri; ctest default TIMEOUT; hello_editor'a
grid + selection box + draggable gizmo + EditHistory undo entegrasyonu.

---

## B. KULLANICI KARARI BEKLEYENLER (dönüşte cevaplanacak)

| # | Karar | Bağlam | Bloke ettiği iş |
|---|-------|--------|-----------------|
| K1 | **ROADMAP_PHASE_2 onayı** | docs/ROADMAP_PHASE_2.md (X4/X5/X1-FU-F deep-dive'ları) "user approves" şartlı | X4 + X1-FU-F ADR'lerinin yazımı ve büyük implementasyonlar |
| K2 | **L1 Metal Fork-A** | Run 25 §8.5 verdict: READY-TO-START | Tüm Metal backend hattı |
| K3 | **X5 ADR (20260608) kabulü** | Mekanizma fiilen ÇALIŞIYOR (prim/shadow/line hot-reload canlı); ADR'deki Q1-Q3 resmî kabul bekliyor | Engine-wide shader-on-disk rollout (samples dışına) |
| K4 | **Phase 1020 chrome görsel onayı** | İkinci-bounce bindless fix'i landed; chrome küre görünümünü gözle doğrula | Golden chrome-probe re-bake kararı |
| K5 | **Flicker kaynağı isimlendirme** | 4 ayrı toggle hazır (TAA/MBlur/Grain/Clouds) — birini kapatıp suçluyu söyle | Kök-katman fix'i (capture-driven kurala göre) |
| K6 | **NVIDIA sürücü + WU policy** | 0x9F DRIVER_POWER_STATE_FAILURE ×2 (Windows upgrade sonrası); admin PowerShell komutu daha önce verildi | Gece koşularının kesintisizliği |

---

## C. Eksikler (kategori bazında)

### C1. Engine çekirdeği (otonom yürütülebilir)

- **X1-FU polish paketi** (ADR-20260528 Sonuçlar): A `cv`→`atomic
  wait/notify` (1-2 g), B TSan preset koşusu (1 g), C hazard-pointer
  reclamation WSD (3-5 g), D priority-aware steal (2-3 g), E
  concurrency README (≤1 g), G `upload_buffer` thread-safety spec
  audit (2 g — debug_draw artık çok-tüketicili; önemi arttı).
- **X1-FU-F** Vulkan secondary command buffer (~1 hafta; ICommandBuffer
  surface review + safety-integration zorunlu).
- **X4 D3D12 parity + RT** (3-4 hafta; K1 onayına bağlı): ~%73 parity
  mevcut; image-readback API + SPIRV-Cross/DXIL yolu prereq.
- ThreadPool off-worker coroutine resume hattı: phase 1052 notify
  fix'i latent durumu kapattı ama gerçek off-worker awaiter geldiğinde
  worker-side `destroy()` sahiplik modeli yeniden gözden geçirilmeli
  (şu an tek resumer worker — tasarım notu DebugDraw değil
  ThreadPool.hpp'de).

### C2. Editor (otonom; kullanıcı kültürüne en yüksek değer)

- Gizmo v2: **ray-plane drag metriği** (hello_engine paritesi —
  kamera-paralel hassasiyet), **rotate/scale modları** (RotateCommand/
  ScaleCommand zaten mevcut), eksen-düzlem yakalama (XY/XZ/YZ pad'leri).
- Multi-select + grup taşıma; gizmo'nun seçim merkezine snap'i.
- debug_draw'ın editor'de genişlemesi: kamera frustum önizlemesi,
  ışık gizmoları (hello_engine'dekilerin porty).
- hello_editor ↔ hello_engine sahne formatı köprüsü (cdscene
  round-trip zaten var; editor'den hello_engine sahnesine yükleme).

### C3. hello_engine / demolar (düşük öncelik, dolgu işi)

- Kalan text-only probe'lar: scene serializer, input axis, math
  helpers, asset registry tag, NRC MSE eğrisi (line-chart'a uygun).
- main() küçültme devamı: HelloFrameLoop (~690 satırlık on_frame
  bloğu) + HelloPaletteScene aggregates (Run 15'ten beri kuyrukta).

### C4. Lint / kalite (otonom, mekanik)

- 12 ertelenen kural: google-build-using-namespace,
  google-default-arguments, google-explicit-constructor,
  google-runtime-int, fuchsia-default-arguments-{calls,declarations},
  fuchsia-overloaded-operator, fuchsia-statically-constructed-objects,
  fuchsia-trailing-return, llvm-header-guard, llvm-include-order,
  llvm-namespace-comment — site sayıları probe edilip kolaylar
  promote edilmeli.
- Warning-level sınıflar (dirilen ailelerden): readability-redundant-
  casting, readability-avoid-nested-conditional-operator (yaygın),
  misc-use-internal-linkage, hicpp-function-size (draw_r_showcase_panel
  + on_frame — C3'teki extraction'larla doğal çözülür), kalan
  use-std-numbers literal'leri, engine-geneli use-std-min-max.
- CI tutarlılığı: ci.yml'deki clang-tidy job'unun dirilen ailelerle
  süresi/sonucu doğrulanmalı (ilk koşu uzayabilir; per-test TIMEOUT
  artık var ama tidy job'unun kendi timeout'u kontrol edilmeli).

### C5. Dokümantasyon / süreç

- **debug_line + debug_draw ADR'i yok**: park-margin (frame+3)
  konvansiyonu + CPU/GPU ayrım gerekçesi kısa bir ADR hak ediyor.
- **STATUS_AND_PLAN_W8.md bayat**: 1042-1064 fazları yansımıyor.
- Golden re-bake akışı dokümante değil (K4 sonrası gerekecek).
- clang-tidy "Checks scalar'ına # koyma" kuralı CLAUDE.md'ye eklenebilir.

---

## D. Önerilen yürütme sırası

**Sprint 1 — tam otonom — ✅ TAMAMLANDI (phases 1066-1077, 2026-06-11):**
1. ✅ C4 lint kuyruğu (1067-1070): 12 ertelenen kural kapandı — 5
   promote (209→214 WAE; default-arguments NVI refactor dahil),
   7 rationale'li disable.
2. ✅ C5 docs (1066, 1071): debug_draw ADR + STATUS refresh +
   CLAUDE.md tidy kuralı.
3. ✅ C2 gizmo v2 (1072-1074): lib kit (12 test) + ray-plane translate
   + XY/XZ/YZ pads + rotate/scale modları + 1/2/3 hotkeys.
4. ✅ C1 X1-FU-E README + X1-FU-G audit (1075): IDevice 4-kural
   threading contract + research/reports/X1FUG_*.md.
5. ✅ C3 HelloFrameLoop b1+b2 (1076-1077): 18 demo bloğu →
   HelloViewportDemos.hpp; main.cpp 13042→11440, on_frame ~2590→~990.

**Sprint 2 — safety-review'lu otonom — ✅ TAMAMLANDI (phases 1078-1081):**
6. ✅ X1-FU-C hazard-pointer WSD reclamation (1080) + X1-FU-D
   priority-aware pop/steal, 4 seviye deque/worker (1079).
7. ✅ X1-FU-A atomic wake-epoch (1078) — cv + 2ms poll yerine C++20
   atomic wait/notify. Not: TSan Windows'ta YOK (ADR'nin kaydı doğru);
   doğrulama stress + Release+ASAN ile yapıldı, TSan X1-FU-B CI
   lane'ine kaldı. Debug+ASAN ucrtbased /MDd interop'u nedeniyle
   process-init'te ölüyor (proje hatası değil — LLVM 21 bilinen kısıt).
8. ✅ X1-FU-F surface review (1081): research/reports/X1FUF_*.md —
   verdict: raw secondary-buffer DEĞİL, pass-scoped parallel recorder
   (lane modeli). ADR + implementasyon K1 sonrası.
   safety-integration adversarial review'u 1078-1080 üzerinde koşuyor;
   bulgular ayrı fix phase'leriyle kapatılacak.

**Sprint 3 — onay sonrası büyük işler:**
9. K1+K3 → X4 D3D12 parity (3-4 hafta, en büyük kalem) → X1 Phase 2
   WSL entegrasyonu.
10. K2 → L1 Metal P1 (X4 ile paralelleştirilebilir; farklı dosya
    kümeleri).

**Sıralama gerekçesi**: Sprint 1 tamamen geri-alınabilir/düşük-risk;
Sprint 2 concurrency dokunuşları bu haftaki RCA kasları sıcakken;
Sprint 3 çok-haftalık taahhütler olduğundan onay sonrası.

---

## E. Riskler / işletme notları

- **PC kararlılığı (K6)**: 0x9F çözülmeden gece maratonları kesintiye
  açık; her yeşil checkpoint ANINDA commit kuralı geçerli.
- **Zombi background görevler**: restart sonrası eski komut zincirleri
  hortlayıp yanlış mesajla commit atabiliyor — commit daima foreground
  (memory'de kayıtlı).
- **clangd stale-index**: IDE'de görünen "error"ların çoğu bayat;
  gerçek hakem ninja build + tek-TU clang-tidy.
- **Ajan oturum limitleri**: yoğun paralel ajan kullanımında limit
  dolabiliyor (bu sabah 2 kez); kritik patch'ler orkestratörde solo
  bitirilecek şekilde planlanıyor.
