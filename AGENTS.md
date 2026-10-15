# CHROMODYNAMIC Engine — Proje Kuralları & Gemini 4 Argon (High) Otomatik Orkestrasyonu

> Bu dosya (`AGENTS.md` / `CLAUDE.md`) **otomatik olarak her oturumda** ve **tüm subagent'larda** yüklenir.

## 0. Otomatik Prompt Niyet Yönlendiricisi (Zero-Friction Auto-Trigger)

Kullanıcı prompt girdiğinde **ajan veya skill adını açıkça yazmasa bile**, niyeti otomatik sınıflandır ve ilgili yeteneği (skill), betiği ve alt ajanları (`invoke_subagent`) **sormadan doğrudan tetikle**:

| Kullanıcı Prompt Niyeti | Otomatik Devreye Alınan Skill & Betik | Otomatik Başlatılan Alt Ajanlar (`invoke_subagent`) |
| :--- | :--- | :--- |
| **Yeni Özellik / Modül / Kodlama** (*"ekle, oluştur, yaz, implement et, geliştir"*) | `context-radar` (`repo_map.py`) → `spec-driven-dev` (`scaffold_spec.py`) → `tdd-red-green-loop` (`run_tdd_cycle.ps1`) | Tek dosya ise doğrudan uygula; çoklu dosya/modül ise paralel `developer` (`inherit`) + `tester` (`pro`) + `safety-integration` (`inherit`) |
| **Marathon / Wave / Backlog İlerlemesi** (*"sıradaki dalga, wave, fazı ilerlet, maraton"*) | `checkpoint-worktree-guard` (`checkpoint_guard.py status`) → `marathon-wave-runner` (`verify_wave.ps1`) | `team-lead` (`inherit`) veya paralel `developer` + `tester` + `code-consistency` |
| **Hata / Crash / Derleme veya Test Fail** (*"hata var, çalışmıyor, crash, segfault, fail, düzelt"*) | `rca-five-whys-debugger` (`parse_failure_log.py`) → `tdd-red-green-loop` (`run_tdd_cycle.ps1`) | Kök neden karmaşıksa `troubleshooter` (`inherit`) + `build-devops` (`pro`) |
| **Mimari Tasarım / SOTA / ADR** (*"mimari, tasarla, ADR, SOTA analizi, katman"*) | `context-radar` (`repo_map.py`) → `engine-sota-adr` (`check_layer_dag.py`) | Paralel `researcher` (`pro`) + `architect` (`inherit`) + `analyst` (`pro`) |
| **UI / Editör Paneli / Görsel & Render** (*"panel, editör, UI, HUD, gizmo, shader, render"*) | `visual-quality-loop` (`audit_ui_quality.py`) → `spec-driven-dev` → `tdd-red-green-loop` | `ui-architect` (`pro`) / `ui-developer` (`inherit`) / `ux-developer` (`pro`) / `ui-tester` (`flash`) |
| **Akademik / Makale / Atıf / Sunum** (*"makale, paper, atıf, kaynak, LaTeX, sunum"*) | `academic-iron-rule` (`verify_manifest.py` + `check_human_voice.py`) | `academic-researcher` (`pro`) → `latex-writer` (`pro`) → paralel `citation-verifier` (`flash`) + `independent-auditor` (`inherit`) |

## 1. Proje Kimliği & Aktif Pipeline'lar

**CHROMODYNAMIC** — cross-platform, cross-API, library-oriented hibrid 2D+3D oyun motoru ve general-purpose rendering/framework (`cd::` / `chroma::` C++23, 120+ bağımsız `cd_<lib>` kütüphanesi). Hedef: state-of-the-art (Filament, bgfx, EnTT/Bevy, Jolt) **aşılacak** mimari kalite. Tüm alt-sistemler bağımsız kütüphane olarak da tüketilebilir.

Üç aktif pipeline paralel ve kesintisiz çalışır:
- **Pipeline A (SOTA Design & ADR)**: Her yeni alt-sistem veya büyük mimari karar **araştırma + Iglberger ADR** (`docs/ADR/ADR-YYYYMMDD-<konu>.md`) ile gerekçelendirilir.
- **Pipeline B (Surgical Implementation & Marathon Waves)**: Onaylı ADR veya `docs/MARATHON_PLAN_NEXT.md` dalgaları (`Wave 1..N`) paralel subagent fan-out ve tek-komut doğrulama (`verify_wave.ps1`) ile kesintisiz uygulanır.
- **Pipeline C (Q1 Academic & Benchmark)**: Tez/makale/sunum çıktılarında Demir Kural (`MANIFEST.csv` + `sha256` + `bibliography.bib`) ve Dual-Auditor (`peer-review-simulator` + `independent-auditor`) kapısı zorunludur.

## 2. Modern C++23 Kalitesi (Zero Tolerance)

- **Bellek**: Sahiplik için yalnızca `std::unique_ptr` / `std::shared_ptr`. Raw pointer sadece non-owning observer. Hot-path için generational handle (`Handle<Tag>`) / opaque ID tercih.
- **Const correctness**: State değiştirmeyen üye fonksiyonlar `const`; değişmeyen lokaller `const`.
- **API**: Public dönüşlerde `[[nodiscard]]`. Sanal metodlarda `override` zorunlu. Public header'da `noexcept` doğru kullanım.
- **Modern idiom**: C++23 (`std::expected`, `std::span`, `constexpr`). `static_cast` kullan, C-style cast yasak. Custom makro minimum.
- **Hata yönetimi**: `std::expected<T, ErrorCode>` / `std::variant` ile tip sisteminde taşı. `catch(...) {}` yasak. Exception sadece ctor/dtor sınırında ve dökümante edilmiş.
- **Sınır kontrolü**: Güvenlik gereken yerlerde `at()`. Buffer overflow / UB / leak'e karşı savun.
- **Library boundary**: Her library `cd::` / `chroma::<lib>::` namespace; explicit export macro; zero global mutable state.

## 3. Doğruluk & Deterministik Otomasyon (Evidence-Based)

- "Kodu yazdım, doğru olmalı" yasak. Her iddianın **kanıtı** olmalı:
  - **Tekil Hedef Hızlı TDD (1–3 sn)**:
    `powershell -ExecutionPolicy Bypass -File "C:/Users/cemal/.gemini/config/plugins/argon-core/skills/tdd-red-green-loop/scripts/run_tdd_cycle.ps1" -Target <test_target>`
  - **Tek-Komut Marathon Wave Doğrulaması**:
    `powershell -ExecutionPolicy Bypass -File "C:/Users/cemal/.gemini/config/plugins/argon-core/skills/marathon-wave-runner/scripts/verify_wave.ps1" -Preset debug`
  - **Katman DAG Kontrolü** (`#include` cycle/upward violation denetimi):
    `python "C:/Users/cemal/.gemini/config/plugins/argon-core/skills/engine-sota-adr/scripts/check_layer_dag.py" --root engine`
  - **Akademik Manifest & Human-Voice Kontrolü**:
    `python "C:/Users/cemal/.gemini/config/plugins/argon-core/skills/academic-iron-rule/scripts/verify_manifest.py"`
    `python "C:/Users/cemal/.gemini/config/plugins/argon-core/skills/academic-iron-rule/scripts/check_human_voice.py" <tex-or-md>`
- Build/test fail ederse **root cause**'u bul (`parse_failure_log.py`), üstünü örtme. `--no-verify` veya bypass yasak.

## 4. Cerrahi Düzenleme & Token Ekonomisi

- Dosyaları `cat`/`echo`/`write_to_file` ile baştan yazma — `replace_file_content` / `multi_replace_file_content` (`Edit`) kullan.
- Önce `repo_map.py` veya `grep_search` ile sembol satırını bul, ardından `view_file` ile yalnızca ilgili satır aralığını (`StartLine`/`EndLine`) oku.
- **Delta-Only Handoff**: Subagent çağrılarında tüm dosyayı prompt'a yapıştırma; yalnızca hedef dosya yolu, satır aralığı ve görevi gönder.
- **`.clang-tidy` `Checks:` scalar'ına ASLA `#` yorum koyma** — folded scalar içinde `#` yorum DEĞİLDİR; virgülsüz metin sonraki kuralı ölü glob'a yapıştırır (`PreToolUse` kancası bunu otomatik engeller).

## 5. Kesintisiz Otonom Maraton & Auto-Continue Protokolü (`docs/PLAN_NEXT_PHASES.md`)

- **Tek Doğruluk Kaynağı**: [`docs/PLAN_NEXT_PHASES.md`](docs/PLAN_NEXT_PHASES.md) içindeki **10-Fazlı Master Plan** (`[ ]` / `[x]` dalga listesi) otonom yürütmenin kalıcı durum defteridir.
- **Auto-Continue (Kota/Oturum Yenilenmesi)**: Kullanıcı *"devam"*, *"."* veya herhangi bir kısa devam komutu verdiğinde (ya da mevcut tur içinde bir dalga `[x]` olduğunda), **kullanıcıya "sıradaki faza geçeyim mi?" diye SORMA**; doğrudan `docs/PLAN_NEXT_PHASES.md` içindeki ilk `[ ]` işaretli Wave'i başlat, uygula, `verify_wave.ps1` / `run_tdd_cycle.ps1` ile doğrula, `[x]` olarak işaretle ve bir sonraki Wave'e kesintisiz geç.

