# ADR-007 — Audio Architecture

- **Status**: Accepted (Phase 1 Design)
- **Date**: 2026-05-17
- **Related**: ADR-005 (Foundation), ADR-015 (Concurrency), ADR-016 (Vendor Matrix)

## Bağlam

`cd::audio` subsystem; library-oriented (engine'siz kullanılabilir), VR-grade latency hedef (<15 ms motion-to-sound; 256-sample @ 48 kHz buffer = 5.33 ms + driver). Vendor minimum prensibi (T5.Q2) ama device I/O custom yazımı 4-platform × audio expertise → Sprint 7'yi 4-6 hafta uzatır, ROI negatif.

## Karar

### A. Backend (T16.Q1) — Hybrid: vendor device I/O + custom mixer

**Vendor**: `miniaudio` (MIT, single-header) — K4 kategori, **Phase 3 replace hedefi** (4-6 ay native WASAPI/ALSA/CoreAudio/AAudio).

Vendor responsibility: device + format negotiation + resampling.
Custom responsibility: mixer + DSP graph + 3D spatial + effects.

```cpp
namespace cd::audio {
    class IDevice {  // stable interface
        virtual void start_callback(AudioCallback) = 0;
        virtual SampleFormat negotiated() const = 0;
    };
}
namespace cd::audio::vendor::miniaudio { class MaDevice : public IDevice {...}; }
namespace cd::audio::native::*    { /* Phase 3 native impl, IDevice swap */ }
```

### B. 3D Spatial (T16.Q2)

**Custom HRTF + Steam Audio opsiyonel plugin**:
- Core: Custom HRTF (MIT KEMAR dataset, FFT convolution via PFFFT vendor BSD veya KissFFT public-domain).
- Distance: Custom inverse-square + occlusion ray-cast (physics ADR-008 cross).
- Plugin: Steam Audio runtime DLL load (Apache 2.0, opsiyonel `CD_AUDIO_STEAM=ON`).
- Ambisonics: 1st-order Sprint 9; 3rd-order Sprint 12+ VR-pro.

**Reddedildi**: Resonance Audio (Google arşivledi 2024).

### C. DSP Graph (T16.Q3) — Hybrid bus mixer + node graph

İki katman:
- **Bus layer** (top-level): Master, Music, SFX, Voice, Ambient (data-driven; TOML config).
- **Node graph** (bus içinde): Source → Effects chain → Bus send (procedural authoring).

**Threading**: Dedicated audio actor (`cd::concurrency` ADR-015 actor model carve-out); SPSC lock-free ring buffer command queue main → actor. Audio callback driver thread (OS), allocation-free, **coroutine YOK** (real-time discipline).

### D. Codec (T16.Q4)

- **Opus** (libopus, BSD-3) — music + voice **tek codec**, 6-510 kbps range, < 26.5 ms algorithmic latency. Superior to Vorbis + MP3 every metric.
- **dr_wav** (PD, single-header) — uncompressed/raw assets, intro debug.
- **dr_flac** (PD) — lossless master (sound designer pipeline).

**Reddedildi**: Vorbis (Opus modern ikamesi); MP3 (patent free 2017 sonra ama hala suboptimal); AAC (Fraunhofer patent).

### E. Determinism Mode (Aşma noktası)

`cd::audio::Mixer::set_deterministic(seed)` — fixed timestep, sample-accurate scheduling, bit-exact replay. Wwise/FMOD'ta yok. Test + gameplay replay için killer feature.

### F. Audio LOD (Tsingos 2009)

Perceptual budget: 256 active source → top 32 binaural rendered, gerisi stereo bus mix. Mobile/Switch için kritik.

### G. HRTF Runtime-Swappable

User custom HRTF profile (Sony 360 Reality Audio benzeri ear scan). Wwise/FMOD'ta dataset sabit.

### H. Library-Oriented Standalone

`cd::audio` engine'den bağımsız `#include <cd/audio.hpp>` + CMake target. Standalone audio app yazılabilir (DAW plugin host bile).

## Reddedilen

- **Native per-platform custom backend (Phase 1)**: 4 platform × audio expertise; Sprint 7'yi 4-6 hafta uzatır; **Phase 3 hedefi olarak ertelendi**.
- **FMOD/Wwise plugin**: library-oriented kırılır, royalty/lisans riski, vendor lock.
- **Resonance Audio**: Google arşivledi 2024, maintenance riski.
- **Audio callback'te coroutine**: real-time thread allocation/suspension yasak, latency riski.
- **Tek codec MP3/Vorbis**: Opus üstün.

## Sonuçlar

**Pozitif**: düşük dep footprint (3 single-header lib), VR latency hedefi karşılanır, standalone library kullanım mümkün, determinism unique selling point.

**Negatif**: custom HRTF maintenance (~2000 LOC + DSP expertise); Steam Audio opsiyonel test matrix; miniaudio mobile (iOS) edge case'leri gelecek riski.

**Replace-Ready (D1)**: `IDevice` interface, gelecek native WASAPI/CoreAudio impl için açık kapı; Steam Audio runtime-load build-time bağımlılık yok.

## Açık Sorular

| ID | Soru | Çözüm |
|---|---|---|
| Q1 | FFT backend: KissFFT (PD basit) vs PFFFT (BSD) vs FFTW (GPL) | PFFFT eğilim, Sprint 7 benchmark |
| Q2 | Ambisonic order: 1st (4ch ucuz) Sprint 9 vs 3rd (16ch) Sprint 12+ | 1st default; 3rd opt-in VR-pro mode |
| Q3 | Voice chat: libopus + WebRTC AEC? | ADR-011 (networking) cross-cut |
| Q4 | Console (PS5 Tempest, Xbox Spatial) — Sprint? | Phase 2+ NDA SDK |
| Q5 | Determinism mode OS audio clock drift handle? | Fixed buffer + silence padding fallback |
| Q6 | Asset bank format: Wwise SoundBank-vari binary mı C++ DSL? | ADR-006 cross-cut, binary bank |
| Q7 | Web/WASM target: Web Audio API binding? | miniaudio web backend, latency 25ms+ kabul |

## Cross-Cutting

- **ADR-005 (Foundation)**: tri-clock (real/game/hires) timestamp; tracking allocator audio thread tag.
- **ADR-015 (Concurrency)**: `cd::concurrency::actor::AudioMixer` dedicated thread, RT priority (`AvSetMmThreadCharacteristics("Pro Audio")` Win, SCHED_FIFO Linux, THREAD_TIME_CONSTRAINT_POLICY macOS). SPSC ring buffer command queue.
- **ADR-006 (Asset)**: audio asset streaming ring buffer (decoder thread, 2-3 chunk lookahead); master `.flac` → runtime `.opus`; hot-reload audio actor swap source with 5 ms crossfade.

## Kanıt

- miniaudio: github.com/mackron/miniaudio (MIT/PD)
- libopus: opus-codec.org (BSD)
- Steam Audio: valvesoftware.github.io/steam-audio/ (Apache 2.0)
- Tsingos (2009) — Pre-computed acoustics for games — **STUB**
- Begault (1994) — 3-D Sound for VR and Multimedia, HRTF foundational — **STUB**
- Valimaki et al. (2012) — Fifty years of artificial reverberation IEEE — **STUB**
- Zotter & Frank (2019) — Ambisonics: A Practical 3D Audio Theory — **STUB**
