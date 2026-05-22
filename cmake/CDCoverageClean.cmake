# CDCoverageClean.cmake — helper invoked by the `coverage-clean` target.
#
# Removes every .gcda file under SEARCH_DIR so the next test run starts
# with a clean coverage baseline. The .gcno files (emitted at compile
# time, one per TU) are preserved because regenerating them would force
# a full rebuild.
#
# Called as:
#   cmake -DSEARCH_DIR=<build-dir> -P CDCoverageClean.cmake

if(NOT DEFINED SEARCH_DIR)
  message(FATAL_ERROR "CDCoverageClean.cmake invoked without SEARCH_DIR")
endif()

file(GLOB_RECURSE _gcda_files "${SEARCH_DIR}/*.gcda")
list(LENGTH _gcda_files _count)
foreach(_f IN LISTS _gcda_files)
  file(REMOVE "${_f}")
endforeach()
message(STATUS "[cd] coverage: removed ${_count} .gcda file(s)")
