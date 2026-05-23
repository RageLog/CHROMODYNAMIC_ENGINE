# =============================================================================
# CDDoxygen — Doxygen API documentation pipeline
# Adds the `cd_docs` custom target when CD_ENABLE_DOXYGEN=ON (default OFF).
#
# Output: ${CMAKE_BINARY_DIR}/docs/doxygen/html/index.html
#
# Usage:
#   include(CDDoxygen)
#   cd_setup_doxygen()                  # honours CD_ENABLE_DOXYGEN
#   # then: cmake --build --target cd_docs
# =============================================================================

function(cd_setup_doxygen)
    if(NOT CD_ENABLE_DOXYGEN)
        return()
    endif()

    find_package(Doxygen QUIET)
    if(NOT DOXYGEN_FOUND)
        message(WARNING
            "[cd-docs] CD_ENABLE_DOXYGEN=ON but Doxygen not found. "
            "Install doxygen (and optionally graphviz for diagrams). "
            "Target cd_docs will not be created."
        )
        return()
    endif()

    set(_template "${CMAKE_SOURCE_DIR}/docs/Doxyfile.in")
    if(NOT EXISTS "${_template}")
        message(WARNING "[cd-docs] Doxyfile.in not found at ${_template}")
        return()
    endif()

    # Output directory under build tree so CI can publish straight from it.
    set(CD_DOXYGEN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/docs/doxygen")
    file(MAKE_DIRECTORY "${CD_DOXYGEN_OUTPUT_DIR}")

    # Inputs Doxygen walks. We point at the engine subtree + samples + top-level
    # README.md (acts as landing page). Tests and third-party deps are excluded
    # via EXCLUDE_PATTERNS in the template. Each path is per-path quoted so
    # spaces in CMAKE_SOURCE_DIR are tolerated.
    #
    # docs/ADR/ and docs/PLAN.md are intentionally NOT in INPUT: those files
    # are narrative markdown (Turkish prose, embedded `@import` examples,
    # `#include` shown literally, ad-hoc heading levels) that Doxygen mis-
    # interprets as commands. The full ADR set is rendered on GitHub from
    # the markdown source directly — no need to double-index it here.
    set(_cd_doxy_inputs
        "${CMAKE_SOURCE_DIR}/engine"
        "${CMAKE_SOURCE_DIR}/samples"
        "${CMAKE_SOURCE_DIR}/tools"
        "${CMAKE_SOURCE_DIR}/docs/DESIGN.md"
        "${CMAKE_SOURCE_DIR}/docs/LIBRARIES.md"
        "${CMAKE_SOURCE_DIR}/docs/ARCHITECTURE.md"
        "${CMAKE_SOURCE_DIR}/docs/Modules.dox"
    )
    if(EXISTS "${CMAKE_SOURCE_DIR}/README.md")
        list(APPEND _cd_doxy_inputs "${CMAKE_SOURCE_DIR}/README.md")
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/CLAUDE.md")
        list(APPEND _cd_doxy_inputs "${CMAKE_SOURCE_DIR}/CLAUDE.md")
    endif()
    set(CD_DOXYGEN_INPUT "")
    foreach(_p IN LISTS _cd_doxy_inputs)
        if(EXISTS "${_p}")
            string(APPEND CD_DOXYGEN_INPUT "\"${_p}\" ")
        endif()
    endforeach()

    set(CD_DOXYGEN_MAINPAGE "${CMAKE_SOURCE_DIR}/README.md")
    if(NOT EXISTS "${CD_DOXYGEN_MAINPAGE}")
        # Fall back to docs/DESIGN.md if there's no README at the root.
        set(CD_DOXYGEN_MAINPAGE "${CMAKE_SOURCE_DIR}/docs/DESIGN.md")
    endif()

    # Detect Graphviz `dot` for class/collab graphs. Doxygen renders text-only
    # graphs without it; we just want the toggle right.
    if(DOXYGEN_DOT_EXECUTABLE OR Doxygen_dot_FOUND)
        set(CD_DOXYGEN_HAVE_DOT "YES")
    else()
        set(CD_DOXYGEN_HAVE_DOT "NO")
    endif()

    set(_generated "${CMAKE_BINARY_DIR}/docs/Doxyfile")
    configure_file("${_template}" "${_generated}" @ONLY)

    add_custom_target(cd_docs
        COMMAND ${DOXYGEN_EXECUTABLE} "${_generated}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "[cd-docs] Generating Doxygen HTML → ${CD_DOXYGEN_OUTPUT_DIR}/html"
        VERBATIM
        USES_TERMINAL
    )

    # `docs` is a popular target name and CMake itself uses it for nothing.
    # Add an alias if free.
    if(NOT TARGET docs)
        add_custom_target(docs DEPENDS cd_docs)
    endif()

    message(STATUS "[cd-docs] Doxygen ${DOXYGEN_VERSION} → cd_docs target ready")
    message(STATUS "[cd-docs]   doxyfile: ${_generated}")
    message(STATUS "[cd-docs]   output  : ${CD_DOXYGEN_OUTPUT_DIR}")
    message(STATUS "[cd-docs]   dot     : ${CD_DOXYGEN_HAVE_DOT}")
endfunction()
