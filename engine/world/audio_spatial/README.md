# cd::audio::spatial

3D positional audio sidecar to `cd::audio`. The 2D mixer / playback
backend stays in `cd::audio`; **where** sounds come from is the
concern of this library.

| Library             | Concern                                           |
|---------------------|---------------------------------------------------|
| `cd::audio`         | 2D mixer / playback (WASAPI / CoreAudio / ALSA).  |
| `cd::audio::spatial`| ITD / ILD / Doppler / HRTF (positional math).     |

## Sprints

| Sprint | Phase | Surface |
|--------|-------|---------|
| 1 | `phase693` | `SpatialMixer` — ITD/ILD panning + 1/r distance attenuation + Doppler. |
| 2 | `phase751` | `HrtfMixer` — real HRTF convolution via OpenAL Soft. |

## OpenAL Soft dependency

3-tier resolution (mirrors `cd::asset::save_compression` lz4 / etc.):

```
Tier 1: vcpkg     find_package(OpenAL CONFIG)         — canonical CI path
Tier 2: FetchContent  github.com/kcat/openal-soft tag 1.24.2
Tier 3: absent       CD_AUDIO_SPATIAL_HAS_OPENAL=0    — HrtfMixer compiles
                                                       as no-op stub,
                                                       SpatialMixer (Sprint-1)
                                                       remains operative.
```

**LGPL note**: openal-soft is LGPL-2.0-or-later. Linked as a SHARED
library (vcpkg default on Windows) so CHROMODYNAMIC stays
LGPL-clean — link-time-only relationship satisfies LGPL. See
`docs/ADR/ADR-20260605-openal-soft-lgpl.md`.

## Sprint-1 surface

```cpp
namespace cd::audio::spatial {

struct Listener
{
    cd::math::Vec3f position;
    cd::math::Vec3f velocity;
    cd::math::Vec3f forward;
    cd::math::Vec3f up;
};

struct Source
{
    cd::math::Vec3f position;
    cd::math::Vec3f velocity;
    float           gain;
    float           rolloff_metres { 5.0F };    // 1/r reference distance
};

struct PanResult
{
    float gain_l, gain_r;       // stereo gains in [0, 1]
    float doppler_pitch;        // multiplier on source pitch
    float distance_attenuation; // 1/r derived
};

class SpatialMixer
{
public:
    void                          set_listener(Listener);
    [[nodiscard]] PanResult       mix_source(Source) const;
};

}
```

## Sprint-2 HRTF surface

```cpp
namespace cd::audio::spatial {

#if CD_AUDIO_SPATIAL_HAS_OPENAL
class HrtfMixer
{
public:
    [[nodiscard]] static cd::expected<HrtfMixer, Error>
                                  create(/* device + HRTF profile config */);

    void                          set_listener(Listener);
    void                          enqueue_source(Source, std::span<const int16_t> pcm);

    // Render N frames of binaural stereo into out_lr (interleaved).
    void                          render(std::span<float> out_lr_interleaved);
};
#endif

}
```

The HRTF profile is queried from the OpenAL Soft default device set
(`hrtf-default.mhr` etc.). User-overridable through a config path
at `HrtfMixer::create`.

## Doppler model

```
  doppler_pitch = (c + listener_v · n) / (c + source_v · n)
  where  n = unit vector from source to listener
         c = speed of sound = 343 m/s (configurable in SpatialMixer)
```

Same physics as the OpenAL specification — no relativistic correction;
caller can scale `c` if they want exaggerated cinema-style Doppler.

## Dependencies

* `cd::core` — `Defines.hpp`.
* `cd::audio` — sample-format vocabulary (PUBLIC).
* `OpenAL::OpenAL` — Sprint-2 PRIVATE link, **optional** (see
  3-tier resolution table above).
