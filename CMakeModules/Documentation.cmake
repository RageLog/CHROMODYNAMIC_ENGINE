function(dfh_setup_external_docs)
    set(options)
    set(oneValueArgs DOCUMENTS_ROOT API_REFERENCE_DIR DOXYFILE)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "" ${ARGN})

    if(ARG_DOCUMENTS_ROOT)
        set(_documents_root "${ARG_DOCUMENTS_ROOT}")
    else()
        set(_documents_root "${CMAKE_SOURCE_DIR}/../02_Documents")
    endif()

    file(TO_CMAKE_PATH "${_documents_root}" _documents_root)

    # Keep user-provided cache overrides intact; only provide defaults.
    if(NOT DEFINED DFH_DOCUMENTS_ROOT OR DFH_DOCUMENTS_ROOT STREQUAL "")
        set(DFH_DOCUMENTS_ROOT "${_documents_root}" CACHE PATH "External documentation root directory")
    endif()

    if(ARG_API_REFERENCE_DIR)
        set(_api_reference_dir "${ARG_API_REFERENCE_DIR}")
    else()
        set(_api_reference_dir "${DFH_DOCUMENTS_ROOT}/03_API_Reference")
    endif()
    file(TO_CMAKE_PATH "${_api_reference_dir}" _api_reference_dir)

    if(NOT DEFINED DFH_API_REFERENCE_DIR OR DFH_API_REFERENCE_DIR STREQUAL "")
        set(DFH_API_REFERENCE_DIR "${_api_reference_dir}" CACHE PATH "API reference root directory")
    endif()

    if(ARG_DOXYFILE)
        set(_doxyfile "${ARG_DOXYFILE}")
    else()
        set(_doxyfile "${DFH_API_REFERENCE_DIR}/Doxyfile")
    endif()
    file(TO_CMAKE_PATH "${_doxyfile}" _doxyfile)

    if(NOT DEFINED DFH_DOXYFILE_PATH OR DFH_DOXYFILE_PATH STREQUAL "")
        set(DFH_DOXYFILE_PATH "${_doxyfile}" CACHE FILEPATH "Doxygen configuration template path")
    endif()

    # Backward-compatible alias used by existing callers.
    if(NOT DEFINED DFH_DOCS_DIR OR DFH_DOCS_DIR STREQUAL "")
        set(DFH_DOCS_DIR "${DFH_API_REFERENCE_DIR}" CACHE PATH "External documentation source directory")
    endif()

    if(NOT EXISTS "${DFH_API_REFERENCE_DIR}")
        if(ENABLE_DOXYGEN)
            message(WARNING "[Docs] API reference directory not found: ${DFH_API_REFERENCE_DIR}")
        endif()
    endif()

    if(NOT EXISTS "${DFH_DOXYFILE_PATH}")
        if(ENABLE_DOXYGEN)
            message(WARNING "[Docs] Doxygen file not found: ${DFH_DOXYFILE_PATH}")
        endif()
    endif()
endfunction()
