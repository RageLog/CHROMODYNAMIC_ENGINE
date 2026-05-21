# ADR-014 — CI/CD, Build Cache & Distribution

- **Status**: Accepted (Phase 1 Design)
- **Date**: 2026-05-17
- **Related**: ADR-005 (Foundation), ADR-006 (Asset), ADR-016 (Vendor Matrix)

## Bağlam

Public OSS C++23 engine, 4-compiler × 2-platform matrix, library-oriented packaging, mod-safe asset pipeline, DCO contribution model. Ship surface: **source canonical + reference binaries + downstream package managers**.

Maliyet hedefi: $0/ay CI yıl 1 + minimal CDN cost.

## Karar

### A. CI Host (T26.Q1) — GitHub Actions

**GitHub Actions** (public OSS unlimited minutes, 4 vCPU/16 GB/14 GB SSD Win+Linux, M1 ARM macOS free OSS) + **self-hosted runner pool** (Sprint 20+, ARC Kubernetes, Hetzner AX52 / EPYC bare-metal).

**Reddedilen**: GitLab CI (community gravity GHA'da), Jenkins (ops yükü), Bazel (CMake/vcpkg ekosistem terk).

### B. Build Cache (T26.Q2) — sccache + Cloudflare R2

**sccache** (Mozilla Rust, MSVC dahil multi-compiler, Bevy/Servo prod): backend **Cloudflare R2** (ücretsiz 10 GB + ucuz egress).

```bash
SCCACHE_BUCKET=chromodynamic-cache
SCCACHE_REGION=auto
AWS_ACCESS_KEY_ID=...
```

**GitHub Actions cache** (10 GB repo limit) backup.

**vcpkg binary cache**: `VCPKG_BINARY_SOURCES=clear;x-gha,readwrite` veya `nuget,https://nuget.pkg.github.com/...,readwrite`. Cold install 15-20 dk → cache <1 dk.

**Asset DDC cache (S6 cross-cut)**: aynı R2, ayrı bucket `chromodynamic-ddc`. CI bake upload, dev pull-only. zstd-compressed blob + JSON manifest.

**Reddedilen**: ccache (MSVC yok), distcc/icecream (modern incremental+sccache yeterli; LTO uyumsuz), FASTBuild (Windows/Unreal world'üne özgü).

### C. Compiler Matrix CI

```yaml
strategy:
  fail-fast: false
  matrix:
    os: [windows-2022, ubuntu-24.04]
    compiler: [msvc, clang-cl, clang, gcc]
    config: [Debug, RelWithDebInfo]
    exclude:
      - { os: ubuntu-24.04, compiler: msvc }
      - { os: ubuntu-24.04, compiler: clang-cl }
```

10 paralel job (16 yerine; Linux'ta MSVC/clang-cl yok). Sprint 14 deliverable.

### D. Release Packaging (T26.Q3 = E hepsi)

| Hedef | Generator | Use case | Signing |
|---|---|---|---|
| **Win installer** | CPack NSIS (basit) veya WiX (MSI enterprise) | End-user editor | Authenticode |
| **Win portable** | CPack ZIP | CI artifact, dev | sha256+sig |
| **Linux portable** | AppImage (linuxdeploy+appimagetool) | distro-agnostic | gpg detached |
| **Linux native** | CPack DEB + RPM | apt/dnf, library | repo-key |
| **Linux sandbox** | Flatpak (flathub manifest) | Steam Deck, conservative | flathub OSTree |
| **macOS** | CPack DragNDrop (DMG) + productbuild (.pkg) | End-user | codesign + notarytool |
| **Source** | CPack TGZ + ZIP + GitHub auto-source | Distro packagers | gpg |
| **Steam** | steamcmd app_build + VDF | Game-side, opsiyonel | Valve |
| **Itch.io** | butler push | Indie release | butler |

```cmake
include(CPack)
set(CPACK_GENERATOR "TGZ;ZIP")
if(WIN32)  list(APPEND CPACK_GENERATOR NSIS WIX) endif()
if(LINUX)  list(APPEND CPACK_GENERATOR DEB RPM External) endif()
```

**Library-oriented packaging kritik**: monolitik **`chromodynamic-dev` + `chromodynamic-runtime` split** (Debian convention). vcpkg port `ports/chromodynamic/portfile.cmake` resmi registry için.

### E. Source-First Release Stratejisi

4-compiler × 2 platform × 3 config = **24 binary ABI cehennemi**. Çözüm: **source-first**.

- GitHub Release: source tarball + reference build artifacts (MSVC Release Win64, Clang Release Linux x86_64, Clang Release macOS arm64 — 3 binary referans).
- Diğer kombinasyonları kullanıcı vcpkg/CMake ile kendi build eder.
- Bevy + Filament pattern.

### F. Asset Signing (T26.Q4) — Ed25519 Minisign

| Katman | Algoritma | Tool | Trust root |
|---|---|---|---|
| Source release | SHA-256 + GPG detached | gpg --detach-sign | maintainer key, keys.openpgp.org |
| Asset manifest | Ed25519 + BLAKE3 tree | Minisign / signify | engine release key, in-tree pubkey |
| Win binary | Authenticode SHA-256 | signtool / osslsigncode | **SignPath OSS program (free)** |
| macOS binary | codesign + notarytool | Xcode | Apple Developer ID ($99/yr, yıl 2) |
| Linux pkg | repo GPG | aptly / createrepo | maintainer key |

**Asset signing**: her `.cdpak` → Merkle tree root (BLAKE3) → Ed25519 imza. Engine boot pubkey hard-coded + opsiyonel community-signed mod store pubkey listesi. Format Minisign-compatible.

**Key management**: ilk yıl **age + encrypted file** (1Password vault). Yıl 2+: **YubiKey 5** (FIDO2+OpenPGP slot) hardware token. HSM overkill OSS solo/small team.

### G. Auto-Update (T26.Q5) — Editor için Sparkle Trio

| Platform | Tool | Lisans | Delta |
|---|---|---|---|
| Windows | WinSparkle | MIT | xdelta3 |
| macOS | Sparkle 2 | MIT | bsdiff |
| Linux | AppImageUpdate (zsync2) | MIT | + |
| Linux native | apt/dnf repo refresh | OS | OS-level |
| Game runtime | Steam / Epic launcher | proprietary | + |

**Karar**: Editor için trio (yıl 2 sprint), **engine library için no auto-update** (vcpkg/conan version pin). Editor opt-in feature flag, sessiz arka plan değil. Delta: zsync2 + xdelta3. Full installer fallback hep mevcut. **Game (engine kullanıcısının ürünü) auto-update engine sorumluluğunda DEĞİL**.

### H. Distribution Channel (T26.Q6)

Sıralı katman:

1. **GitHub Releases** (canonical, source + ref binaries + checksums + sig). Git tag `v0.x.y` → GHA `release.yml`.
2. **vcpkg registry overlay** (`chromodynamic-team/vcpkg-registry`; uzun vadede `microsoft/vcpkg` PR).
3. **Conan Center** (recipe `recipes/chromodynamic/all/conanfile.py`).
4. **Cloudflare R2 CDN** (asset DDC delivery, yıl 2).
5. **Steam depot** (oyun katmanı; engine değil — sadece CI template doküman).
6. **Epic / GOG / itch.io** — dokümantasyon + butler template.
7. **Linux distros** (Flathub, AUR, nixpkgs) — community-maintained, upstream metadata.

### I. Aşma Noktaları

- **4-compiler matrix sccache pattern** (çoğu engine 2 compiler; biz 4'ü MSVC dahil sccache backend).
- **Library-bağımsız CPack split** (monolit + per-library opt-in component).
- **DDC + binary cache aynı R2** (UE DDC private; bizim public read, mod community CI gratis).
- **DCO + signed release** (git interpret-trailers + GPG release tag + asset Ed25519).
- **C++23/26 opt-in compiler probe** (CMake `try_compile` matrix CI feature flag → `__cpp_lib_*` macro envanteri).

## Reddedilen

- GitLab CI / Jenkins / Bazel
- distcc / icecream / FASTBuild
- Snap (Canonical lock-in)
- ccache tek başına (MSVC yok)
- CloudHSM (overkill OSS)
- Steam engine binary push (24 ABI patlaması; source-first felsefesine aykırı)
- Per-compiler binary release (ABI patlaması)

## Sonuçlar

**Pozitif**:
- $0 yıl 1 CI cost; community PR ergonomi maksimum.
- ABI sorunları compile-time matrix yakalanır.
- Mod-safety Ed25519 chain.
- Library-bağımsız ship.

**Negatif**:
- sccache MSVC bazı PCH edge-case (dokümante workaround).
- macOS notarization yıl 2 (M1 ARM build cold ship).
- 4-compiler CI cost cache miss durumunda 60 dk+ (cache health monitoring).

**Risk**: GHA OSS policy değişimi (Microsoft) → self-hosted runner backup hazır. Cloudflare R2 lock-in → S3-compatible API, AWS S3'e taşınır.

**Replace-Ready (D1)**: sccache → custom build-cache değil (ROI yok). Crashpad/Tracy backend Phase 4 değerlendir (ADR-013 cross).

## Açık Sorular

| ID | Soru | Çözüm |
|---|---|---|
| Q1 | SignPath OSS sponsorship başvuru zamanı? | Sprint 16 ön-koşul |
| Q2 | Apple Developer ID hesabı: maintainer kim, şirket mi? | $99/yr legal entity tercih |
| Q3 | vcpkg-registry overlay public mi private? | Public (community contribution) |
| Q4 | DDC R2 bucket public-read default — mod hash leak riski? | Public, asset versioning policy doc |
| Q5 | Editor + engine library aynı release cycle mi? | Monorepo tag (engine SemVer, asset CalVer) |
| Q6 | Reproducible build hedef seviyesi? | Best-effort, timestamp+build-path eliminasyonu try |
| Q7 | ARM64 Linux (Raspberry Pi 5, Ampere) S14 mı S22? | S22 (Sprint öncelikli x86_64) |
| Q8 | Steam SDK closed → CI template public repo Valve TOS uyumlu mu? | Doc-only, no SDK shipping |

## Cross-Cutting

- **ADR-005 (Foundation)**: `CMakePresets.json` tek truth source — CI ve dev local aynı flag. Compiler flag profiles: `cd-warnings`, `cd-sanitizers` (ASan/UBSan/TSan/MSan), `cd-lto`, `cd-pgo` interface library targets. `vcpkg.json` + `vcpkg-configuration.json` (registry pin, baseline SHA). `CMAKE_EXPORT_COMPILE_COMMANDS=ON` zorunlu.
- **ADR-006 (Asset)**: DDC remote cache key: `BLAKE3(asset_source) + BLAKE3(cooker_binary) + cooker_version`. CI bake nightly `cmake --build --target cook_all_assets`, R2 upload. Dev `cdpak pull --remote` pull-only. Asset Ed25519 imza bake step son.

## Kanıt

- GitHub Actions OSS: docs.github.com/en/actions/learn-github-actions/usage-limits
- sccache: github.com/mozilla/sccache
- Cloudflare R2: developers.cloudflare.com/r2/
- CPack: cmake.org/cmake/help/latest/module/CPack.html
- Minisign: jedisct1.github.io/minisign/
- SignPath OSS: signpath.io/open-source-program
- WinSparkle: winsparkle.org
- Bevy release: github.com/bevyengine/bevy/releases
- Filament release: github.com/google/filament/releases
- DCO (Linux kernel): developercertificate.org
