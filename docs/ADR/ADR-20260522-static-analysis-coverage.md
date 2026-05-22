# ADR-20260522 — Static analysis (.clang-tidy) + code coverage gates

## Bağlam

CI'de yalnızca clang-format + compile + test vardı. CLAUDE.md §3 ("Doğruluk
/ Evidence-Based") ve §11 (CI/CD) ek statik analiz + coverage ölçümü
gerekiyor — Q1 dergi tekrar-edilebilirliği için tüm metrics dosyalanmalı.

## Karar

### `.clang-tidy` (W16.2)

Repo köküne `.clang-tidy` eklendi. Aktif kontrol grupları:

* `bugprone-*` (narrowing-conversions hariç — kasıtlı)
* `cert-*`
* `cppcoreguidelines-*` (magic-numbers, special-member-functions
  vb. hariç — engine konvansiyonumuza ters)
* `hicpp-*`
* `misc-*` (include-cleaner hariç — false positive yoğun)
* `modernize-*` (trailing-return-type ve pass-by-value hariç)
* `performance-*`, `portability-*`, `readability-*` (magic-numbers
  ve function-cognitive-complexity hariç — gameplay kodu için)

Identifier-naming kuralları:
* `CamelCase` — class/struct/enum
* `lower_case` — function/method/variable/member/namespace
* `k`-prefix CamelCase — enum constants + constexpr variables
* `_`-suffix lower_case — private members
* `UPPER_CASE` (CD_-prefix) — macros

CI job (`tidy`): `lint` job'ından sonra çalışır. PR diff'inde değişen
.cpp/.hpp'leri pull → `compile_commands.json` üzerinden `clang-tidy
--warnings-as-errors='*'` çağırır. Tam ağaç analizi PR-gating değil
(nightly workflow planlı).

### Coverage (W16.3)

`cmake/CDCoverage.cmake` yeni modül. `CD_ENABLE_COVERAGE=ON` cache
değişkeni ile aktive edilir. GCC/Clang `-fprofile-arcs -ftest-coverage`
ekler; MSVC için OpenCppCoverage external tool (CI sorumlu).

CMake custom target'ları:
* `coverage-clean` → her .gcda dosyasını sil (fresh run için)
* `coverage-report` → gcovr → coverage.html + coverage.xml
* `coverage-summary` → stdout line-by-line özet

CI job (`coverage`): Ubuntu + GCC + gcovr. CI matrix lean tutmak için
yalnızca tek OS. HTML + Cobertura XML artifact olarak upload.

## Reddedilen alternatifler

* **CodeQL / SonarCloud:** Kapsam geniş ama setup ağır, kendi hostingleri
  paid. clang-tidy + gcov OSS, sıfır maliyet.
* **PVS-Studio / Coverity:** Ticari lisans gerektirir, OSS engine için
  uygun değil.
* **Code coverage Windows'ta MSVC üzerinden:** OpenCppCoverage iyi ama
  CI matrix'i karmaşıklaştırıyor. Linux gcov tek OS coverage'ı yeterli
  bilgi sağlıyor — Q1 reproducibility için.

## Sonuçlar

* Yeni PR'lar artık clang-tidy gate'inden geçmeden merge edilemez.
* Coverage raporu her CI run'da artifact olarak indirilebilir; PR yorumu
  olarak yüzde değişimini göstermek future enhancement.
* Mevcut kodda clang-tidy warning'leri var (özellikle cdmesh ve
  file_watcher TU'larında). Bunlar CI gate'e takılmıyor çünkü
  `--warnings-as-errors` yalnızca DEĞİŞEN dosyalara uygulanıyor; mevcut
  warning'ler ayrı bir cleanup PR'ı ile temizlenecek.

## Açık sorular

* Coverage gate (minimum line %) eklenmeli mi? Şu an raporlayıcı,
  bloklayıcı değil. v2 hedefi: % drop > 2 ise PR fail.
