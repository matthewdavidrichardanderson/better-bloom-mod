cmake_minimum_required(VERSION 3.26)
project(better_bloom VERSION 2.8.0 LANGUAGES CXX)

set(DUSKLIGHT_DIR "" CACHE PATH "Dusklight source checkout, including extern/aurora")
if(NOT EXISTS "${DUSKLIGHT_DIR}/sdk/CMakeLists.txt")
    message(FATAL_ERROR "Set DUSKLIGHT_DIR to a Dusklight checkout containing the mod SDK")
endif()
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

# Current Apple linkers reject Dusklight's generated bundle-loader stub because it
# does not contain an LC_UUID command. The classic linker accepts the stub and
# produces a loadable macOS mod bundle.
if(APPLE)
    target_link_options(better_bloom PRIVATE -Wl,-ld_classic)
endif()
