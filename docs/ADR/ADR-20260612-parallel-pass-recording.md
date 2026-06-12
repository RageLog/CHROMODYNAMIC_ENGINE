# ADR-20260612 — Pass-scoped parallel command recording (X1-FU-F)

**Durum**: Accepted (kullanıcı 2026-06-12 "planı tamamen işleme koy"
direktifiyle K1 kapısını açtı; tasarım research/reports/
X1FUF_secondary_command_buffer_surface_review.md'nin kararıdır).

## Bağlam

X1 prep/submit pattern'ı CPU doldurma maliyetini paralelleştirdi ama
vkCmd* kayıt maliyeti serial kaldı (ADR-20260528'de orijinal X1E bu
yüzden X1-FU-F'e ertelendi). Surface review dört backend'i taradı:
Vulkan'ın "secondary command buffer" nesnesini ham dışa vurmak yanlış
soyutlama — D3D12'nin karşılığı paralel DIRECT list'lerdir (bundle'lar
tuzak), Metal'inki MTLParallelRenderCommandEncoder, GL'de hiçbiri yok.

## Karar

**Pass-scoped parallel recorder** soyutlaması:

1. `IDrawRecorder` — ICommandBuffer'dan AYRILAN draw-subset taban
   arayüzü (pipeline/descriptor/vertex/index/push/viewport/scissor/
   draw/draw_indexed/draw_mesh_tasks/debug-group).
   `ICommandBuffer : IDrawRecorder` olur; yaşam döngüsü (begin/end),
   pass scope, compute dispatch, copy/barrier/RT yüzeyi ICommandBuffer
   üzerinde kalır.
2. `IParallelPassRecorder` — `ICommandBuffer::
   begin_parallel_render_pass(info, lane_count)` ile açılır;
   `lane(i) -> IDrawRecorder&` her biri TEK thread'e bağlı kayıt
   yüzeyi; `finish()` lane'leri **lane SIRASINA göre** primary'ye
   katar ve pass'i kapatır. Lane sırası = submission sırası ⇒
   aynı-girdili kareler worker dağılımından bağımsız birebir aynı
   replay edilir (golden-image güvencesi).
3. Default implementasyon `nullptr` döner (= backend henüz
   desteklemiyor); çağıran serial yola düşer. Null backend gerçek
   lane'lerle referans implementasyondur (deterministik merge'ün
   birim testi); Vulkan lane'leri secondary buffer + per-thread pool +
   vkCmdExecuteCommands ile (adım 2), D3D12 segmentli direct
   list'lerle (X4 ile), Metal sub-encoder'larla (K2 ile) gelir.

Misuse sınıfı TİP SİSTEMİYLE kapanır: `IDrawRecorder&` üzerinden
`begin_render_pass`/`end`/`barrier` çağrısı DERLENMEZ (testte
`requires` ifadesiyle pinlenmiştir) — surface review'un "split'i yap"
tavsiyesinin gerekçesi buydu.

## Reddedilen alternatifler

- **Ham secondary-buffer API'si**: D3D12/Metal'e haritalanmıyor;
  inheritance-info gibi Vulkan'a özgü kavramlar sızar.
- **Lane'in ICommandBuffer& dönmesi**: misuse yalnız dokümantasyonla
  engellenirdi; Null assert'i ancak runtime'da yakalar.
- **D3D12 bundle eşlemesi**: RT değişimi yasak vb. kısıtlar gerçek
  pass-split'i imkânsızlaştırır.

## Sonuçlar

- Threading kontratı genişler: lane oluşturma = kaynak yaratma
  (IDevice kural 1, dış senkron); per-frame lane reset fif disiplinine
  bağlanır (debug_draw ADR'indeki park-margin ailesi).
- Lane sayısı doğal olarak WorkStealingThreadPool::thread_count() ile
  eşleşir; X1 prep/submit scratch bölüntüsü değişmeden taşınır.
- Adım planı: (1) split+Null+testler ✅ bu ADR'le; (2) Vulkan lane'leri
  ✅ phase1116 + GPU testi (SEAL phase1119-1120); (3) hello_engine HDR
  pass'i panel toggle'ı arkasında ✅ phase1121 — chrome_probe fixture
  serial-vs-lanes yakalaması BAYT-AYNI (cmp clean), lane'ler v1'de ana
  thread'de sıralı kaydedilir (paylaşılan-durum audit'i sonrası worker
  dispatch); (4) D3D12/Metal kendi gemileriyle.
- safety-integration kapısı adım 2'de zorunlu (pool ömrü + reset
  fencing) — Sprint-2 concurrency barı.
- **Adım-2 kapı durumu (phase1119)**: safety audit VETO → kontrat
  uygulandı → SEAL adayı. (A3) retired lane pool'ları `begin()`'de
  serbest bırakılır — begin() zaten "primary pending değil" kontratını
  taşır (Renderer frame-fence bekler) ve retired secondary'nin pending
  ömrü primary'ninkinin alt kümesidir; fix bu yüzden ek varsayım
  gerektirmez. (C) lane debug-label arena'ları `finish()`'te primary'ye
  taşınır (pool'larla aynı fence disiplini). (A4) swapchain teardown
  iki döngüsü `view_formats_`'ı da siler. (B2) `lane(i)` aralık dışı
  debug assert (Vulkan + Null). (A2) recorder ömür/threading kontratı
  `ICommandBuffer.hpp`'de dokümante: finish() yıkımdan önce zorunlu;
  lane kayıtları finish()'ten happens-before; abandon pass scope'unu
  açık bırakır. MSAA nöbeti: `iri.rasterizationSamples` 1-sample
  hardcode — adım 3'te MSAA'lı pass'e lane açmadan önce genelle.
