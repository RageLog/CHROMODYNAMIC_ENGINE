# cmake/Sanitizers.cmake

function(enable_sanitizers target_name)
    # Only enable sanitizers in Debug builds (they slow down Release)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(STATUS "[Sanitizers] Skipped for '${target_name}' (only enabled in Debug builds)")
        return()
    endif()

    set(SANITIZERS "")

    option(ENABLE_ASAN "Enable Address Sanitizer" ON)
    option(ENABLE_UBSAN "Enable Undefined Behavior Sanitizer" ON)
    
    if(MSVC)
        if(ENABLE_ASAN)
            target_compile_options(${target_name} PRIVATE /fsanitize=address)
            target_link_options(${target_name} PRIVATE /fsanitize=address) 
        endif()
    
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        if(ENABLE_ASAN)
            list(APPEND SANITIZERS "address")
        endif()

        if(ENABLE_UBSAN)
            list(APPEND SANITIZERS "undefined")
        endif()

        if(SANITIZERS)
            string(REPLACE ";" "," SANITIZER_LIST "${SANITIZERS}")
            target_compile_options(${target_name} PRIVATE -fsanitize=${SANITIZER_LIST} -fno-omit-frame-pointer)
            target_link_options(${target_name} PRIVATE -fsanitize=${SANITIZER_LIST})
        endif()
    endif()
    
    message(STATUS "[Sanitizers] Target '${target_name}' secured with: ASan=${ENABLE_ASAN}, UBSan=${ENABLE_UBSAN}")
endfunction()