# cd::audio::dsp_fx

Four digital-signal-processing primitives for the audio mixer's
post-effect chain:

| Primitive       | Role                                                  |
|-----------------|-------------------------------------------------------|
| `BiquadCoeffs`  | Raw second-order IIR coefficient bundle.              |
| `LowPass`       | Biquad low-pass filter (direct-form II transposed).   |
| `HighPass`      | Biquad high-pass filter (same topology).              |
| `DelayLine`     | Integer-sample delay with read/write API.             |
| `Reverb`        | Real scalar Schroeder/Freeverb FDN — 4 parallel LBCF combs + 2 series Schroeder allpasses over `DelayLine`. |

All classes are **mono**, sample-by-sample or span-based. The caller
iterates over channels for stereo / surround. Thread-safety: none —
caller must serialise if needed.

## Filter topology

The biquad path uses **direct-form II transposed** for numerical
stability under floating-point arithmetic:

```
  H(z) = (b0 + b1*z⁻¹ + b2*z⁻²)
        / (1  + a1*z⁻¹ + a2*z⁻²)
```

`a0` is normalised to 1 before storage; `a1, a2` are already divided
by the raw `a0` returned by the cookbook formulae. Internal state is
two `float` registers (`z1, z2`) per filter instance — minimal cache
footprint, predictable cost per sample.

## Public surface

```cpp
namespace cd::audio::dsp_fx {

struct BiquadCoeffs
{
    float b0, b1, b2;
    float a1, a2;
};

class LowPass
{
public:
    static BiquadCoeffs            design(float sample_rate_hz,
                                          float cutoff_hz, float q);
    void                           reset();
    [[nodiscard]] float            process(float sample);
    void                           process(std::span<float> samples);
};

class HighPass { /* same shape as LowPass */ };

class DelayLine
{
public:
    explicit DelayLine(std::size_t capacity_samples);
    void                           write(float sample);
    [[nodiscard]] float            read(std::size_t delay_samples) const;
    void                           reset();
};

class Reverb
{
public:
    void  configure(const Config& cfg);   // room_size / damping / wet_dry_mix / sample_rate
    void  process(std::span<const float> in, std::span<float> out);  // real FDN
    void  reset();
};

}
```

## Coefficient formulae

`LowPass::design` / `HighPass::design` follow the standard audio EQ
cookbook formulae — `BiquadCoeffs` populated via the well-known
`alpha = sin(w0) / (2*Q); cos_w0 = cos(w0); ...` pattern.

> **Citation status (Demir Kural):** the formula reference is
> [unverified] Robert Bristow-Johnson, *Cookbook formulae for audio
> EQ biquad filter coefficients* (Audio EQ Cookbook, web/public
> domain). PDF + BibTeX entry pending; `research/library/MANIFEST.csv`
> does not yet contain a verified entry. An `academic-researcher`
> pass is queued before this citation may be treated as verified.

## Reverb network (real scalar FDN)

Schroeder/Freeverb network, fully implemented in `DspFx.cpp`:

* **4 parallel Low-pass Feedback Comb Filters (LBCF).** Each is a delay line
  with a one-pole low-pass in its feedback path (the Freeverb "filterstore",
  driven by `damping`). Delay lengths are mutually prime (snapped to the next
  prime) to scatter echo coincidences; `room_size` scales both the delays and
  the RT60.
* **2 series Schroeder allpass diffusers.** True allpass form
  `H(z) = (-g + z^-N)/(1 - g·z^-N)` — unity magnitude at every frequency, so
  they smear echo density without colouring the spectrum.
* **RT60-derived feedback gain** `g = 10^(-3·N/(RT60·Fs))`, clamped `< 1`, so
  the impulse envelope falls 60 dB over the target time and the network is
  provably BIBO-stable (no blow-up).
* **Denormal flush** in the comb filterstore prevents the subnormal-FPU stall
  on long silent tails; it is audibly inert (~150 dB below unity).

`room_size ∈ [0,1]` maps to RT60 ≈ `0.1 + 2·room_size` s and a delay scale of
`0.5 + room_size`. SIMD acceleration of the filter bank remains a deferred
perf promote-on-need (ADR-20260616-band3-world-scope §2.3); the scalar path
above is the correctness contract any future SIMD bank must reproduce.

## Dependencies

* `cd::core` — `Defines.hpp`.
* `cd::audio` — sample format vocabulary (PUBLIC; the `.cpp` only
  uses `float`, but the header takes `std::span<float>` which
  matches `cd::audio`'s mixer-side buffer type).
