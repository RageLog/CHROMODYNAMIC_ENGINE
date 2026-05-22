# research/papers/ — CHROMODYNAMIC Mimari Kitap Koleksiyonu

> Bu klasör **telifli kitapları** içerir. `.gitignore` ile repo'ya commit edilmez. Kullanıcı manuel olarak telifli olanları, asistan açık-erişimli olanları yerleştirir.

## İçindekiler

### Mimari ve Yazılım Tasarımı (zorunlu referans)

| Kitap | Yazar | Yıl | Konu | Durum |
|------|-------|-----|------|-------|
| C++ Software Design — Design Principles and Patterns for High-Quality Software | Klaus Iglberger | 2022 | SOLID, GoF, modern C++ tasarım — **ADR formatı bu kitaptan** | ✅ taşındı |
| Fundamentals of Software Architecture | Mark Richards, Neal Ford | 2020 | Mimari karakteristikleri, trade-off analizi, architectural style'lar | ✅ taşındı |
| Software Architecture: The Hard Parts | Richards, Ford et al. | 2021 | Distributed mimari, modülarite, data ownership | ✅ taşındı |
| Professional CMake — A Practical Guide | Craig Scott | 2024+ | Modern CMake, presets, target-based design | ⏳ manuel ekle |

### Game Engine Mimarisi

| Kitap | Yazar | Yıl | Konu | Durum |
|------|-------|-----|------|-------|
| Game Engine Architecture | Jason Gregory | 3rd ed. 2018 | Full-stack engine — render, audio, anim, physics, tooling | ⏳ manuel ekle (Naughty Dog tech-lead'in standart referansı) |
| Real-Time Rendering | Akenine-Möller, Haines, Hoffman, Pesce, Iwanicki, Hillaire | 4th ed. 2018 | Modern rendering matematiği + algoritmaları | ⏳ manuel ekle |
| Foundations of Game Engine Development (Vol 1-3) | Eric Lengyel | 2016-2024 | Math, rendering, physics — daha derin matematik | ⏳ manuel ekle |
| Game Programming Patterns | Bob Nystrom | 2014 | GoF + game-spesifik pattern'lar — **açık erişim** | ⏳ otomatik indirilecek |

### Veri-Yönelimli & Performans

| Kitap | Yazar | Yıl | Konu | Durum |
|------|-------|-----|------|-------|
| Data-Oriented Design Book | Richard Fabian | 2018+ | DOD, ECS arkaplanı — **açık erişim** | ⏳ otomatik indirilecek |
| Computer Architecture: A Quantitative Approach | Hennessy, Patterson | 6th ed. | Cache, branch, pipeline — perf temeli | ⏳ manuel ekle |
| What Every Programmer Should Know About Memory | Ulrich Drepper | 2007 | Memory perf, NUMA — açık erişim PDF (LWN.net) | ⏳ otomatik indirilecek |

### Concurrency

| Kitap | Yazar | Yıl | Konu | Durum |
|------|-------|-----|------|-------|
| C++ Concurrency in Action | Anthony Williams | 2nd ed. 2019 | std::thread, atomic, executors | ⏳ manuel ekle |
| The Art of Multiprocessor Programming | Herlihy, Shavit | 2nd ed. 2020 | Lock-free, formal model | ⏳ manuel ekle (opsiyonel) |

### Graphics

| Kitap | Yazar | Yıl | Konu | Durum |
|------|-------|-----|------|-------|
| Physically Based Rendering — From Theory to Implementation | Pharr, Jakob, Humphreys | 4th ed. 2023 | PBR matematiği, ray tracing — **açık erişim** (pbrt.org) | ⏳ otomatik indirilecek |
| GPU Pro / GPU Zen series | Wolfgang Engel (ed.) | various | Pratik render teknikleri | ⏳ manuel ekle |
| Ray Tracing in One Weekend / In a Weekend Series | Peter Shirley | 2019+ | RT temeli — **açık erişim** | ⏳ otomatik indirilecek |

### Vulkan / Modern API

| Kitap | Yazar | Yıl | Konu | Durum |
|------|-------|-----|------|-------|
| Vulkan Programming Guide (Khronos) | Sellers, Kessenich | 2016 | Vulkan API — sürüm eski ama temeli sağlam | ⏳ manuel (opsiyonel) |
| 3D Game Engine Programming (Vulkan-based) | Various | varied | API-pratik | ⏳ manuel (opsiyonel) |

## Kullanım

- ADR yazarken bu kitaplardan **doğrudan alıntı** yapılabilir (telifli olanlar için "fair use" - kısa pasaj, tartışma amaçlı). Tam metin LaTeX/whitepaper kapsamı dışında.
- `architect` ajanı tasarım kararı verirken bu klasördeki kitapları **okuyabilir**.
- Bir mimari karar için akademik referans gerekiyorsa: `research/library/` (peer-reviewed makaleler, Demir Kural geçerli) kullanılır.

## Klasör politikası

- Bu klasör **`.gitignore`** içindedir. Telif hakkı nedeniyle repo'ya commit edilmez.
- Açık-erişimli kitaplar otomatik indirilebilir; telifliler manuel.
- İsim konvansiyonu: `<Yazar> - <Kısa Başlık>.<ext>` veya `<Yazar><Yıl> - <Kısa Başlık>.<ext>`.
