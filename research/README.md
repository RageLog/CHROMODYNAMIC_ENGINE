# research/ — CHROMODYNAMIC Engine Araştırma Kökü

Bu dizin CHROMODYNAMIC projesinin **akademik + mimari kitap + SOTA araştırma** tarafıdır. Implementasyon kodu (`Engine/`, `Project/`, `CMakeLists.txt`) ile **karıştırılmaz**. Yazıcı ajanlar yalnızca buraya yazar; geliştirici ajanlar buradan **okuyabilir** ama yazmaz.

## Dizin

```text
research/
├── library/                       # peer-reviewed makale kütüphanesi (Demir Kural'ın merkezi)
│   ├── pdf/<bibkey>.pdf
│   ├── notes/<bibkey>.md
│   ├── bibliography.bib
│   ├── MANIFEST.csv               # otorite tablo
│   ├── ATTEMPTS.md                # indirilemeyenler (atıf yasak)
│   └── README.md
├── papers/                        # MİMARİ KİTAPLAR (Iglberger, Richards-Ford, Gregory, Akenine-Möller, vb.)
│   │                              # .gitignored — kullanıcı manuel koyar veya açık-erişimli indirilir
│   ├── README.md                  # kitap index'i
│   └── *.pdf / *.epub
├── reports/                       # denetim raporları
│   ├── verification_*.md          # citation-verifier
│   ├── independent_audit_*.md     # independent-auditor (BAĞIMSIZ)
│   ├── methodology_audit_*.md
│   ├── peer_review_*.md
│   ├── ethics_integrity_*.md
│   └── stats/                     # statistical-analyst script + sonuç
├── HUMAN_VOICE.md                 # AI-yazım engelleme kuralları (tüm yazıcılar zorlar)
└── README.md                      # bu dosya
```

> Not: DtForHil'deki `papers/` dizini "yazılan makaleler" içindi. CHROMODYNAMIC'te **mimari kitap koleksiyonu** anlamına gelir. Eğer ileride whitepaper/tez yazılırsa `research/whitepapers/<paper-id>/` yapısı (eski "papers" rolüyle) ayrıca açılır.

## Mühendislik tarafıyla ilişki

- **Tek yönlü okuma**: `research/` → kod tabanını okuyabilir (deney, ölçüm, doğrulama için).
- **Yasak**: Kod tabanı `Engine/` veya `include/chroma/` altına atıflı LaTeX, BibTeX, makale fragmanı bırakılmaz.
- **Köprü**: Sayısal sonuç gerekiyorsa `research/whitepapers/<paper-id>/scripts/` reproducible script çalıştırır, çıktısı `research/whitepapers/<paper-id>/tables/` veya `research/reports/stats/` altına gider.

## Ajan yazma yetkisi (özet)

| Ajan | Yazabildiği yer |
|------|-----------------|
| academic-researcher | `research/library/{pdf,notes,bibliography.bib,MANIFEST.csv,ATTEMPTS.md}` |
| citation-verifier | `research/reports/verification_*.md` |
| independent-auditor | `research/reports/independent_audit_*.md` |
| methodology-auditor | `research/reports/methodology_audit_*.md` |
| statistical-analyst | `research/reports/stats/` |
| latex-writer | `research/whitepapers/<paper-id>/{main.tex,sections/,README.md}` (opsiyonel mod) |
| figure-table-curator | `research/whitepapers/<paper-id>/{figures,tables,scripts}` (opsiyonel mod) |
| peer-review-simulator | `research/reports/peer_review_*.md` |
| ethics-integrity-reviewer | `research/reports/ethics_integrity_*.md` |
| reproducibility-engineer | `research/whitepapers/<paper-id>/{scripts,README.md}` (opsiyonel mod) |
| doc-writer (academic mode) | `research/**/*.md` (README, reproduction guide) |

Mühendislik ajanları (`developer`, `tester`, `build-devops`, `architect` vb.) bu dizine **yazmaz**, sadece okuyabilir. `architect` ise **ADR yazar** ama o `docs/ADR/` altındadır.

## Mimari kitap koleksiyonu — `papers/`

CHROMODYNAMIC tasarım kararlarında temel referanslar:

- **Iglberger** — *C++ Software Design: Design Principles and Patterns for High-Quality Software* (zorunlu)
- **Richards & Ford** — *Fundamentals of Software Architecture* (zorunlu)
- **Richards & Ford** — *Software Architecture: The Hard Parts* (zorunlu)
- **Gregory** — *Game Engine Architecture* 3rd ed. (önerilen, manuel ekle)
- **Akenine-Möller et al.** — *Real-Time Rendering* 4th ed. (önerilen, manuel ekle)
- **Nystrom** — *Game Programming Patterns* (açık erişimli, otomatik indirilir)
- **Fabian** — *Data-Oriented Design Book* (açık erişimli, otomatik indirilir)
- **Williams** — *C++ Concurrency in Action* (önerilen, manuel)
- **Scott** — *Professional CMake* (önerilen, manuel)

`papers/README.md` içinde tam kitap index'i tutulur (hangi konuda hangi kaynağa bakılır).

## Yeni whitepaper başlatma (opsiyonel mod)

Eğer engine süresince bir whitepaper/tez yazma kararı verilirse:

```bash
mkdir -p research/whitepapers/<paper-id>/{sections,figures,tables,scripts}
# sonra latex-writer'ı çağır, o iskelete `main.tex` + `sections/*.tex` yazar.
```

`<paper-id>` konvansiyonu: `YYYY-<venue-short>-<topic>` (ör. `2027-tog-chroma-rhi`).

## Demir Kural (özet)

> İndirilmemiş makale kullanılamaz. Bir referans atıf için uygun olmak için PDF + MANIFEST satırı + notes üçlüsü zorunludur. `research/library/README.md`'ye bak.

Engineering kaynakları (vendor docs, GitHub README, conference talk, blog) için Demir Kural geçerli **değil** — `researcher` ajanı link + erişim tarihi ile rapor üretir.

## AI-yazım engelleme (özet)

> Tüm yazıcı ajanlar `research/HUMAN_VOICE.md` kurallarına bağlıdır: yasak kelime listesi + cümle-uzunluk varyansı + insan sürtüşmesi. Yazım sonrası mekanik tarama zorunlu. `research/HUMAN_VOICE.md`'ye bak.
