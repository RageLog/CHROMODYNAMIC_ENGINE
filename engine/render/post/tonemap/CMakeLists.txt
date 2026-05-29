# engine/render/post_tonemap — cd::post_tonemap (Day 13)
# State-of-the-art tonemap operators (Narkowicz / Hill ACES / Hable / AGX)
# as header-only CPU functions + matching GLSL strings.
cd_add_library(post_tonemap
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
    cd::math
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
