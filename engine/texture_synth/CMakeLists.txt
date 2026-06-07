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

# phase898-noise-tests: regression net for the value_noise2 +
# value_noise2_quintic + fbm helpers. Locks deterministic output
# at known sample points so a future refactor can't silently
# change the noise distribution.
if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
