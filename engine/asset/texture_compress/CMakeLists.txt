# engine/asset/texture_compress — cd::asset_texture_compress (Phase 650)
#
# Offline texture compression pipeline: compress raw RGBA8 pixels into
# GPU-native block-compressed formats for desktop (BC1/BC3/BC5/BC7) and
# mobile (ASTC 4×4 / 8×8).
#
# Sprint-1: API surface + BC1 stub encoder (naive min/max endpoint picker,
#           correct-shape 8-byte blocks, no transparent texels).
# Sprint-2: real BC1 PCA + BC3/BC5/BC7 via bc7enc + ASTC via astc-encoder
#           (vcpkg opt-in).
#
# Moment: game ships with 8 GB → 2 GB texture footprint; mobile build runs on
# devices without 8 GB VRAM.
#
# No external dependencies — Sprint-1 encoder is header + stdlib only.
cd_add_library(asset_texture_compress
  TYPE STATIC
  SOURCES
    src/TextureCompress.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
