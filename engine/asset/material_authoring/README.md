# cd::asset::material_authoring

Asset-tier material authoring helper. Designers hand-edit
`.material.json` files; the engine **loads + validates** them before
the asset enters the render pipeline. Catches the typo
`"roughness": 1.5` or a missing albedo-texture path **before** the
asset reaches the GPU; the hot-reload pipeline surfaces the diagnostic
in real time.

**Not** a v2 of `cd::material`. This sits one tier above —
`cd::material` is the runtime BRDF / parameter layer; this library is
the **authoring + validation** layer that produces it. The fields
map one-to-one: `alpha_mode` / `alpha_cutoff` static_cast directly
to `cd::material::AlphaMode` / `cd::material::AlphaParams`.

## Public surface

```cpp
namespace cd::asset::material_authoring {

struct AuthoredMaterial
{
    std::string                    id;
    std::array<float, 3>           albedo_factor;
    float                          metallic;
    float                          roughness;
    float                          normal_strength;
    cd::material::AlphaMode        alpha_mode;
    float                          alpha_cutoff;
    std::filesystem::path          albedo_path;
    std::filesystem::path          normal_path;
    std::filesystem::path          metallic_roughness_path;
};

cd::expected<void, Error>             save_to_json(const AuthoredMaterial&,
                                                   const std::filesystem::path&);
cd::expected<AuthoredMaterial, Error> load_from_json(const std::filesystem::path&);
cd::expected<void, Error>             validate_authored(const AuthoredMaterial&);

namespace AuthoringDefaults
{
    AuthoredMaterial dielectric();   // r=0.5  m=0.0  factory preset
    AuthoredMaterial metal();        // r=0.2  m=1.0
    AuthoredMaterial cloth();        // r=0.8  m=0.0
}

}
```

## Validation rules

`validate_authored()` checks:

* `id` is non-empty.
* `metallic ∈ [0, 1]`, `roughness ∈ [0.04, 1]` (sub-0.04 roughness
  collapses BRDF NDF singularities; matches the W7 convention).
* `normal_strength ∈ [0, 5]`.
* `alpha_cutoff ∈ [0, 1]`.
* Albedo factor channels each `∈ [0, 1]`.
* If a texture path is set, it must be relative (asset-root anchored)
  and end in a known extension (`.png` / `.ktx2` / `.cdtex`).

Returns `Error{ kValidationError, <human-readable message> }` on
first failure — `validate_authored` is "first-fail" by design; the
hot-reload UI surfaces one diagnostic at a time.

## Error domain

```cpp
namespace authoring_errors
{
inline constexpr std::uint32_t kDomain = 0x001F;
enum class Code : std::uint32_t
{
    kOk              = 0,
    kFileNotFound    = 1,
    kIoError         = 2,
    kParseError      = 3,
    kMissingField    = 4,
    kValidationError = 5,
};
}
```

## Dependencies

* `cd::core` — `Defines.hpp`, `expected`, error infrastructure.
* `cd::material` — `AlphaMode` enum (header-only include for the
  shared type vocabulary; no link to BRDF eval).
* `cd::asset_json` — `.material.json` (de)serialisation.

`EXCLUDE_FROM_INSTALL` because `cd::material` transitively is. Not a
runtime library — only the authoring tools and the editor hot-reload
panel link to it.
