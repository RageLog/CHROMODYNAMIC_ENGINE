# ADR-20260522 — On-disk caches: SPIR-V compile + VkPipelineCache

## Bağlam

İki ayrı "shader cold-start ısınma" maliyeti vardı:

1. **GLSL → SPIR-V translation.** glslang her invocation'da ~50 ms / shader
   stage harcıyor. hello_gltf 1 VS + 1 FS = ~100 ms cold-start overhead.
   Editor session'larda shader değişmediği halde bu maliyet her launch'ta
   tekrar ödeniyor.

2. **VkPipeline state-hash → driver bytecode.** Vulkan
   `vkCreate{Graphics,Compute}Pipelines` driver-side shader optimization
   yapar. Cold cache durumda 100-1000 ms / pipeline (AMD/NVIDIA shader
   compiler'a göre değişir). Sıcak cache'te driver state-hash'ten
   önbelleğe bakar; ~1 ms.

Her ikisi de aynı problem ailesi (sıcak-cache vs cold-cache) ama farklı
katmanlarda; ayrı ayrı çözüldü.

## Karar

### (a) cd::shader::CachedCompiler (W9.2)

ICompiler decorator. Hash key: FNV-1a 64-bit `source bytes + stage +
lang + target + entry_point + debug_flag`. Cache yolu:
`<cache_dir>/<key_hex>.spv` + `.meta` sidecar.

```cpp
auto cache = cd::shader::make_cached_glslang_compiler(".shader_cache");
auto r = cache->compile(desc);  // first call writes, second reads
```

Birim test: 5 case (miss writes, hit short-circuits, different stage
misses independently, clear forces recompile, second instance picks up
disk cache). Atomic publish via .tmp + rename.

### (b) VkPipelineCache persistence (W9.3)

VulkanDevice ctor `init_pipeline_cache_()` çağırır — `.shader_cache/
pipeline_cache.bin` varsa seed olarak kullanır. dtor `save_pipeline_cache_()`
ile `vkGetPipelineCacheData` çağırıp diske yazar (yine .tmp + rename
atomic publish).

`vkCreateGraphicsPipelines` ve `vkCreateComputePipelines` artık
`pipeline_cache_` argümanı ile çağrılıyor (`VK_NULL_HANDLE` yerine).

Yerel doğrulama: hello_cube --headless 5 iki kez koşturuldu;
.shader_cache/pipeline_cache.bin 14558 byte yazıldı, ikinci koşum
dosyayı seed olarak okudu, hata yok.

## Reddedilen alternatifler

* **Tek `cd::cache` kütüphanesi:** İki kaynak iki API. SPIR-V hash key
  user input (source + stage), pipeline cache key Vulkan driver-internal.
  Tek API'de zorlanarak birleştirmek ergonomik fayda vermez.
* **SHA-256 yerine FNV-1a:** Cryptographic strength gerekmiyor (collision
  → wrong SPIR-V loaded), birthday-bound 64-bit'te ~1e-12 olasılık. FNV
  ~5× hızlı ve sıfır dependency.
* **In-memory hash → no disk:** SPIR-V cache process-ömrü sürüyor; cold
  start için diskte gerekli. Pipeline cache aynı şekilde.

## Sonuçlar

* Geliştirici döngüsü: hello_gltf launch süresi cold-start sonrası
  beklenen 2-5×. Production'da bu fark Release build'de daha az
  görünür çünkü pipeline cache Vulkan SDK convention'ı; ama bizim
  shader compile cache de Release'de aynı kazancı verir.
* Engine version değişiminde (shader semantics değişebilir) cache
  invalidation gerekir. v1: yok — kullanıcı `.shader_cache/` siler.
  v2: engine version SPIR-V hash key'e konacak.
* Disk yazımı best-effort: read-only filesystem (shipped game data)
  durumunda sessizce no-op olur; compiler hâlâ çalışır.

## Açık sorular

* `.shader_cache/` per-app data dir'inde mi olmalı (XDG_CACHE_HOME /
  %LOCALAPPDATA%)? Şu an CWD-relative. Shipped product için package
  installer script'i pre-warm cache koymalı; engine yolu konfigürable
  olmalı (v2 — `CachedCompiler` ctor'a `cache_dir` zaten alıyor;
  VulkanDevice'a aynı injection eklenecek).
