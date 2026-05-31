# ADR-20260601 Audio DSP FX — cd::audio::dsp_fx Sprint-1

**Status:** Accepted  
**Date:** 2026-06-01  
**Phase:** 562

---

## Bağlam

CHROMODYNAMIC's audio tier (`cd::audio`) provides backend abstraction, mixer, voice management, and basic one-pole filters (Phase 65.A `LowPass`, Phase 29.D `SimpleReverb`). As the engine moves toward a production-quality audio pipeline, audio processing primitives need a dedicated library with:

1. A reusable **biquad coefficient bundle** (`BiquadCoeffs`) that can be populated by multiple filter types and consumed by future SIMD-accelerated processing loops.
2. A **second-order low-pass filter** and **high-pass filter** based on the Audio EQ Cookbook bilinear-transform formulae, replacing the first-order exponential-average `LowPass` in `cd::audio` for any application requiring steeper roll-off or proper frequency-domain specification.
3. A **general-purpose delay line** with configurable maximum depth, needed by reverb, chorus, flanger, and any time-domain effect.
4. A **reverb stub** that establishes the API contract so call sites compile today, with the real Schroeder comb-filter + allpass network deferred to Sprint-2.

The existing `cd::audio` library is the right home for backend integration; however, DSP processing nodes have no backend dependency and belong in a separate, independently testable library. This mirrors the DAG principle in CLAUDE.md §7: downstream consumers (editor, sample apps, tools) should link only the DSP primitives without pulling in platform audio backend code (WASAPI, CoreAudio, ALSA).

---

## Karar

### Library boundary

A new library `cd_audio_dsp_fx` is created at `engine/world/audio_dsp_fx/`. It depends on `cd::core` (for `Defines.hpp`) and `cd::audio` (for potential future integration with `cd::audio::DspGraph`). All public API lives in a single header `include/cd/audio/dsp_fx/DspFx.hpp`; the implementation TU `src/DspFx.cpp` is minimal for Sprint-1 but provides the mandatory non-empty object file and will grow with Sprint-2 SIMD internals.

Namespace: `cd::audio::dsp_fx`.

### Filter design: second-order biquad (IIR, direct-form II transposed)

Sprint-1 implements second-order Butterworth-style low-pass and high-pass filters using the coefficient formulae from:

> **[unverified]** Robert Bristow-Johnson, "Cookbook formulae for audio EQ biquad filter coefficients," Audio EQ Cookbook (widely distributed public-domain reference, various revisions).  
> **PDF + BibTeX entry pending.** `research/library/MANIFEST.csv` does not yet contain a verified entry for this document. An academic-researcher pass is queued before this citation may be treated as verified under the Demir Kural.

The Cookbook LPF/HPF formulae derive from the bilinear transform of a second-order analog prototype:

```
w0    = 2*pi*f0/Fs
alpha = sin(w0) / (2*Q)         (Q = 1/sqrt(2) for Butterworth maximally flat response)

LPF:
  b0 = (1 - cos(w0)) / 2
  b1 =  1 - cos(w0)
  b2 = (1 - cos(w0)) / 2
  a0 =  1 + alpha
  a1 = -2 * cos(w0)
  a2 =  1 - alpha

HPF:
  b0 =  (1 + cos(w0)) / 2
  b1 = -(1 + cos(w0))
  b2 =  (1 + cos(w0)) / 2
  a0 =  1 + alpha
  a1 = -2 * cos(w0)
  a2 =  1 - alpha
```

All coefficients are normalised by `a0` before storage in `BiquadCoeffs`. Direct-form II transposed is chosen over direct-form I for better numerical behaviour at low cutoff-to-sample-rate ratios (smaller intermediate state values).

### DelayLine

A circular buffer of `std::vector<float>` sized at `configure()` time. `write()` advances the head; `read(n)` returns the sample `n` positions before the current write head. `samples_back = 1` returns the most recently written sample. This is the canonical primitive for all time-domain effects (comb filter, chorus, etc.) and will be used directly by the Sprint-2 Schroeder FDN.

### Reverb Sprint-1 stub

Sprint-1: `configure()` stores a `Config` struct; `process()` copies input span to output span verbatim (passthrough). This establishes the call-site API so that any code depending on `cd::audio::dsp_fx::Reverb` compiles and runs correctly during the sprint gap.

### Sprint-2 plan

Sprint-2 will replace the passthrough `Reverb` with a Schroeder network:

- 4 parallel comb filters (feedback delay network) with tunable delay lengths and a per-comb one-pole damping LPF.
- 2 allpass filters in series for diffusion.
- Delay lengths derived from `Config::room_size` (scale factor on a fixed prime-ratio table).
- Wet/dry mix applied as a final linear blend.
- All delay lines reuse `DelayLine` instances.

### Reddedilen alternatifler

| Alternative | Reason rejected |
|---|---|
| Extend existing `cd::audio` with the new classes | Violates library boundary: backend consumers (editor audio preview) would pull in DSP headers and vice versa. Unit test isolation would be harder. |
| Header-only library (no `.cpp` TU) | CMake STATIC library requires at least one object file; an empty TU would be flagged by some linkers. More importantly, Sprint-2 SIMD internals must not be in headers (ODR, compile time). |
| Use `cd::audio::LowPass` (first-order EMA) everywhere | First-order filter has -20 dB/decade roll-off; second-order gives -40 dB/decade, which is standard for audio equaliser bands. The Cookbook formulae are the industry baseline. |
| FDN reverb in Sprint-1 | Multi-comb FDN requires tuned delay lengths, damping tables, and a stereo cross-feed implementation. Shipping a stub first gives call sites a stable API without blocking the Sprint-1 delivery. |

---

## Sonuçlar

- New STATIC library `cd_audio_dsp_fx` (target `cd::audio_dsp_fx`) in `engine/world/audio_dsp_fx/`.
- Public API: `BiquadCoeffs`, `BiquadState`, `LowPass`, `HighPass`, `DelayLine`, `Reverb` (stub).
- 9 tests shipped (Sprint-1), all PASS.
- Sprint-2 will land the Schroeder FDN reverb; the stub is a forward-compatible placeholder.
- Bristow-Johnson Cookbook citation is **[unverified]** until `research/library/MANIFEST.csv` entry + BibTeX entry are added by the academic-researcher agent.
