# =============================================================================
# CHROMODYNAMIC — scripts/check_shader_staleness.cmake
# X5-3(b) (ADR-20260608 addendum A.3) — pre-golden EXE-side staleness guard.
#
# Asserts sha256(SOURCE shader) == sha256(EXE-side copied shader) for every
# hello_engine *.glsl. Run as a CTest FIXTURES_SETUP step BEFORE any golden-
# image compare so a stale exe-side copy fails LOUDLY here instead of
# producing a false byte-identical golden (memory rule
# feedback_ondisk_shader_golden_staleness).
#
# Invocation (from tests/CMakeLists.txt):
#   ${CMAKE_COMMAND}
#     -DSRC_DIR=<repo>/samples/engine/hello_engine/shaders
#     -DEXE_DIR=$<TARGET_FILE_DIR:cd_sample_hello_engine>/shaders
#     -P scripts/check_shader_staleness.cmake
#
# Exits non-zero (FATAL_ERROR) on the first divergence; prints the offending
# file plus both digests so the failure is self-diagnosing.
# =============================================================================
if(NOT DEFINED SRC_DIR)
  message(FATAL_ERROR "check_shader_staleness: SRC_DIR not set")
endif()
if(NOT DEFINED EXE_DIR)
  message(FATAL_ERROR "check_shader_staleness: EXE_DIR not set")
endif()

file(GLOB shader_sources "${SRC_DIR}/*.glsl")
if(shader_sources STREQUAL "")
  message(FATAL_ERROR "check_shader_staleness: no *.glsl found under '${SRC_DIR}'")
endif()

set(checked 0)
foreach(src ${shader_sources})
  get_filename_component(fname "${src}" NAME)
  set(exe "${EXE_DIR}/${fname}")
  if(NOT EXISTS "${exe}")
    message(FATAL_ERROR
      "check_shader_staleness: exe-side copy MISSING for '${fname}'\n"
      "  source : ${src}\n"
      "  exe    : ${exe}\n"
      "  -> the POST_BUILD copy_if_different step did not run; rebuild "
      "cd_sample_hello_engine.")
  endif()
  file(SHA256 "${src}" src_hash)
  file(SHA256 "${exe}" exe_hash)
  if(NOT src_hash STREQUAL exe_hash)
    message(FATAL_ERROR
      "check_shader_staleness: STALE exe-side shader '${fname}'\n"
      "  source sha256 : ${src_hash}\n"
      "  exe    sha256 : ${exe_hash}\n"
      "  -> exe-side copy is out of date (relink-only-build trap). A golden "
      "computed now would be a FALSE byte-identical. Rebuild "
      "cd_sample_hello_engine to re-fire the shader copy.")
  endif()
  math(EXPR checked "${checked} + 1")
endforeach()

message(STATUS "check_shader_staleness: ${checked} hello_engine shader(s) sha256-match source == exe-side")
