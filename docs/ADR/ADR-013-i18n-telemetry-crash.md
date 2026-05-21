# ADR-013 — Localization, Telemetry & Crash Reporting

- **Status**: Accepted (Phase 1 Design)
- **Date**: 2026-05-17
- **Related**: ADR-005 (Foundation), ADR-006 (Asset), ADR-009 (UI), ADR-016 (Vendor Matrix), ADR-017 (DtForHil Salvage)

## Bağlam

Üç çapraz-kesen alt-sistem (`cd::i18n`, `cd::profile`, `cd::diag::crash`). Tüm üçü library-oriented: ayrı header-only/static, sıfır global state, opt-in build flag, dış bağımlılık opsiyonel/PIMPL arkasında.

User decisions:
- T24.Q1 = state-of-art (i18n format)
- T24.Q2 = Font A (full Harfbuzz infra), Latin öncelik script
- T24.Q3 = E accessibility out-of-scope phase 1 (infra-ready)
- T24.Q4-Q6 = state-of-art (telemetry, crash, analytics)

## Karar

### A. i18n (T24.Q1) — Mozilla Fluent FTL

**Primary**: Mozilla **Fluent FTL** syntax + CLDR plural backend; runtime hot-reload; ICU optional heavy mode (collation/regex editör/asset-pipeline tarafı).

```
locale/en-US/main.ftl
  hello = Hello, { $name }!
  cart-summary =
    { $count ->
      [one]   You have one item in your cart.
     *[other] You have { $count } items in your cart.
    }
```

In-engine `cd::i18n::Bundle` parser (~2-3K LOC, minimalist FTL subset: selector, placeable, term, attribute). CLDR plural classifier embedded (~40 dil). ICU optional `CD_I18N_ICU=ON`.

**Reddedilen**: gettext (asymmetric plural/gender yok, static); ICU MessageFormat tek başına (30MB veri, verbose); custom JSON (translator tool yok, yeniden icat).

#### A.1 Workflow

- **Source**: `assets/locale/<locale>/<bundle>.ftl` (en-US source of truth)
- **Pipeline**: `cd::asset::LocaleBaker` FTL → binary bundle `.cdloc` (magic header + interned string table). Hot-reload watcher (S6) FTL kaynak değişimi dinler.
- **XLIFF köprü**: `tools/fluent-xliff-bridge` Python (Pontoon/Crowdin source).

#### A.2 RTL / Script

- HarfBuzz infra Latin+CJK+Arabic+Devanagari hepsi destek; **Latin öncelik phase 1**.
- BiDi: UAX#9 — fribidi optional dep veya minik in-engine UBA. ICU heavy mode `icu::BidiLine`.
- Mirror-aware UI primitives (`UiAlignment::Start/End` yerine Left/Right) — ADR-009 constraint.

#### A.3 Accessibility (T24.Q3 = E)

Out-of-scope phase 1; API surfaces **infrastructure-ready**: `cd::ui::Widget::AccessibilityNode` opsiyonel virtual; phase 2 AccessKit entegrasyon hedef.

#### A.4 Hot-Reload

`cd::i18n::Catalog` çift tampon: aktif + pending. Asset reload → atomik pointer swap (RCU pattern). UI `LocalizedText` widget her frame catalog version check (cheap atomic load), değişirse re-layout queue'sune.

### B. Telemetry / Profiling (T24.Q4) — Tracy + Custom HUD

**Primary**: **Tracy Profiler** (BSD-3, Bartosz Taudul, AAA-grade — CDPR, Embark, Larian production use). `cd::profile::vendor::tracy`. K3 kategori, Phase 4 kısmen replace.

`cd::profile::Zone` thin wrapper:
- Tracy varsa → `TracyCZoneN`
- Tracy yoksa → no-op fallback (CI build Tracy submodule olmadan derlenir)

**Custom HUD overlay** (her zaman aktif): `cd::profile::HudOverlay` — frame-time histogram, 1%/0.1% low, draw call count, triangle count, GPU memory, allocator pages. F2 toggle default.

**Reddedilen**: Optick (unmaintained 2022 sonrası); perfetto secondary; UE5 Insights closed.

#### B.1 GPU Profiler

- Tracy GPU zones (Vulkan timestamp queries, D3D12 query heap, Metal counter sample).
- Vendor marker: PIX (Windows), RGP (AMD), Nsight (NVIDIA NVTX). `CD_GPU_MARKERS=ON`.
- RenderDoc API: `RENDERDOC_API_1_6_0::TriggerCapture()` editor menüsünden (debug).

#### B.2 Memory Tracking (S5 cross-cut)

`cd::mem::Allocator` her alloc/free → Tracy event + `cd::profile::MemoryTag`. UE5 LLM pattern: per-allocator tag (`MemoryTag::Renderer`, `::Audio`, vb.).

#### B.3 Production Telemetry

- Anonimleştirilmiş FPS distribution, crash rate, GPU model — **opt-in**, varsayılan kapalı.
- Engine kendi backend host etmez; oyun geliştirici Sentry/Backtrace/custom HTTP configure eder.
- **OpenTelemetry C++ SDK** opt-in (`CD_TELEMETRY_OTEL=ON`) — OTLP/gRPC veya OTLP/HTTP, Grafana/Tempo/Jaeger uyumlu standart.

### C. Crash Reporting (T24.Q5) — Crashpad

**Primary**: Google **Crashpad** (Apache 2.0, out-of-process handler). `cd::diag::vendor::crashpad`. **K4 kategori, Phase 4 replace hedefi** (6-12 ay custom SEH/sigaction/Mach handler).

**Reddedilen**: Breakpad (legacy, Crashpad halefi); Backtrace.io SDK (commercial vendor lock); custom SEH/sigaction direct (3-6 ay edge case dolu, Phase 4'e ertele).

#### C.1 Symbol Pipeline

- CI: her release artifact için **iki sembol**:
  - Windows: `*.pdb` (Microsoft SymSrv layout)
  - Cross-platform: `dump_syms` → `.sym` (Breakpad format)
- Upload: `symstore.exe` (Windows) + Sentry `sentry-cli upload-dif`.
- Internal SymSrv-uyumlu HTTP/SMB share: `\\symsrv\chromodynamic\<binary>.pdb\<guid><age>\`.

#### C.2 Stack Walking

- Windows: SEH (`AddVectoredExceptionHandler`) — Crashpad.
- Linux: `sigaction` SIGSEGV/SIGBUS/SIGFPE/SIGILL/SIGABRT — Crashpad alternate stack.
- macOS: Mach exception ports — Crashpad native.
- Soft assert: `cd::diag::capture_non_fatal()` minidump üretir, process devam eder (UE5 pattern).

#### C.3 Hot-Data Sentinel (aşma noktası)

`cd::diag::Sentinel` lock-free ring buffer (DtForHil ADR-017 P0 `RingBuffer<T>` pattern):
- Last-N frames frame timing
- Last-K log lines
- Current scene name, active asset bundle list

Crash handler bunu minidump'a "user stream" olarak ekler (Crashpad `UserStreamDataSource`). Sentry/Backtrace alone'dan iyi: rebuilds tested investigative narrative.

### D. Analytics (T24.Q6) — Plugin-Only, Built-in YOK

- Engine **hiçbir telemetri toplamaz** — open source güveni.
- Oyun geliştirici opt-in dialog gösterir (GDPR Recital 32, CCPA §1798.135).
- `cd::analytics::Consent` API: per-category (`Crash`, `Performance`, `Gameplay`).
- Plugin: `cd::analytics::ISink` — built-in sink yok. Studio Sentry/GameAnalytics/Unity Analytics/custom HTTP plugin yazar.

```cpp
namespace cd::analytics {
  struct Event { std::string_view name; std::span<const KV> props; };
  class ISink {
    public:
      virtual ~ISink() = default;
      virtual void submit(const Event&, Consent) = 0;
  };
  CHROMA_ANALYTICS_API void register_sink(std::unique_ptr<ISink>);
}
```

OpenTelemetry production telemetry için opt-in.

### E. Aşma Noktaları

1. **Fluent + hot-reload + ICU optional** — UE/Unity ya ICU heavy ya gettext static. İkisinden iyi yanı.
2. **Tracy + custom HUD birlikte** — UE5 Insights closed; Unity Profiler kapalı. Biz BSD baseline + opsiyonel kendi.
3. **Crashpad + Sentinel hot-data ring** — Crashpad alone'dan iyi: son N frame + log tail + scene minidump'a gömülü.
4. **OTel opt-in** — Hiçbir AAA engine standardize OTel; biz veriyoruz (SRE-friendly).
5. **Plugin analytics, zero built-in tracking** — Open source güven sinyali.
6. **Memory tracking S5 ile dokunmuş** — UE5 LLM benzeri, allocator tag'leri compile-time enum (overhead ~0).

## Reddedilen

- **gettext PO/MO** (asymmetric plural yok).
- **ICU MessageFormat tek başına** (30MB + verbose).
- **Custom JSON i18n** (yeniden icat).
- **Optick** (unmaintained).
- **Breakpad** (Crashpad halefi).
- **Backtrace.io SDK** (vendor lock).
- **Built-in analytics (Unity/UE pattern)** (open source güven + GDPR risk).
- **Custom crash handler v1** (3-6 ay edge case → Phase 4 hedef).

## Sonuçlar

**Pozitif**:
- Library-oriented her üç sistem.
- Tracy submodule olmadan da derlenir (no-op fallback).
- Crashpad + Sentinel Sentry/Backtrace alone'dan iyi.
- Open source güveni: zero built-in tracking.
- OTel SRE-friendly.

**Negatif**:
- Yeni dep: Tracy submodule (~2 MB), Crashpad vcpkg (~5 MB), opsiyonel OTel/ICU/fribidi.
- Build süresi tahmini +%3-5 debug, +%1 release.
- Binary boyutu +~3 MB release.

**Replace-Ready (D1)**:
- Crashpad → kendi crash handler (Phase 4, 6-12 ay).
- Tracy → kendi profiler (Phase 4 kısmen; custom HUD zaten kendi).
- Geri alma maliyeti 1 sprint (PIMPL + no-op fallback sayesinde).

## Açık Sorular

| ID | Soru | Çözüm |
|---|---|---|
| Q1 | Fluent parser portu mu fluent-rs C FFI mı? | In-engine C++ parser ~2K LOC |
| Q2 | ICU veri gömme: full vs slim (`icudt_l.dat`)? | Editor full, runtime slim |
| Q3 | Crashpad attachment boyut limiti? | Sentinel ring 1-5 MB default |
| Q4 | OTel exporter: OTLP/gRPC vs HTTP? | HTTP default (gRPC ~10 MB extra) |
| Q5 | PII sanitization stratejisi? | Sentry data scrubber pattern |
| Q6 | Mobile/console crash: iOS KSCrash, Switch/PS5 NDA SDK? | Phase 2 |
| Q7 | Tracy production: stripped vs always-on (Embark)? | Stripped default, always-on opt-in |
| Q8 | Telemetry+crash transport birleşme? | Ayrı endpoint default |

## Cross-Cutting

- **ADR-005 (Foundation)**: `cd::mem::Allocator` her alloc Tracy event + `MemoryTag`. Tri-clock zaman damgalama. Sentinel ring buffer foundation primitive (DtForHil ADR-017 P0).
- **ADR-006 (Asset)**: `LocaleBaker` FTL → `.cdloc` binary; asset bundle hot-reload → i18n catalog tetikle. Symbol pipeline `.pdb`/`.sym` build artifact (asset-benzeri). Crashpad attachment için asset manifest snapshot.
- **ADR-009 (UI)**: `LocalizedText` widget catalog version atomic check; `BiDiText` layout primitive (UAX#9); `UiAlignment::Start/End` enum (RTL aware). Accessibility hook infra-ready phase 1 no-op. Tracy HUD ve crash dialog UI S9 katmanında.

## Kanıt

- Mozilla Fluent: projectfluent.org
- ICU: icu.unicode.org
- Tracy: github.com/wolfpld/tracy (BSD-3)
- Crashpad: chromium.googlesource.com/crashpad/crashpad (Apache 2.0)
- OpenTelemetry C++ SDK: github.com/open-telemetry/opentelemetry-cpp
- Mytkowicz et al. (2010) — Evaluating the Accuracy of Java Profilers PLDI — **STUB**
- Davis et al. (1999) — CLDR Unicode TR#35 — **STUB**
- Pietrek (2002) — DBGHELP 5.1 Minidump MSDN — **STUB**
- Graham et al. (1982) — gprof Call Graph Profiler SIGPLAN — **STUB**
