# engine/texture_synth — cd::texture_synth
#
# Procedural texture generators: 2D value-noise hash, fBm composition,
# Earth-like RGBA albedo, height-derivative normal maps,
# metallic-roughness-AO packs. Header-only.

cd_add_library(texture_synth
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
    cd::math
)
