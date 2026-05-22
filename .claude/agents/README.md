# CHROMODYNAMIC — Hibrit Subagent Suite (Engine Architect + Graphics/Engine Researcher)

> Bu suite üç kimliği **kayıpsız** birleştirir:
>
> 1. **Engine mimar/geliştirici kimliği** — modern C++23 / CMake / Ninja, ASAN/UBSAN, library-oriented modüler engine kalitesi.
> 2. **Graphics & engine SOTA araştırmacı kimliği** — Unreal RHI, Filament, bgfx, Sokol, The Forge, Diligent, EnTT, Bevy, Jolt karşılaştırması; engineering blog/talk/paper toplama.
> 3. **Akademik araştırmacı kimliği** — peer-reviewed rendering, GPU sched, ECS, allocator, concurrency literatürü; Q1 disiplini ile kanıt + atıf.
>
> Hiçbir geliştirme yeteneği kaybedilmedi; üstüne akademik denetim katmanı eklendi. Bu suite, DtForHil hibrit suite'inin **game engine** domain'ine **kayıpsız** uyarlanmış sürümüdür.

## Demir Kural (TÜM AJANLAR İÇİN — istisnasız)

> **İndirilmemiş makale kullanılamaz.**
>
> Bir akademik referans ancak ve ancak şunlar varsa atıf/alıntı için uygundur:
>
> - `research/library/pdf/<bibkey>.pdf` dosyası mevcut.
> - `research/library/MANIFEST.csv` içinde **doğrulanmış** satırı var (sha256, downloaded_at, verifier).
> - `research/library/bibliography.bib` içinde **eksiksiz** BibTeX girdisi var (DOI veya arXiv ID dahil).
>
> Bu üçü tamam değilse → makale **mevcut değildir**. WebSearch/abstract/Google Scholar özeti **kaynak değildir**.
> İndirilemeyen makaleler `research/library/ATTEMPTS.md`'ye sebepleriyle loglanır ve **kullanılmaz**.

Engineering kaynakları (vendor docs, GitHub README, conference talk, blog) için Demir Kural geçerli **değil** — link + erişim tarihi yeterli; bunlar `researcher` ajanı tarafından toplanır.

## Kütüphane Düzeni (Tek Otorite)

```text
research/
├── library/
│   ├── pdf/                  # İndirilen PDF'ler (kanonik isim: <bibkey>.pdf)
│   ├── notes/                # Her makale için 1 paragraf özet (<bibkey>.md)
│   ├── bibliography.bib      # BibTeX (sadece MANIFEST'te doğrulanmış)
│   ├── MANIFEST.csv          # bibkey,doi,title,authors,year,venue,pdf_path,sha256,downloaded_at,verified_at,verifier
│   └── ATTEMPTS.md           # İndirilemeyenler (paywall, 404, kapsam dışı) — atıf yapılamaz
├── papers/                   # MİMARİ KİTAPLARI (Iglberger, Richards-Ford, Gregory, vb.) — gitignored
├── reports/                  # Denetim raporları
└── HUMAN_VOICE.md            # AI-detection önleme + Türkçe/İngilizce yazım kuralları
```

`docs/ADR/` — kalıcı mimari karar kayıtları (Iglberger formatı).

## Tier ve Model Dağılımı

| Tier                    | Ajan                      | Model  | Yazma yetkisi                                                  |
| ----------------------- | ------------------------- | ------ | -------------------------------------------------------------- |
| Orchestration           | team-lead                 | opus   | sadece Agent dispatch + TodoWrite                              |
| Engine Engineering      | analyst                   | sonnet | sadece rapor (etki/risk haritası, kod tabanı)                  |
|                         | architect                 | opus   | `.hpp` / interface + `docs/ADR/`                               |
|                         | planner                   | sonnet | TodoWrite WBS                                                  |
|                         | developer                 | sonnet | C++23 implementasyon                                           |
|                         | tester                    | sonnet | gtest/catch2 tests + golden image diff hazır                   |
|                         | build-devops              | sonnet | CMakeLists.txt, presets, vcpkg, Conan, CI                      |
|                         | safety-integration        | opus   | concurrency / lifetime / UB denetim (BLOCKING, engine kritik)  |
|                         | code-consistency          | haiku  | clang-tidy / clang-format mekanik fix                          |
|                         | troubleshooter            | sonnet | RCA reçete                                                     |
|                         | researcher                | sonnet | rapor (engine/graphics SOTA: Unreal/Filament/bgfx/EnTT/Bevy)   |
| Academic Research       | academic-researcher       | sonnet | `research/library/{pdf,notes,bibliography.bib,MANIFEST.csv}`    |
|                         | citation-verifier         | sonnet | `research/reports/verification_*` (BLOCKING)                    |
|                         | independent-auditor       | opus   | `research/reports/independent_audit_*` (BAĞIMSIZ DOUBLE-CHECK)  |
|                         | peer-review-simulator     | opus   | `research/reports/peer_review_*` (RFC veya tez modunda)        |
| Methodology & Stats     | methodology-auditor       | opus   | `research/reports/methodology_audit_*` (BLOCKING — benchmark)   |
|                         | statistical-analyst       | sonnet | `research/reports/stats/*` (benchmark anlamlılık)               |
|                         | reproducibility-engineer  | sonnet | config/seed, repro script (benchmark + render test)            |
|                         | data-pipeline-auditor     | sonnet | `research/reports/data_pipeline_audit_*` (asset pipeline)       |
| Writing & Visualization | latex-writer              | sonnet | `paper/sections/*.tex` (opsiyonel: tez veya whitepaper)        |
|                         | figure-table-curator      | sonnet | `paper/figures/`, `paper/tables/`, scripts                     |
|                         | doc-writer                | haiku  | docstring, README, ADR rendering, Doxygen comments             |
| UI                      | ui-architect              | sonnet | UI spec (engine UI + editor UI)                                |
|                         | ui-developer              | sonnet | UI kod (custom IMGUI / retained, kararlaştırılacak)            |
|                         | ui-tester                 | sonnet | UI test (visual regression)                                    |
|                         | ux-developer              | sonnet | UX revizyon (editor workflow, panel layout)                    |
| Templates               | slide-builder             | sonnet | `research/templates/beamer/<template>/` (sunum/RFC modu)        |
| Governance & Release    | ethics-integrity-reviewer | opus   | `research/reports/ethics_integrity_*` (lisans + dual-use)       |
|                         | branch-strategy           | haiku  | git ops                                                        |
|                         | release-manager           | sonnet | SemVer/CalVer, changelog, tag                                  |
|                         | installer-maker           | sonnet | CPack/NSIS/WiX/AppImage paketleme                              |
|                         | deploy-operator           | sonnet | dağıtım (kullanıcı onayı şart)                                  |

**Opus** sadece üst düzey karar/orkestrasyon/etik için. **Haiku** mekanik işler. Geri kalan ana iş yükü **sonnet**.

## Sıfır-Tolerans Prensipleri (TÜM AJANLAR)

1. **İndirilmemiş akademik kaynak kullanılamaz** (Demir Kural).
2. **Halüsinasyon yok**: Her teknik iddia ya kanıt (komut çıktısı / dosya yolu / link+tarih) ya da yerel-doğrulanmış atıf.
3. **Kanıt zorunlu**: "done" iddiası komut + son log satırları + commit hash olmadan kabul edilmez.
4. **Cerrahi düzenleme**: `Edit` ile noktasal; full rewrite yasak.
5. **Geri-alınamaz işlem**: `reset --hard`, `branch -D`, `force-push`, mass `rm` → kullanıcı onayı şart.
6. **Token disiplini**: Aynı analizi iki ajana yaptırma; özet paslaş.
7. **Paralel yürütme**: Bağımsız işler tek mesajda paralel `Agent` çağrısıyla.
8. **Research-first**: Implementasyondan önce daima SOTA + akademik tarama; Phase 1'de kod yazma.

## BLOCKING Gate'ler

Şu ajanların verdict'i BLOCK ise pipeline durur, kullanıcıya escalation:

- `citation-verifier` — halüsinasyon / indirilmemiş kaynaktan alıntı / sayı uyuşmazlığı
- `independent-auditor` — bağımsız ikinci doğrulayıcı; citation-verifier ile uyuşmazlık → CRITICAL
- `methodology-auditor` — deneysel tasarım / istatistik rigor (benchmark çalışmaları için)
- `data-pipeline-auditor` — asset pipeline / split / leak / cache invariants
- `ethics-integrity-reviewer` — lisans uyumu / dual-use (third-party kod) / PII / intihal
- `safety-integration` — race / lifetime / UB (engine concurrency: render thread, job system, async asset)

## Yazım Kalite Kuralı (AI-detection önleme)

Tüm yazıcı ajanlar (`latex-writer`, `doc-writer` akademik mod, `peer-review-simulator` raporu, `academic-researcher` notları, `ethics-integrity-reviewer` raporu) `research/HUMAN_VOICE.md` kurallarına bağlıdır:

- Yasak kelime/öbek listesi (en + tr)
- Cümle-uzunluk varyansı + burstiness
- Cümle-başı bağlaç sınırı
- Post-write mekanik tarama (zorunlu)

Tarama başarısızsa yazı kabul edilmez — yeniden yazılır.

## Pipeline Şablonları

### A) Engine Subsystem Design Pipeline (Phase 1 — TASARIM, AKTİF MOD)

```text
team-lead
 ├─ [Discovery, paralel]   researcher (engine/graphics SOTA: Unreal/Filament/bgfx/...)
 │                         academic-researcher (peer-reviewed papers, PDF indir + MANIFEST)
 │                         analyst (mevcut kod tabanı etki haritası)
 │                         reproducibility-engineer (benchmark env baseline — varsa)
 ├─ [Design]               methodology-auditor (benchmark design varsa BLOCKING)
 │                         architect (.hpp interface + ADR yazımı)
 │                         planner (atomik WBS)
 ├─ [Verify, paralel]      citation-verifier (akademik atıflar varsa BLOCKING)
 │                         independent-auditor (BAĞIMSIZ ikinci doğrulayıcı)
 │                         peer-review-simulator (RFC reviewer simulation)
 ├─ [Gate]                 ethics-integrity-reviewer (lisans/3rd party kod)
 │                         safety-integration (concurrency surface area BLOCKING)
 └─ [Output]               docs/ADR/ADR-* + long-form design doc
```

### B) Engine Implementation Pipeline (Phase ≥ 2 — KOD, İLERİDE)

```text
team-lead
 ├─ [Discovery, paralel]   analyst (etki/risk) │ researcher (kütüphane güncelleme)
 ├─ [Design]               architect (.hpp + ADR) → planner (WBS)
 ├─ [Implement, paralel]   tester (failing test) → developer × N → code-consistency
 │                         + safety-integration (concurrency varsa)
 ├─ [Build & Test]         build-devops (preset/CMake) → tester (ctest + golden image)
 ├─ [UI varsa]             ui-architect → ui-developer → ui-tester → ux-developer
 ├─ [Verify]               troubleshooter (regresyon RCA)
 │                         data-pipeline-auditor (asset pipeline değiştiyse)
 └─ [Release]              release-manager → installer-maker → deploy-operator (onay)
```

### C) Benchmark/Whitepaper Pipeline (opsiyonel)

```text
team-lead
 ├─ [Discovery]            academic-researcher (related work PDF)
 ├─ [Design]               methodology-auditor (BLOCKING)
 ├─ [Implement]            developer (benchmark harness) → tester
 ├─ [Run]                  reproducibility-engineer (multi-seed, multi-platform)
 │                         data-pipeline-auditor (re-verify)
 │                         statistical-analyst (CI, effect size)
 │                         figure-table-curator
 ├─ [Write]                latex-writer + doc-writer (HUMAN_VOICE.md uygulanır)
 ├─ [Verify, paralel]      citation-verifier (BLOCKING) ‖ independent-auditor
 └─ [Gate]                 ethics-integrity-reviewer → peer-review-simulator
```

Üç pipeline aynı projede koşar; team-lead, niyetten hangisinin (veya hangilerinin) gerektiğine karar verir. **Phase 1 = sadece Pipeline A aktif.**

## Çağrılma Yolları

1. **Otomatik dispatch** — `team-lead`'i çağır:

   ```text
   "RHI (Render Hardware Interface) tasarımı yapalım. Unreal/Filament/bgfx/Sokol/Forge/Diligent
    SOTA analizi yap, akademik literatür tara (PDF indir), kendi tasarımımızın hangi noktada
    üstün olacağını gerekçele, ADR-NNNN-rhi-design.md yaz."
   ```

2. **Doğrudan çağırma** — tek subagent yetiyorsa:

   ```text
   "@researcher hedef: ECS storage strategy SOTA; EnTT vs Flecs vs Bevy archetype;
    Confidence-based puanla; hot-path latency + memory + extensibility kıyasla."
   ```

3. **Paralel** — orkestratör tek mesajda birden çok `Agent` çağrısı yapar.

## Eklenecek Yeni Ajan

Yeni ajan eklenirse: (a) bu README'ye satır ekle, (b) `team-lead.md` ekip haritasına ekle, (c) Demir Kural ve BLOCKING gate listesini güncelle.

## Engine'e Özel Notlar

- **Renderer/Shader/Material** alanında ADR yoğunluğu yüksek olacak → architect + researcher + academic-researcher paralel kullanın.
- **Concurrency** (job system, render thread, async asset, parallel ECS) `safety-integration` için zorunlu inceleme alanı.
- **Asset pipeline** importer/baker/cache → `data-pipeline-auditor` (FBX→glTF dönüşüm, KTX2 sıkıştırma, meshlet üretimi gibi her dönüşüm doğrulanmalı).
- **Lisans** vendor library kullanımı (Jolt, Box2D, miniaudio, FreeType, Harfbuzz, mimalloc, vb.) → `ethics-integrity-reviewer` zorunlu.
- **Cross-API shader portability** (HLSL/Slang → SPIR-V → cross-compile) → `researcher` + akademik literatür.

## Not

Bu suite **DtForHil hibrit suite**'inin (Q1 dergi pipeline + C++ HIL pipeline) **CHROMODYNAMIC engine** domain'ine uyarlanmış sürümüdür. Ana ajan tanımları paylaşılır; sadece **README.md** (bu dosya) + **team-lead.md** + **CLAUDE.md** ile domain bağlamı **engine** olarak yeniden yönlendirilmiştir. Spesifik agent dosyaları kendi içinde mostly domain-agnostic kalır; engine-spesifik kararlar `docs/ADR/` ile ifade edilir.
