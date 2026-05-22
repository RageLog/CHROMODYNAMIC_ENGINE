function(enable_doxygen)
    set(options)
    set(oneValueArgs DOXYFILE API_REFERENCE_DIR DOCUMENTS_ROOT OUTPUT_DIR DOCS_DIR)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "" ${ARGN})

    if(ARG_DOCUMENTS_ROOT)
        set(_documents_root "${ARG_DOCUMENTS_ROOT}")
    elseif(DEFINED DFH_DOCUMENTS_ROOT AND NOT DFH_DOCUMENTS_ROOT STREQUAL "")
        set(_documents_root "${DFH_DOCUMENTS_ROOT}")
    else()
        set(_documents_root "${CMAKE_SOURCE_DIR}/../02_Documents")
    endif()

    if(ARG_API_REFERENCE_DIR)
        set(_api_reference_dir "${ARG_API_REFERENCE_DIR}")
    elseif(ARG_DOCS_DIR)
        set(_api_reference_dir "${ARG_DOCS_DIR}")
    elseif(DEFINED DFH_API_REFERENCE_DIR AND NOT DFH_API_REFERENCE_DIR STREQUAL "")
        set(_api_reference_dir "${DFH_API_REFERENCE_DIR}")
    elseif(DEFINED DFH_DOCS_DIR AND NOT DFH_DOCS_DIR STREQUAL "")
        set(_api_reference_dir "${DFH_DOCS_DIR}")
    else()
        set(_api_reference_dir "${_documents_root}/03_API_Reference")
    endif()

    if(ARG_DOXYFILE)
        set(_doxyfile "${ARG_DOXYFILE}")
    elseif(DEFINED DFH_DOXYFILE_PATH AND NOT DFH_DOXYFILE_PATH STREQUAL "")
        set(_doxyfile "${DFH_DOXYFILE_PATH}")
    else()
        set(_doxyfile "${_api_reference_dir}/Doxyfile")
    endif()

    if(NOT EXISTS "${_doxyfile}")
        message(WARNING "[Doxygen] Doxyfile not found: ${_doxyfile}")
        return()
    endif()

    find_package(Doxygen)
    if(NOT DOXYGEN_FOUND)
        message(WARNING "[Doxygen] Doxygen not found. Documentation target skipped.")
        return()
    endif()

    if(ARG_OUTPUT_DIR)
        set(_output_dir "${ARG_OUTPUT_DIR}")
    else()
        set(_output_dir "${CMAKE_BINARY_DIR}/docs/doxygen")
    endif()
    file(MAKE_DIRECTORY "${_output_dir}")

    set(_generated_doxyfile "${CMAKE_BINARY_DIR}/docs/Doxyfile.generated")
    file(READ "${_doxyfile}" _doxyfile_content)
    file(WRITE "${_generated_doxyfile}" "${_doxyfile_content}\n")

    set(_mainpage "${_documents_root}/00_Overview/README.md")
    set(_input_root "${CMAKE_SOURCE_DIR}")
    set(_input_overview "${_documents_root}/00_Overview")
    set(_input_user_manual "${_documents_root}/01_User_Manual")
    set(_input_dev_guide "${_documents_root}/02_Developer_Guide")
    set(_input_api_readme "${_api_reference_dir}/README.md")
    set(_warning_log "${_output_dir}/warnings.log")

    file(APPEND "${_generated_doxyfile}" "PROJECT_NAME = \"DtForHil\"\n")
    file(APPEND "${_generated_doxyfile}" "INPUT = \"${_input_root}\" \"${_input_overview}\" \"${_input_user_manual}\" \"${_input_dev_guide}\" \"${_input_api_readme}\"\n")
    file(APPEND "${_generated_doxyfile}" "OUTPUT_DIRECTORY = \"${_output_dir}\"\n")
    file(APPEND "${_generated_doxyfile}" "USE_MDFILE_AS_MAINPAGE = \"${_mainpage}\"\n")
    file(APPEND "${_generated_doxyfile}" "STRIP_FROM_PATH = \"${CMAKE_SOURCE_DIR}\"\n")
    file(APPEND "${_generated_doxyfile}" "STRIP_FROM_INC_PATH = \"${CMAKE_SOURCE_DIR}\"\n")
    file(APPEND "${_generated_doxyfile}" "EXCLUDE_PATTERNS = */build/* */.git/* */.vscode/* */tests/* */outputs/* */extras/scripts/* */.cache/*\n")
    file(APPEND "${_generated_doxyfile}" "WARN_LOGFILE = \"${_warning_log}\"\n")
    file(APPEND "${_generated_doxyfile}" "GENERATE_HTML = YES\n")
    file(APPEND "${_generated_doxyfile}" "GENERATE_LATEX = NO\n")

    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} "${_generated_doxyfile}"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        COMMENT "Generating Doxygen documentation into ${_output_dir}"
        VERBATIM
    )

    message(STATUS "[Doxygen] docs target uses ${_generated_doxyfile}")
    message(STATUS "[Doxygen] output directory: ${_output_dir}")
endfunction()
