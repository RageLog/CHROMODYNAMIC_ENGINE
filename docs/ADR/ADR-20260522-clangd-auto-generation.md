# ADR-20260522 — .clangd auto-generation per CMake preset

## Bağlam

clangd (LSP backend for VS Code / CLion / nvim) tek bir `.clangd` config
dosyasından çalışır. CHROMODYNAMIC dört Windows compiler preset'i
desteklediği için (`msvc-base`, `clangcl-win-base`, `gcc-ucrt64-base`,
`llvm-win-base`) tek bir hard-coded driver/std-flag IDE deneyimini
bozuyordu:

* `Compiler: clang-cl` + `/std:c++23` çalışıyor MSVC ile, GCC/Clang++ ile
  patlıyor ("unknown flag /std:c++23").
* `Compiler: g++` + `-std=c++23` çalışıyor MinGW ile, MSVC TU'larında
  MSVC STL'in iç SFINAE'sini parse edemiyor (yüzlerce sahte error).
* FetchContent `_deps` klasörü preset başına farklı yola düşüyor
  (`build/ninja-base/_deps` vs `build/msvc-base/_deps`).

Reference: JNICmakeStarter projesi aynı sorunu `configure_file` ile
otomatik üretilen `.clangd` ile çözmüştü.

## Karar

Şu yapıyı benimsedik:

```
.clangd.in              ← tracked template, @VAR@ token'ları içerir
CMakeModules/Clangd.cmake  ← driver/std/build_dir detect, configure_file
.clangd                 ← gitignored, her configure'da yeniden üretilir
```

Driver seçimi:
- `MSVC` (cl.exe) → `clang-cl` (parser modu; build hâlâ cl.exe ile)
- `MINGW` / `GNU` → `g++`
- `Clang` (native) → `clang++`

Std-flag:
- MSVC drive → `/std:c++23`
- Diğer → `-std=c++23`

Build dir:
- `@CD_CLANGD_BUILD_DIR@` → `${CMAKE_BINARY_DIR}` (FetchContent `_deps`)

Ayrıca clangd phantom-error baskılaması (büyük `Suppress:` listesi +
`.hpp/.h` blanket suppress) `.clangd.in` içinde kalıcı oldu çünkü gerçek
derleyici hâlâ tek hakikat kaynağı (CLAUDE.md §3 evidence-based).

## Reddedilen alternatifler

* **Hard-code per-developer:** Geliştirici el ile `.clangd`'ı düzenler →
  her preset değişiminde bozulur, takım tutarlılığı yok.
* **VS Code workspace-specific config:** Sadece VS Code çözer; CLion /
  nvim / Zed kullanıcılarını dışlar.
* **clangd configFile per-directory:** Çoklu config dosyası fragmente,
  bakım yükü yüksek.

## Sonuçlar

* `cmake --preset <X>` sonunda `[cd] .clangd regenerated (driver=...)`
  log'u görünür. Geliştirici hangi preset üzerinde olduğunu bir bakışta
  görür.
* Phantom error sayısı dramatik düştü (önceden ~20-30 sahte error per
  TU, şimdi 0-3 — yalnızca freshly-edited file'ın compile_commands
  güncellenmeden önceki kısa süreli stale view).
* `.clangd.in` git'te, `.clangd` `.gitignore`'da. Yeni geliştirici clone
  + `cmake --preset` + IDE açar; intervention sıfır.

## Açık sorular

* Cross-platform `.clangd.in` (Linux + macOS) için aynı template
  yeterli mi? Linux preset'leri (`ci-gcc`, `ci-clang`, `ci-appleclang`)
  ekledikçe doğrulanacak.
