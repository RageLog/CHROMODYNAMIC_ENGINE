# engine/asset/validator — cd::asset_validator (Phase 611)
#
# Pre-load asset validation: catch malformed, oversized, or missing assets
# BEFORE they enter the render pipeline.
#
# Sprint-1: magic-header checks, non-empty blob, size-limit policy for
#           texture (PNG/JPEG/KTX2/DDS/HDR), glTF 2.0 (JSON + GLB), and
#           audio (WAV/OGG) blobs.
# Sprint-2: deep semantic validation (mip chains, mesh topology, etc.).
cd_add_library(asset_validator
  TYPE STATIC
  SOURCES
    src/Validator.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
