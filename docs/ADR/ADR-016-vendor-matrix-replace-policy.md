# ADR-016 — Vendor Matrix & Replace-Ready Policy

- **Status**: Accepted (Phase 1 Design)
- **Date**: 2026-05-17
- **Scope**: All vendor dependencies + replacement roadmap
- **Authority**: This ADR is normative for **all** other ADRs that introduce a vendor.

## Bağlam

Kullanıcı direktifleri (E2 = d, K4, "low level kendimiz yazma"):
1. Kütüphaneleri olabildiğince **kendimiz yazacağız** (T5.Q2).
2. Vendor sayılmayan temel şeyler alınabilir.
3. **Sonradan yerine kendi yazımımızı geçirecek şekilde tasarla** (Replace-Ready).
4. Low-level (allocator, device I/O, signal handling, profiler) öncelikli replace adayı.

Bu disiplin tüm ADR'larda zorunlu hâle gelir; bu ADR onu **normatif** olarak tek noktada toplar.

## Karar

### A. Vendor Kategorileri

| Kategori | Politika | Replace policy |
|---|---|---|
| **K1. Kaçınılmaz (Khronos/MS official)** | Header-only veya official binary | Yok (replace olmaz) |
| **K2. Custom yazım stratejik avantaj sağlamaz** | Vendor + Replace-Ready interface | Phase 4+ değerlendir |
| **K3. Custom yazım mümkün ama maliyetli** | Vendor (geçici) + Replace-Ready interface | Phase 3 hedef |
| **K4. Stratejik kontrol gerekli (low-level)** | Vendor (Phase 1 hızlı başlangıç) + Replace-Ready | **Phase 2-3 zorunlu** kendi yazım |
| **K5. Yozlaştırıcı (HIL-specific, vendor-locked, proprietary)** | Reddedildi | N/A |

### B. Kesin Vendor Listesi (kabul edilmiş)

| Vendor | Lisans | Kategori | Replace hedefi | Adapter Namespace |
|---|---|---|---|---|
| **Vulkan loader+headers** | Apache 2.0 (Khronos) | K1 | Asla | `cd::rhi::vendor::vulkan` |
| **DXC (HLSL→DXIL/SPIR-V)** | LLVM | K1 | Asla | `cd::shader::vendor::dxc` |
| **SPIRV-Cross** | Apache 2.0 (Khronos) | K1 | Asla | `cd::shader::vendor::spirv_cross` |
| **SPIRV-Reflect** | Apache 2.0 (Khronos) | K1 | Asla | `cd::shader::vendor::spirv_reflect` |
| **Slang** (shader compiler) | Apache 2.0 (Khronos resmi 2024) | K1 | Asla | `cd::shader::vendor::slang` |
| **FreeType** (font face) | FTL/GPL2 dual | K2 | Asla (10+ yıl ROI yok) | `cd::ui::vendor::freetype` |
| **Harfbuzz** (Unicode shaping) | MIT | K2 | Asla (10+ yıl ROI yok) | `cd::ui::vendor::harfbuzz` |
| **libktx + basisu** | Apache 2.0 (Khronos referans) | K1 | Asla | `cd::asset::vendor::ktx2` |
| **Jolt Physics** | MIT | K3 | Phase 5+ değerlendir | `cd::physics::vendor::jolt` |
| **Box2D v3** | MIT | K3 | Phase 5+ değerlendir | `cd::physics2d::vendor::box2d` |
| **mimalloc** | MIT (MS Research) | **K4** | **Phase 3 zorunlu** (6-12 ay) | `cd::mem::vendor::mimalloc` |
| **Tracy Profiler** | BSD-3 | K3 | Phase 4 kısmen | `cd::profile::vendor::tracy` |
| **Crashpad** | Apache 2.0 (Google) | **K4** | **Phase 4 hedef** (6-12 ay) | `cd::diag::vendor::crashpad` |
| **ACL (anim compression)** | MIT (Frechette) | K2 | Asla (ROI yok) | `cd::anim::vendor::acl` |
| **Google Highway** (SIMD) | Apache 2.0 | K2 | Asla (8 ISA imkansız) | `cd::simd::vendor::highway` |
| **miniaudio** (device I/O) | MIT/PD | **K4** | **Phase 3 zorunlu** (4-6 ay native) | `cd::audio::vendor::miniaudio` |
| **miniz** (ZIP CDC) | zlib | K3 | Phase 4+ kendi INFLATE | `cd::io::vendor::miniz` |
| **GameNetworkingSockets (Valve)** | BSD-3 | K3 | Phase 5+ değerlendir | `cd::net::vendor::gns` |
| **libopus** | BSD | K2 | Asla (codec uzmanlık) | `cd::audio::vendor::opus` |
| **dr_wav / dr_flac** | Public Domain | K1 | Asla | `cd::audio::vendor::drlibs` |

### C. Opsiyonel / Plugin Vendor (build flag opt-in)

| Vendor | Opt-in flag | Sebep |
|---|---|---|
| `ufbx` (FBX importer) | `CD_ASSET_FBX=ON` | Format-specific; opsiyonel |
| Pixar USD SDK | `CD_ASSET_USD=ON` | DCC interop; ağır SDK |
| Steam Audio | `CD_AUDIO_STEAM=ON` | HRTF + occlusion plugin |
| PhysX 5 | `CD_PHYSICS_PHYSX=ON` | GPU soft body / fluid |
| ICU full | `CD_I18N_ICU=ON` | Editor + asset pipeline heavy mode |
| fribidi | `CD_I18N_BIDI=ON` | BiDi text layout |
| msdfgen | `CD_UI_MSDF=ON` | Resolution-independent fonts |
| OpenTelemetry C++ SDK | `CD_TELEMETRY_OTEL=ON` | Production telemetry |
| libdatachannel | `CD_NET_WEBRTC=ON` | Voice/browser bridge |
| Sentry SDK | `CD_DIAG_SENTRY=ON` | Crash upload backend |

### D. Reddedilen Vendor (yozlaştırıcı)

| Vendor | Sebep |
|---|---|
| Wwise, FMOD | Royalty + proprietary + vendor lock |
| Havok Physics | Closed source |
| Photon Engine | Closed cloud lock-in |
| ImGui (Dear) | Kullanıcı yasakladı (custom UI) |
| Bullet Physics | Legacy, modern multi-core'a uymaz |
| jemalloc | Effectively abandoned 2024-25 |
| Resonance Audio | Google arşivledi 2024 |
| Optick profiler | Unmaintained (2022 son commit) |
| Breakpad | Crashpad halefi |
| Yoga (Flexbox) | Library-oriented engine'de ext dep eşiği yüksek |
| Skia | 1M+ LOC, build complexity, Google ecosystem coupling |
| RmlUi | CSS parser için fork maliyeti |
| Backtrace.io SDK | Commercial vendor lock-in |
| NoesisGUI | Commercial XAML |
| Ultralight | Commercial HTML/CSS |
| FMOD/Wwise (audio) | Commercial royalty + library-oriented çelişir |
| Newton Game Dynamics / ODE | Legacy, modern AAA değil |

### E. Replace-Ready Discipline (D1) — Tüm vendor için ZORUNLU

Her vendor üç-katmanlı namespace pattern'ine uyar:

```cpp
namespace cd::<lib> {
    // Interface (stable across implementations)
    class I<Name> { virtual ~I<Name>() = default; ... };

    // Factory + capability query
    std::unique_ptr<I<Name>> create_default();
    bool supports_backend(BackendId);
}

namespace cd::<lib>::vendor::<vendor_name> {
    class <Name>Impl : public I<Name> { ... };
}

namespace cd::<lib>::native::<our_name> {
    // Reserved for our own implementation, future
    class <Name>Impl : public I<Name> { ... };
}
```

CMake flag: `CD_<LIB>_BACKEND=vendor_name|native_name|both` controls which `Impl` is compiled. `both` enables runtime selection + A/B testing.

### F. Replace Hedefleri (Phase Timeline)

| Phase | Replace Hedefi | Effort | Avantaj |
|---|---|---|---|
| **Phase 2** (Foundation impl, ~6 ay) | mimalloc'tan basit `cd::mem::native::tlsf_arena_freelist` | 3-4 ay (Phase 2 içinde) | Allocator policy tam kontrol |
| **Phase 3** (Audio impl, ~3 ay) | miniaudio'dan `cd::audio::native::*` (WASAPI/ALSA/CoreAudio/AAudio) | 4-6 ay | Düşük gecikme + kontrol |
| **Phase 3** (Allocator full) | TLSF + arena + freelist + tracking — production-grade | 6-12 ay | mimalloc'tan tamamen ayrıl |
| **Phase 4** (Profiler/Crash impl, ~4 ay) | Custom in-engine profiler (Tracy yerini almaz, çift yaşar) + custom crash handler (Crashpad replace) | 6-12 ay | Engine'e tam entegre, sembol pipeline kontrol |
| **Phase 5+** | Box2D → kendi 2D; Jolt → değerlendir; GNS → kendi transport | 12-24 ay | Tam bağımsızlık (opsiyonel) |

**Phase 1 (mevcut)**: replace YOK; tasarımda hazır. Interface stabilizasyonu yeterli.

### G. Vendor Approval Process

Yeni vendor önerisi için:
1. `researcher` ajan analiz raporu yazar (kategori K1-K5, lisans, replace ROI).
2. `architect` ajan adapter interface taslağı çıkarır.
3. `ethics-integrity-reviewer` ajan lisans uyumu + dual-use check.
4. Kullanıcı (Cemal) son onay.
5. Kabul edilince bu ADR'a satır eklenir.

## Reddedilen Alternatifler

- **Tam vendor kabul (UE5 pattern)**: çekirdek kontrolün kaybı, library-oriented prensibe aykırı.
- **Tam custom (UE3 öncesi pattern)**: 5-10 yıl kayıp, Phase 1 hız hedefiyle çelişir.
- **vcpkg port-only model**: bağımlılık şişer, izole vendor kontrolü zayıflar.
- **Per-ADR vendor karar dağıtık**: tutarsızlık + replace policy çakışması; tek normatif ADR daha temiz.

## Sonuçlar

**Pozitif**:
- Tek noktada tüm vendor kararları; tutarlılık.
- Replace-Ready discipline her yerde uygulanır (D1).
- Low-level kendimiz yazma niyetimiz takvimle belgelenir.
- Yeni vendor önerisi süreci açık.

**Negatif / Risk**:
- Adapter pattern indirection (vtable) — hot path için inline-friendly + compile-time path mevcut.
- Üç namespace katmanı (interface + vendor::* + native::*) cognitive overhead — boilerplate codegen ile azaltılır.
- Replace zamanlama (Phase 2-3) ertelenebilir → mimalloc/miniaudio kaynaklı bağımlılık uzar.

## Cross-Cutting

Bu ADR **tüm diğer ADR'ları normatif olarak bağlar**:
- ADR-001..014: her vendor önerisi bu listede + Replace-Ready namespace pattern'ine uymalı.
- ADR-017 (DtForHil Salvage): vendor değil; "internal salvage" — namespace remap kuralı bu ADR'da değil ADR-017'de.
- Yeni ADR önerisinde yeni vendor varsa bu ADR güncellenmeli.

## Sınır

Bu ADR vendor kütüphaneleri kapsar. DtForHil salvage **vendor değildir** (bizim ekosistemimiz içinde refactor edilir) — ADR-017 ele alır.
