# ADR-20260522 — `cmake_minimum_required` + `CMAKE_POLICY_VERSION_MINIMUM`

## Bağlam

CMake 4.x altında iki ortogonal sorun ortaya çıktı:

1. **Proje-içi `cmake_minimum_required` tutarsızlığı.** Repo'da dört
   farklı dosyada dört farklı versiyon bildirimi vardı:
   * `CMakeLists.txt`: `VERSION 3.25`
   * `CMakeModules/util.cmake`: `VERSION 3.18`
   * `CMakeModules/VersionFromGit.cmake`: `VERSION 3.0.0` (vendored
     fork'tan kalma — CMake 4.x bunu fatal eror olarak reddediyor)
   * `Dependencies/CMakeLists.txt`: `VERSION 3.18`

2. **Vendor dep'lerin düşük `cmake_minimum_required` deklarasyonları.**
   FetchContent ile çekilen projeler:
   * `tinygltf` v2.9.3 → `VERSION 3.6` (deprecation warning, < 3.10)
   * `volk` v1.3.290 → `VERSION 3.10`
   * `vma` v3.1 → `VERSION 3.15...3.26` (range syntax — temiz)

   CMake 4.x "compat with CMake < X.Y will be removed" deprecation
   warning'i emit ediyor.

Sonuç: configure her seferinde ya fatal error ya da bir sürü warning
ile gürültülü.

## Karar

**(a)** Proje-içi tüm `cmake_minimum_required(VERSION ...)` çağrıları
tek bir sabit yüksek değere kilitlendi: **3.28**.

| Dosya | Önce | Sonra |
|---|---|---|
| `CMakeLists.txt` | 3.25 | 3.28 |
| `CMakeModules/util.cmake` | 3.18 | 3.28 |
| `CMakeModules/VersionFromGit.cmake` | 3.0.0 | 3.28 |
| `Dependencies/CMakeLists.txt` | 3.18 | 3.28 |

3.28 seçimi: `BLOCK/ENDBLOCK`, presets v8, modern FetchContent. Her
modern CMake feature'ını gerektirmeden kullanabiliyoruz.

**(b)** Top-level `CMakeLists.txt`'e cache değişkeni eklendi:
```cmake
set(CMAKE_POLICY_VERSION_MINIMUM 3.15 CACHE STRING
    "Floor for vendored deps' cmake_minimum_required" FORCE)
```

Bu değişken `cmake_minimum_required` tarafından honor ediliyor: vendor
dep'in deklare ettiği versiyon bu floor'dan düşükse, floor kazanıyor —
deprecation warning susturuluyor. **FORCE** kritik: stale cache'ten
gelen eski değer (3.5) yeni kodu override etmesin.

## Reddedilen alternatifler

* **`CMAKE_POLICY_VERSION_MINIMUM` set without FORCE:** İlk configure'da
  cache'lenir; sonraki kod değişiklikleri yok sayılır. "Sessiz dejenere"
  failure modu.
* **Vendor dep'leri patch'lemek:** Her dep için her sürüm bump'ında
  yeniden patch — sürdürülemez.
* **`cmake_minimum_required` 3.5'te tutmak:** CMake 4.x'in kabul ettiği
  alt sınır 3.5, ama bu BLOCK / preset v8 gibi modern feature'ları
  yasaklar.
* **Per-dep `set(CMAKE_POLICY_VERSION_MINIMUM ...)`:** Her FetchContent
  bloğunda tekrarlamak gerekirdi; tek bir global ayar daha temiz.

## Sonuçlar

* Configure çıktısı temiz (zero deprecation warning, zero compat error)
  hem yerel hem CI'de.
* Yeni vendor dep eklendiğinde alt sınırı `cmake_minimum_required` >
  3.15 olduğu sürece intervention gerekmez.
* Bir gün CMake 5.x deprecation eşiğini 3.15'in üstüne taşırsa, tek bir
  satır değişikliği (3.15 → 3.20 vb.) yeterli olacak.

## Açık sorular

* CMake 4.2.1 yerel olarak çalışıyor; CI ubuntu-24.04 stock CMake'i
  3.28.3. Compat OK ama gelecek bump'lar için izlenmeli.
