# cd::audio::dsp_fx

Four digital-signal-processing primitives for the audio mixer's
post-effect chain:

| Primitive       | Role                                                  |
|-----------------|-------------------------------------------------------|
| `BiquadCoeffs`  | Raw second-order IIR coefficient bundle.              |
| `LowPass`       | Biquad low-pass filter (direct-form II transposed).   |
| `HighPass`      | Biquad high-pass filter (same topology).              |
| `DelayLine`     | Integer-sample delay with read/write API.             |
| `Reverb`        | Sprint-1 stub: passthrough. Sprint-2: Schroeder comb-allpass network using `DelayLine`. |

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
    void                           process(std::span<float> samples);  // sprint-1: passthrough
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

## Sprint-2 reverb plan

Schroeder reverb network: four comb filters in parallel followed by
two allpass filters in series, all driven by `DelayLine` instances.
Sized for typical room characteristic times (RT60 ∈ [0.5 s, 4 s]).

## Dependencies

* `cd::core` — `Defines.hpp`.
* `cd::audio` — sample format vocabulary (PUBLIC; the `.cpp` only
  uses `float`, but the header takes `std::span<float>` which
  matches `cd::audio`'s mixer-side buffer type).
