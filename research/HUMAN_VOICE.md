# HUMAN_VOICE — AI-Yazım Önleme Kuralları (zorunlu, tüm yazıcı ajanlar için)

> Bu dosyaya `latex-writer`, `doc-writer` (akademik mod), `peer-review-simulator` (kendi raporu için),
> `academic-researcher` (özet notu için), `ethics-integrity-reviewer` (rapor için) bağlıdır.
> Yazımdan sonra mekanik tarama (aşağıdaki "Post-write check") zorunludur — başarısız ise yeniden yaz.

## Neden

Q1 dergi reviewer'ları, plagiarism tool'ları (Turnitin), AI-content detector'lar (GPTZero, Originality.ai, Copyleaks) artık AI-yazımı agresif şekilde tespit ediyor. AI-flag bir makaleyi düşürmeye yeter. Bunun ötesinde — AI-yazımı **akademik açıdan kötü** çünkü konkret detay yerine genel klişeyle doludur.

Hedef: insan akademisyenin yazdığı — sayıya, kanıta, somut deneyime tutunan — metin.

## Yasak Kelime / Öbek Listesi (kara liste)

İngilizce yazımda **kullanma**:

```text
delve, delve into
navigate, navigating
tapestry
realm, in the realm of
ever-evolving, ever-growing, ever-changing
embark, embark on
harness, harnessing
leverage (verb), leveraging
journey
landscape (metafor olarak)
pivotal, play a pivotal role
underscore, underscoring
moreover
furthermore
additionally
besides this
in conclusion
to summarize, in summary
it is important to note that
it should be noted that
it is worth noting that
it's worth mentioning
robust (sayısal kanıt olmadan)
seamless, seamlessly
cutting-edge (sayısal kanıt olmadan)
state-of-the-art (sayı + kıyaslama olmadan)
groundbreaking
revolutionize
unlock, unleashing
foster
elevate
testament to
nuanced (boş süs olarak)
intricate (boş süs olarak)
comprehensive (sayısal kapsam olmadan)
holistic
multifaceted
paradigm shift
synergize, synergy
```

Türkçe yazımda **kullanma**:

```text
keşfetmek (metafor olarak)
yolculuğa çıkmak
manzara (metafor olarak)
sürekli gelişen
sancak yapmak
köprü kurmak (klişe)
güçlü (sayısal kanıt olmadan)
sağlam (sayısal kanıt olmadan)
kapsamlı (sayısal kapsam olmadan)
özetle, özet olarak
sonuç olarak (final başlık dışında)
bunun yanı sıra
ayrıca, dahası, bununla birlikte (cümle başında üst üste)
şunu belirtmek gerekir ki
şunu vurgulamak önemlidir ki
göz önünde bulundurulduğunda (cümle başında)
```

## Cümle-Uzunluk Varyansı

- Her paragrafta cümleler arasında 4× varyans olmalı (en uzun / en kısa ≥ 3).
- AI tipik: tüm cümleler 18–22 kelime → düz tonus, monoton ritim. Sen bunu kır.
- Hedef dağılım: %25 kısa (≤ 10 kelime), %50 orta (11–22), %25 uzun (23–35).
- 36+ kelimelik cümle → büyük olasılıkla bölünmeli.

## Cümle-Yapısı Çeşitliliği

- Üst üste **3 cümle aynı kalıpla başlamasın** (örn. "We trained... We evaluated... We compared..." → en az birini "Two key choices shaped the protocol: ..." gibi tek-cümlelik açılışa çevir).
- Pasif/aktif denge: Methods → ağırlıklı aktif (we); Results → ağırlıklı pasif kabul ama monoton olmasın.
- Subordinate clause kullan ("Because the leakage check failed in fold 2, we re-split...").
- Yer yer kısa direkt cümle ("Fold 2 leaked. We re-split.") — bir paragrafta 1 tane yeter, abartma.

## Burstiness (cümle uzunlukları arasında zıplama)

İki uzun cümle arasına bir kısa, sonra orta, sonra uzun. Bu **insan ritmi**. AI rölantide aynı uzunlukta seyreder.

Örnek (kötü, AI):

> The system was trained using a learning rate of 0.001. The system was evaluated using accuracy and F1. The system outperformed the baseline by 2.3%.

Örnek (iyi, insan):

> We trained the system with a learning rate of 0.001, chosen after a coarse 1e-2 / 1e-3 / 1e-4 sweep on the validation split. Accuracy and macro-F1 were the primary metrics; sensitivity entered as a tie-breaker. The gap over Baseline-X reached 2.3 percentage points and held across all five seeds.

## İnsan Sürtüşmesi (Honest Friction)

Gerçek akademisyenler sürtüşmeyi gizlemez:

- "Initially we used X, which failed because Y; we then switched to Z."
- "This result was somewhat unexpected: Z normally performs worse."
- "We were unable to reproduce the published 91.2% — our re-run yielded 89.6 ± 0.4%, and we report that figure."

Bu cümleler AI-detector için **insan** sinyali, reviewer için **dürüstlük** sinyali. Limit bölümünde özellikle kıymetli.

## Domain-Spesifik Kelime Hazinesi

DtForHil HIL/radar domeni: "track-update rate", "range-Doppler", "clutter suppression", "hardware-in-the-loop loopback", "real-time deadline", "scenario replay", "RCS", "PRF". Bu kelimelerin doğal yoğunluğu insan-yazımı işaretidir. AI genellikle bu terimleri kullanmadan etrafından dolaşır.

## Cümle-Başı Bağlaç Yasağı (akademik İngilizce)

Üst üste **2'den fazla** cümle "However," "Therefore," "Moreover," "Furthermore," "Additionally," "Indeed," ile başlamasın. Bağlacın kendisini de seyret: "However" cümle başında en fazla 1 paragrafta 1.

## Sayısal Kesinlik — Hedge'i Abartma

- "may", "could", "appears to" gibi hedge'ler ölçülü; özellikle Results'ta sonuç anlatırken sayı + CI ver, hedge'i Discussion'a sakla.
- AI fazla hedge eder ("could potentially serve as a robust approach"). İnsan: "reduced false-alarm rate by 18% (95% CI: 14–22%)".

## Atıf Tabanlılığı

Her teknik iddia ya bizim sonuç tablomuza dayanmalı ya da `\cite{key}` taşımalı. AI sıklıkla atıfsız genel iddia yapar ("This is a well-known technique"). İnsan: ya kaynak, ya kendi sayımız.

## Em-dash / En-dash / Üç Nokta

- **Em-dash YASAK (kesin)**: `---` (LaTeX) ve `—` (Unicode U+2014) **hiç kullanma**.
  AI-detector'lar (GPTZero, Originality.ai, Copyleaks) em-dash'i en güçlü AI tell-tale
  signal olarak değerlendirir. Yerine: virgül, iki nokta, parantez, nokta-nokta
  başlangıçlı yeni cümle.
- En-dash (`--`): yalnızca sayfa/sayı aralığı için (`pp.~221--225`, `5--10 saniye`).
  Cümle içinde ayraç olarak kullanma.
- "..." (üç nokta): akademik metinde **kullanılmaz** (alıntıda kesilmiş kısım hariç).

## Post-write Mekanik Tarama (yazımdan sonra zorunlu)

Her yazıcı ajan, yazımdan sonra şu komutları koşmadan rapor üretmez:

```bash
# 1) Yasak kelime taraması (en/tr)
WORDS=("delve" "tapestry" "realm" "ever-evolving" "harness" "leverage" "pivotal" "moreover" "furthermore" "additionally" "in conclusion" "it is important to note" "it should be noted" "robust" "seamless" "cutting-edge" "state-of-the-art" "groundbreaking" "revolutionize" "unlock" "foster" "elevate" "comprehensive" "holistic" "multifaceted" "synergy" "keşfet" "yolculuğa çıkmak" "manzara" "sürekli gelişen" "köprü kurmak" "kapsamlı" "özetle" "şunu belirtmek gerekir ki")
for w in "${WORDS[@]}"; do
  grep -niF "$w" research/papers/<paper-id>/sections/*.tex 2>/dev/null
done
# Sonuç varsa → bu satırları yeniden yaz, sonra tekrar tara.

# 2) Cümle uzunluk varyansı
python -c "
import re, sys, statistics
text = open(sys.argv[1]).read()
sents = [s for s in re.split(r'(?<=[.!?])\s+', re.sub(r'%.*','',text)) if len(s.strip()) > 5]
lens = [len(s.split()) for s in sents]
if not lens: sys.exit()
mn, mx, md, sd = min(lens), max(lens), statistics.median(lens), statistics.pstdev(lens)
print(f'n={len(lens)} min={mn} max={mx} median={md} stdev={sd:.1f}')
print('AI risk' if (mx/max(mn,1) < 3 or sd < 4) else 'OK')
" research/papers/<paper-id>/sections/methods.tex

# 3) Cümle-başı bağlaç sayımı
grep -cE '^(However|Moreover|Furthermore|Additionally|Therefore|Indeed),' research/papers/<paper-id>/sections/*.tex
# Toplam > 2 ise dağılımı düzelt
```

Çıktıda 0 yasak kelime + uzunluk varyansı OK + bağlaç ≤ 2 → yazım kabul. Aksi halde **yeniden yaz**.

## "Zaten İnsan Yazmış" Sinyalleri (üret bunları)

- Konkret deney sayıları: "5 seed × 5 fold = 25 koşu, toplam ~14 GPU-saat"
- Spesifik hata: "İlk koşuda config dosyasında batch_size 64 yerine 32 kalmıştı; tabloyu güncelledik."
- Yerel atıf: "Tablo II'nin son sütunu"
- Karşı argümana yer: "Bu sonuç X et al.'nin gözleminden farklı; olası neden..."
- Yumuşak limit: "We did not test on Dataset-Z; that comparison is left to future work."

## Yapıcı Kapanış

AI-detector geçmek tek başına amaç değil. Asıl amaç: **reviewer'ı ikna eden, somut, dürüst yazım**. Bu kuralları takip ettiğinde detector'lar zaten geçer; daha önemlisi reviewer puanın yükselir.
