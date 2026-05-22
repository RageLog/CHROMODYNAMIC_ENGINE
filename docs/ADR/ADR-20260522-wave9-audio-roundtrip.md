# ADR-20260522 — Wave 9: audio synth → WAV → asset_wav round-trip sample

## Bağlam

Wave 4'te `cd::asset_wav` library'sini ekledik ama hiç bir sample bunu
disk üzerinden test etmiyordu — sadece in-memory `decode()` unit
testleri vardı. Bu, library'nin disk I/O kodyolu (`std::ifstream`
ate-seek-read) hakkında "headless smoke harness'ta da geçer" güvenini
sağlamıyordu.

Wave 9 (~13:20–13:25) bu boşluğu kapatır: `hello_audio_synth`.

## Karar

Tek sample, headless:

1. Sinüs sentez (440 Hz, mono s16, 44.1 kHz, 0.25 s → 11025 frame)
2. RIFF/WAVE byte stream construct (44-byte header + samples)
3. `std::ofstream` ile diske yaz
4. `cd::asset_wav::load(path)` → `Wav`
5. Round-trip assert: channels=1, sample_rate=44100, bits=16, frame
   count match, payload byte-identical via `std::memcmp`

Smoke-harness `--headless` flag'i parse etmez (cd::sample::Runtime'a
dependency yok); sample doğal olarak ~50 ms'de exit eder. Smoke
harness'ta bu yeterli.

## Reddedilen alternatifler

- **Sample içine WAV encoder kütüphanesi ekle:** dr_wav tarzı. Hand-roll
  WAV encoder zaten 30 satır; vendoring overkill.
- **Platform output (WASAPI / CoreAudio / ALSA):** v1'de audio sürmek
  istemiyoruz. `cd::audio` library var ama platform output entegrasyonu
  ayrı bir sprint. Round-trip "decode doğru çalışıyor mu" sorusunu
  cevaplamak için yeterli.
- **Test-only fixture (cd_test_asset_wav'a ekle):** Ayrı bir sample
  daha demonstratif — README'deki sample listesinde "audio loop var"
  sinyali veriyor.

## Sonuçlar

| Metric | Wave 8 sonu | Wave 9 sonu |
|---|---|---|
| Engine libraries | 48 | **48** |
| Samples | 21 | **22** (+hello_audio_synth) |
| ctest binaries | 50 | **50** |
| Headless smoke-clean | 21/21 | **22/22** |
| ADRs (this date) | 24 | **25** |

`cd::asset_wav` artık production-confidence: unit-test + sample-loop +
smoke-harness her dalgada otomatik koşuluyor.

## Açık sorular

- **`hello_audio_synth` disk artifact temizliği:** Sample `hello_audio_synth_out.wav`
  dosyasını çalışma dizinine bırakıyor. Test fixture pattern'ı (`testing::TempDir()`)
  unit-test'lerde uygulandı; sample-side TempDir eşdeğeri henüz yok.
  v2'de basit bir `cd::sample::tmpdir()` helper'ı ile temizlenebilir.
- **`cd::audio::WavBuffer` adapter:** `cd::asset_wav::Wav` → `cd::audio`'nun
  voice/mixer input format'ına dönüşüm. `cd::audio` v2.
- **Float32 sample round-trip:** Şu an sadece s16 sample synthesize
  ediyoruz. fmt code 3 (IEEE float) için ek sample değer ekleyebilir
  (test'lerde zaten cover ediliyor, sample-side overkill).
