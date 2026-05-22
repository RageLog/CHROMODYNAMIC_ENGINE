# ADR-20260522 — Wave 4: developer-experience tooling + two new libraries

## Bağlam

Wave 3 (11:30–12:00) tooling katmanını tamamladı: Doxygen, smoke harness,
coverage gate. Wave 4 (12:00–12:45) iki paralel hedef:

1. **Polish:** Doxygen warnings'ı sıfırla, `WARN_AS_ERROR=FAIL_ON_WARNINGS`
   gate'i kur, README'yi sıfırdan yaz, LIBRARIES.md kataloğunu üret.
2. **İki yeni kütüphane:** `cd::bench` (microbench utility) ve
   `cd::asset_wav` (RIFF/WAVE PCM loader).

## Karar

### 1. Doxygen disipline

- `WARN_IF_DOC_ERROR = NO` — cross-file `\ref` failures (narrative docs
  Doxygen INPUT'unda değil) gürültü; GitHub markdown render zaten linkleri
  takip ediyor.
- `WARN_AS_ERROR = FAIL_ON_WARNINGS` — obsolete tag, unknown command vs.
  artık fail. ADR / PLAN exclude'lu olduğu için clean.
- `docs/LIBRARIES.md` Doxygen INPUT'a eklendi — Doxygen-friendly yazıldı.

### 2. README.md

Tek satırlık placeholder yerine:
- Engine kimliği + status (45 lib, 18 sample, 47 ctest)
- Highlights (modern C++23, Vulkan dynamic rendering, library-oriented,
  cooked asset pipeline, ECS, profile, frustum cull, ImGui, tooling)
- Quick start (preset → build → test → smoke → docs → coverage → sanitizers)
- Compiler matrix (9 toolchain × 3 OS)
- Library tier overview (5 tiers)
- Repo layout
- Sample table (18 samples + responsibilities)
- Contributing rules (özet)

### 3. `docs/LIBRARIES.md`

Full per-library table organizasyonu (5 tier, 45 lib). Her satır:
target, alias, namespace, include root, responsibility. Dependency
kuralları sonda.

### 4. `cd::bench` (header-only)

`engine/foundation/bench/include/cd/bench/Benchmark.hpp`:

```cpp
auto r = cd::bench::run("name", body, Config{ .min_samples = 64 });
r.print(std::cout);   // [bench] name mean=N ns/op  med=...  min=...  p99=...
```

Özellikler:
- Warmup (3 iteration discarded)
- Inner-call calibration: sample 1 µs'den kısaysa inner_calls iki kat
  artırılır (timer resolution noise'tan kaçınma)
- Stats: mean, median, min, p99, total_seconds, samples, inner_calls
- `do_not_optimize<T>(value)` barrier — Clang/GCC inline asm + MSVC
  fallback (volatile atomic xor)
- CSV output: `name,label,samples,inner_calls,mean,median,min,p99,wall`

**Test:** 6 unit test, geçer (Debug).
**Sample:** `hello_bench` 4 microbench:
- integer_loop_64: 110 ns/op (speed-of-light floor)
- Result<int> success: 2.4 ns/op
- vector<int>::reserve(64)+push: 1494 ns/op (heap dominated)
- PoolAllocator alloc+free: 4.4 ns/op (O(1) free-list)

### 5. `cd::asset_wav`

`engine/asset_wav/include/cd/asset_wav/Wav.hpp`:

```cpp
auto r = cd::asset_wav::load("kick.wav");
if (r) { mixer.upload(*r); }
```

Format support:
- RIFF/WAVE container
- fmt sub-chunk parsing (channels, rate, bits, format code)
- data sub-chunk → raw byte buffer
- Format codes: PCM (1), IEEE float (3)
- Multi-chunk navigation: bilinmeyen chunk'lar (JUNK, LIST, cue, PEAK)
  sessizce atlanır
- Even-byte padding RIFF spec'i

Out of scope (v1):
- ADPCM, μ-law, A-law, MP3-in-WAV
- WAVE_FORMAT_EXTENSIBLE (GUID dispatch, nadiren oyun audio)
- Decode to f32 (audio engine kendi conversion'ını yapar)

Tests (7):
- mono s16 @ 44100 decode
- stereo f32 @ 48000 decode
- bad magic → kMagicMismatch
- truncated buffer → kCorrupt
- unsupported format code (0xFFFE) → kUnsupportedFormat
- unknown chunk (JUNK) skip
- missing file → kFileNotFound

## Reddedilen alternatifler

### cd::bench

- **google/benchmark integration:** ~5K SLOC + macros + global registry.
  Bizim kullanım: 1-2 microbench per session, ad-hoc. Engine için
  third-party transitive dep, smoke build'i şişirir. Hand-roll v1
  yeterli; v2'de gerekirse swap.
- **`std::chrono::high_resolution_clock`:** Bazı platformlarda
  `steady_clock` alias'ı; bazılarında `system_clock`. `steady_clock`
  monotonik garantisi var, biz onu kullanıyoruz.
- **Tracy integration:** Excellent runtime profiler ama microbench'in
  paralel use-case'i değil. Tracy session uzun çalışır; bench tek-shot.

### cd::asset_wav

- **dr_wav (single-header public-domain):** Mükemmel kütüphane, popüler.
  Ama bizim use-case sadece PCM v1 — 200 satır hand-roll daha küçük
  binary, tam control, ve license belirsizliği yok (dr_wav MIT-0 ama
  copy-vendoring policy'mizde MIT-style olmayan license'lar listede yok).
- **stb_vorbis tarzı geniş kapsam:** Compressed WAV, MP3-in-WAV, ADPCM
  → bunlar oyun audio için ya kaynaktan kontrol edilebilir (.ogg/.opus)
  ya da decoder offline yapılır. Runtime asset loader minimal kalır.

### README.md

- **Generated from sub-READMEs:** Her `engine/<lib>/` kendi README'sini
  topla → root README'ye concat. Premature; lib sayısı 45 ve değişiyor.
- **Sadece LIBRARIES.md, README boş:** README hâlâ GitHub repo'nun ilk
  görüntüsü; quick start orada olmalı.

## Sonuçlar

| Metric | Wave 3 sonu | Wave 4 sonu |
|---|---|---|
| Engine libraries | 45 | **47** (+cd::bench, +cd::asset_wav) |
| Samples | 18 | **19** (+hello_bench) |
| ctest binaries | 47 | **49** |
| Headless smoke-clean | 18/18 | **19/19** |
| Doxygen warnings | 5 (benign) | **0** + WARN_AS_ERROR gate |
| README.md | placeholder | full quick-start + catalogue link |
| LIBRARIES.md | — | 45-row tier table |
| ADRs (this date) | 17 | **19** (+wave3, +wave4, +smoke, +ktx2 — wave3 ek 2) |

Reviewer flow tek-komut:

```bash
cmake --preset ninja-base
cmake --build --preset ninja-debug
ctest --preset ninja-debug                             # 49/49
cmake --build --preset ninja-debug --target smoke      # 19/19
cmake --build --preset ninja-debug --target cd_docs    # 0 warning, fail-on-warning gated
cmake --build --preset ninja-debug --target coverage-report  # if -DCD_ENABLE_COVERAGE=ON
```

## Açık sorular

- **Audio engine integration:** `cd::audio` v1 mixer'ı kabul edebilir
  mi `Wav` payload'ını? Mevcut `cd::audio` interface'i interaktif
  voice graph odaklı; WAV → buffer source binding henüz yok. v2.
- **WAV smoke sample:** `hello_wav` sample'ı (sine wave synth → write
  WAV → load → assert roundtrip). v2 için iyi candidate.
- **Audio cooker (`cd_cook_audio`):** Opus/Ogg → engine-internal binary
  (.cdaudio). Şimdilik runtime decode yeterli; cook tool v3.
- **GCC concurrency hang:** wave 4'te tetiklenmedi, kapsama dışı. ADR
  olarak ayrı tarama gerekli.
- **Microbench corpus:** hello_bench 4 satırlı; ECS query, render
  submit, asset load, file I/O için ayrı bench sample'ları ek bir tour
  olabilir.
