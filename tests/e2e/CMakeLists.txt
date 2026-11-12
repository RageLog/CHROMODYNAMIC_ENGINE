# tests/e2e/CMakeLists.txt
cmake_minimum_required(VERSION 3.28)

set(E2E_SOURCES
    main.cpp
    harness/E2eHarness.hpp
    harness/MockAudioSink.hpp
    harness/HeadlessReadback.hpp
    harness/CliRunner.hpp
    tier1_feature/test_tier1_features_1_to_9.cpp
    tier1_feature/test_tier1_features_10_to_19.cpp
    tier1_feature/test_tier1_features_20_to_31.cpp
    tier2_boundary/test_tier2_boundary_1_to_9.cpp
    tier2_boundary/test_tier2_boundary_10_to_19.cpp
    tier2_boundary/test_tier2_boundary_20_to_31.cpp
    tier3_pairwise/test_tier3_pairwise.cpp
    tier4_scenarios/test_tier4_scenarios.cpp
)

add_executable(cd_e2e_suite ${E2E_SOURCES})

target_compile_features(cd_e2e_suite PUBLIC cxx_std_23)
cd_apply_warnings(cd_e2e_suite)
cd_apply_sanitizers(cd_e2e_suite)

target_include_directories(cd_e2e_suite PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/harness
)

target_link_libraries(cd_e2e_suite PRIVATE
    GTest::gtest
    cd::core
    cd::math
    cd::mem
    cd::time
    cd::platform
    cd::diag
    cd::log
    cd::asset
    cd::asset_image
    cd::asset_json
    cd::asset_pak
    cd::imgdiff
    cd::rhi
    cd::render2d
    cd::scene_renderer
    cd::audio
    cd::physics
    cd::scene
    cd::world2d
    cd::schema
    cd::app_common
)

if(TARGET cd::ui_renderer)
    target_link_libraries(cd_e2e_suite PRIVATE cd::ui_renderer)
endif()

set_target_properties(cd_e2e_suite PROPERTIES FOLDER "tests/e2e")

if(CD_ENABLE_TESTING)
    add_test(NAME cd_e2e_suite COMMAND cd_e2e_suite)
    set_tests_properties(cd_e2e_suite PROPERTIES TIMEOUT 300)

    # Alias target and test for parity
    add_custom_target(cd_test_e2e_suite DEPENDS cd_e2e_suite)
    add_test(NAME cd_test_e2e_suite COMMAND cd_e2e_suite)
    set_tests_properties(cd_test_e2e_suite PROPERTIES TIMEOUT 300)
endif()
