# Keep WiiCompiled's PAD mappings, dead zones, input blocking and persistence.
# This directory scopes the PS5 C++ headers to upstream Abseil's own targets.
add_compile_options(-nostdinc++)
include_directories(SYSTEM "${MKW_PS5_CXX_BUILD}/include/c++/v1")
link_libraries(mkw_ps5_libcxx)
set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(CMAKE_CXX_STANDARD 20)
set(MKW_ABSL_ROOT "${MKW_WORKSPACE}/.tools/desktop-check/abseil-cpp-20240722.0")
if(NOT EXISTS "${MKW_ABSL_ROOT}/absl/container/flat_hash_map.h")
    message(FATAL_ERROR "Run Prepare-DesktopCheck.ps1 for Aurora's pinned Abseil")
endif()
add_subdirectory("${MKW_ABSL_ROOT}" abseil)
add_library(mkw_ps5_aurora_input STATIC
    "${CMAKE_CURRENT_LIST_DIR}/../aurora_input.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../pad_autopilot.cpp"
    "${MKW_AURORA_ROOT}/lib/input.cpp"
    "${MKW_AURORA_ROOT}/lib/dolphin/pad/pad.cpp")
target_include_directories(mkw_ps5_aurora_input PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/.."
    "${MKW_AURORA_ROOT}/include" "${MKW_AURORA_ROOT}/lib"
    "${MKW_DEPS_ROOT}/SDL3-3.4.4/include")
target_compile_definitions(mkw_ps5_aurora_input PUBLIC MKW_PLATFORM_PS5=1 TARGET_PC=1)
target_compile_options(mkw_ps5_aurora_input PRIVATE -O2 -include cstdlib)
if(MKW_PS5_RELEASE)
    target_compile_definitions(mkw_ps5_aurora_input PRIVATE MKW_PS5_RELEASE=1)  # no autopilot script loading
endif()
target_link_libraries(mkw_ps5_aurora_input PUBLIC absl::flat_hash_map mkw_ps5_fmt mkw_ps5_texture_decode mkw_ps5_native_input mkw_ps5_virtual_pad)
set_target_properties(mkw_ps5_aurora_input PROPERTIES POSITION_INDEPENDENT_CODE ON
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
