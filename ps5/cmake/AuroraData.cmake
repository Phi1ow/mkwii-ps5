# CPU texture conversion retained from WiiCompiled's Aurora. This target has
# no Dawn/WebGPU dependencies and will feed the native AGC upload path.
set(MKW_AURORA_ROOT "${MKW_WORKSPACE}/Wiicompiled/aurora-main")
set(MKW_FMT_ROOT "${MKW_WORKSPACE}/.tools/game-deps/fmt-11.1.4")
if(NOT EXISTS "${MKW_FMT_ROOT}/src/format.cc")
    message(FATAL_ERROR "Run Prepare-GameDependencies.ps1 for the pinned fmt source")
endif()
add_library(mkw_ps5_fmt STATIC "${MKW_FMT_ROOT}/src/format.cc" "${MKW_FMT_ROOT}/src/os.cc")
target_include_directories(mkw_ps5_fmt PUBLIC "${MKW_FMT_ROOT}/include")
# FILE is opaque on PS5. Never select fmt's fast wrapper using the FreeBSD
# header's private fields: those do not describe the console's libc FILE.
target_compile_definitions(mkw_ps5_fmt PUBLIC FMT_USE_FALLBACK_FILE=1 FMT_USE_FCNTL=0)
add_library(mkw_ps5_texture_decode STATIC
    "${MKW_AURORA_ROOT}/lib/gfx/texture_convert.cpp" "${MKW_AURORA_ROOT}/lib/logging.cpp")
target_include_directories(mkw_ps5_texture_decode PUBLIC "${MKW_AURORA_ROOT}/include" "${MKW_AURORA_ROOT}/lib")
target_compile_definitions(mkw_ps5_texture_decode PUBLIC MKW_PLATFORM_PS5=1 TARGET_PC=1)
target_link_libraries(mkw_ps5_texture_decode PUBLIC mkw_ps5_fmt)
foreach(target mkw_ps5_fmt mkw_ps5_texture_decode)
    target_link_libraries(${target} PRIVATE mkw_ps5_libcxx)
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_compile_options(${target} PRIVATE -O2 -march=x86-64-v3 -ffp-contract=off -include cstdlib)
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endforeach()
add_library(mkw_ps5_agc_texture STATIC "${CMAKE_CURRENT_LIST_DIR}/../gpu/texture_layout.cpp" "${CMAKE_CURRENT_LIST_DIR}/../gpu/gpu_texture.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/color_target.cpp" "${CMAKE_CURRENT_LIST_DIR}/../gpu/gpu_color_target.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/depth_layout.cpp" "${CMAKE_CURRENT_LIST_DIR}/../gpu/depth_target.cpp" "${CMAKE_CURRENT_LIST_DIR}/../gpu/gpu_depth_target.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/gpu_fence.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/gpu_buffer.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/gpu_wait_service.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/shader_binary.cpp" "${CMAKE_CURRENT_LIST_DIR}/../gpu/gpu_shader.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/blit_vertices.cpp" "${CMAKE_CURRENT_LIST_DIR}/../gpu/agc_blit.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/video_presenter.cpp" "${CMAKE_CURRENT_LIST_DIR}/../gpu/present_rect.cpp")
target_include_directories(mkw_ps5_agc_texture PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../gpu")
target_compile_features(mkw_ps5_agc_texture PUBLIC cxx_std_20)
target_link_libraries(mkw_ps5_agc_texture PRIVATE mkw_ps5_libcxx)
target_compile_options(mkw_ps5_agc_texture PRIVATE -O2 -march=x86-64-v3 -ffp-contract=off -Wall -Wextra -Werror)
set_target_properties(mkw_ps5_agc_texture PROPERTIES POSITION_INDEPENDENT_CODE ON)
