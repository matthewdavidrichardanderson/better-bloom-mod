cmake_minimum_required(VERSION 3.26)
project(better_bloom VERSION 2.8.0 LANGUAGES CXX)

set(DUSKLIGHT_DIR "" CACHE PATH "Dusklight source checkout, including extern/aurora")
if(NOT EXISTS "${DUSKLIGHT_DIR}/sdk/CMakeLists.txt")
    message(FATAL_ERROR "Set DUSKLIGHT_DIR to a Dusklight checkout containing the mod SDK")
endif()
option(BETTER_BLOOM_TESTS "Build automated tests" OFF)
option(BETTER_BLOOM_GPU_TESTS "Build rendering tests requiring a GPU" ON)
add_subdirectory("${DUSKLIGHT_DIR}/sdk" dusklight-sdk EXCLUDE_FROM_ALL)

add_mod(better_bloom
    FEATURES game webgpu
    SOURCES src/mod.cpp src/renderer.cpp src/settings.cpp
    MOD_JSON mod.json
    RES_DIR res
    BUNDLE
)

if(MSVC)
    target_compile_options(better_bloom PRIVATE /Zc:__cplusplus /W4)
    target_link_options(better_bloom PRIVATE /PDBALTPATH:better_bloom.pdb)
else()
    target_compile_options(better_bloom PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BETTER_BLOOM_TESTS)
    enable_testing()
    if(BETTER_BLOOM_GPU_TESTS)
        add_executable(bloom_tests tests/render.cpp src/renderer.cpp)
        target_compile_features(bloom_tests PRIVATE cxx_std_20)
        target_include_directories(bloom_tests PRIVATE src)
        target_link_libraries(bloom_tests PRIVATE dusklight_mod_feature_webgpu dawn::webgpu_dawn)
        add_test(NAME bloom_rendering COMMAND bloom_tests "${CMAKE_CURRENT_SOURCE_DIR}/res")
    endif()
    add_executable(settings_tests tests/settings.cpp src/settings.cpp)
    target_compile_features(settings_tests PRIVATE cxx_std_20)
    target_include_directories(settings_tests PRIVATE src)
    target_link_libraries(settings_tests PRIVATE dusklight_mod_api)
    add_test(NAME bloom_controls COMMAND settings_tests)
    add_executable(pulse_tests tests/pulse.cpp)
    target_compile_features(pulse_tests PRIVATE cxx_std_20)
    target_include_directories(pulse_tests PRIVATE src)
    add_test(NAME bloom_pulse COMMAND pulse_tests)
    add_executable(profile_tests tests/profile.cpp)
    target_compile_features(profile_tests PRIVATE cxx_std_20)
    target_include_directories(profile_tests PRIVATE src)
    add_test(NAME bloom_reference_profile COMMAND profile_tests)
    if(WIN32 AND BETTER_BLOOM_GPU_TESTS)
        add_custom_command(TARGET bloom_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:dawn::webgpu_dawn> $<TARGET_FILE_DIR:bloom_tests>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE_DIR:dawn::webgpu_dawn>/dxil.dll $<TARGET_FILE_DIR:bloom_tests>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE_DIR:dawn::webgpu_dawn>/dxcompiler.dll $<TARGET_FILE_DIR:bloom_tests>)
    endif()
endif()
