# CDCompilerDetection.cmake — Compile-time feature probe
# ADR-005 §A + ADR-016 (C++26 opt-in via CD_HAS_CXX26_* macros)

include_guard(GLOBAL)

include(CheckCXXSourceCompiles)

# Probe C++26 features as they ship. Result available via target_compile_definitions
# Note: only set CD_HAS_CXX26_* if both compiler supports it AND CD_USE_CXX26 enabled.

set(CD_USE_CXX26 OFF CACHE BOOL "Probe C++26 opt-in features (toolchain dependent)")

if(CD_USE_CXX26)
  check_cxx_source_compiles("
    #include <experimental/reflect>
    int main() { return 0; }
  " CD_HAS_CXX26_REFLECTION_RAW)

  check_cxx_source_compiles("
    #include <execution>
    int main() {
      auto sender = std::execution::just(42);
      return 0;
    }
  " CD_HAS_CXX26_EXECUTION_RAW)
endif()

# Report
message(STATUS "[cd] Compiler         : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "[cd] C++ Standard     : ${CMAKE_CXX_STANDARD}")
message(STATUS "[cd] C++26 opt-in     : ${CD_USE_CXX26}")
if(CD_USE_CXX26)
  message(STATUS "[cd] CXX26 reflection : ${CD_HAS_CXX26_REFLECTION_RAW}")
  message(STATUS "[cd] CXX26 execution  : ${CD_HAS_CXX26_EXECUTION_RAW}")
endif()
