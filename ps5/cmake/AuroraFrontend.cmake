# Unmodified GX command producers, separated from renderer resource headers.
# FIFO processing and frame synchronization remain required backend symbols.
set(MKW_TRACY_ROOT "${MKW_WORKSPACE}/.tools/game-deps/tracy-a64b9a20294d59421a2f57aeca3c6383d8c48169")
if(NOT EXISTS "${MKW_TRACY_ROOT}/public/tracy/Tracy.hpp")
    message(FATAL_ERROR "Run Prepare-GameDependencies.ps1 for Aurora's pinned Tracy headers")
endif()
set(MKW_GX_FRONTEND_SOURCES)
foreach(unit GXBump GXCull GXDispList GXDraw GXExtra GXFifo GXGeometry GXGet GXLighting GXManage GXPixel GXTev GXTexture GXTransform GXVert frontend)
    list(APPEND MKW_GX_FRONTEND_SOURCES "${MKW_AURORA_ROOT}/lib/dolphin/gx/${unit}.cpp")
endforeach()
add_library(mkw_ps5_gx_frontend STATIC ${MKW_GX_FRONTEND_SOURCES} "${MKW_AURORA_ROOT}/lib/gx/fifo.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../runtime/aurora_debug_ps5.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../runtime/aurora_display_ps5.cpp")
target_include_directories(mkw_ps5_gx_frontend PUBLIC "${MKW_AURORA_ROOT}/include" "${MKW_AURORA_ROOT}/lib")
target_include_directories(mkw_ps5_gx_frontend PRIVATE "${MKW_TRACY_ROOT}/public")
target_compile_definitions(mkw_ps5_gx_frontend PUBLIC MKW_PLATFORM_PS5=1 TARGET_PC=1)
target_compile_features(mkw_ps5_gx_frontend PUBLIC cxx_std_20)
target_compile_options(mkw_ps5_gx_frontend PRIVATE -O2 -march=x86-64-v3 -ffp-contract=off -include cstdlib)
target_include_directories(mkw_ps5_gx_frontend PRIVATE "${MKW_DEPS_ROOT}/xxHash-0.8.3")
target_link_libraries(mkw_ps5_gx_frontend PUBLIC mkw_ps5_fmt mkw_ps5_xxhash PRIVATE mkw_ps5_libcxx)
set_target_properties(mkw_ps5_gx_frontend PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(mkw_ps5_gx_registers STATIC
    "${MKW_AURORA_ROOT}/lib/gx/register_decoder.cpp"
    "${MKW_AURORA_ROOT}/lib/gx/register_dispatch.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_register_state.cpp")
target_include_directories(mkw_ps5_gx_registers PUBLIC "${MKW_AURORA_ROOT}/include" "${MKW_AURORA_ROOT}/lib")
target_include_directories(mkw_ps5_gx_registers PRIVATE "${MKW_TRACY_ROOT}/public")
target_compile_definitions(mkw_ps5_gx_registers PUBLIC MKW_PLATFORM_PS5=1 TARGET_PC=1)
target_compile_features(mkw_ps5_gx_registers PUBLIC cxx_std_20)
target_compile_options(mkw_ps5_gx_registers PRIVATE -O2 -march=x86-64-v3 -ffp-contract=off -include cstdlib)
target_link_libraries(mkw_ps5_gx_registers PUBLIC mkw_ps5_fmt PRIVATE mkw_ps5_libcxx)
set_target_properties(mkw_ps5_gx_registers PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(mkw_ps5_gx_textures STATIC "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_texture_cache.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../runtime/aurora_guest_write_ps5.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_memory_sources.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../runtime/perf_microbench_ps5.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../runtime/guest_sampler_ps5.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../runtime/async_log.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../runtime/thread_placement_ps5.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../runtime/gx_host_memory_ps5.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_copy_texture_cache.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_copy_readback.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_copy_deferred.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_copy_store.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_texture_copy.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_texture_copy_plan.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_display_copy.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_display_copy_plan.cpp"
    "${MKW_AURORA_ROOT}/lib/gfx/efb_ram_encoder.cpp")
target_include_directories(mkw_ps5_gx_textures PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../gpu"
    "${MKW_AURORA_ROOT}/include" "${MKW_AURORA_ROOT}/lib" "${MKW_DEPS_ROOT}/xxHash-0.8.3")
target_include_directories(mkw_ps5_gx_textures PRIVATE "${MKW_AURORA_ROOT}/../runtime/include" "${CMAKE_CURRENT_LIST_DIR}/../runtime")
target_compile_definitions(mkw_ps5_gx_textures PUBLIC MKW_PLATFORM_PS5=1 TARGET_PC=1)
target_compile_features(mkw_ps5_gx_textures PUBLIC cxx_std_20)
target_compile_options(mkw_ps5_gx_textures PRIVATE -O2 -march=x86-64-v3 -ffp-contract=off -Wall -Wextra -include cstdlib)
target_link_libraries(mkw_ps5_gx_textures PUBLIC mkw_ps5_texture_decode mkw_ps5_agc_texture mkw_ps5_gx_frontend
    mkw_ps5_gx_registers mkw_ps5_xxhash PRIVATE mkw_ps5_libcxx)
set_target_properties(mkw_ps5_gx_textures PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(mkw_ps5_gx_tev STATIC "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_tev_program.cpp")
target_include_directories(mkw_ps5_gx_tev PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../gpu"
    "${MKW_AURORA_ROOT}/include" "${MKW_AURORA_ROOT}/lib")
target_compile_definitions(mkw_ps5_gx_tev PUBLIC MKW_PLATFORM_PS5=1 TARGET_PC=1)
target_compile_features(mkw_ps5_gx_tev PUBLIC cxx_std_20)
target_compile_options(mkw_ps5_gx_tev PRIVATE -O2 -march=x86-64-v3 -fno-fast-math -ffp-contract=off -Wall -Wextra -include cstdlib)
target_link_libraries(mkw_ps5_gx_tev PUBLIC mkw_ps5_gx_registers PRIVATE mkw_ps5_libcxx)
set_target_properties(mkw_ps5_gx_tev PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(mkw_ps5_gx_material STATIC "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_sampler.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_blend_state.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_fog_state.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_direct_material.cpp")
target_compile_features(mkw_ps5_gx_material PUBLIC cxx_std_20)
target_compile_options(mkw_ps5_gx_material PRIVATE -O2 -march=x86-64-v3 -fno-fast-math -ffp-contract=off -Wall -Wextra -include cstdlib)
target_link_libraries(mkw_ps5_gx_material PUBLIC mkw_ps5_gx_tev mkw_ps5_gx_textures PRIVATE mkw_ps5_libcxx)
set_target_properties(mkw_ps5_gx_material PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(mkw_ps5_gx_geometry STATIC "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_geometry.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_geometry_buffer.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_transform_state.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_lighting_state.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_texgen_state.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_draw_packet.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/agc_gx_draw.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_draw_dispatch.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_renderer.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_frame_renderer.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../runtime/aurora_bootstrap_ps5.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../runtime/aurora_timing_ps5.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/present_history.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_draw.cpp")
target_include_directories(mkw_ps5_gx_geometry PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../gpu")
target_compile_features(mkw_ps5_gx_geometry PUBLIC cxx_std_20)
target_compile_options(mkw_ps5_gx_geometry PRIVATE -O2 -march=x86-64-v3 -fno-fast-math -ffp-contract=off -Wall -Wextra -include cstdlib)
target_link_libraries(mkw_ps5_gx_geometry PUBLIC mkw_ps5_gx_registers mkw_ps5_gx_frontend mkw_ps5_gx_material mkw_ps5_gx_viewport mkw_ps5_agc_texture mkw_ps5_aurora_input PRIVATE mkw_ps5_libcxx)
set_target_properties(mkw_ps5_gx_geometry PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MKW_PS5_RELEASE)
    # Frame renderer screenshots/frame-stats line and the bootstrap diagnostics.
    target_compile_definitions(mkw_ps5_gx_geometry PRIVATE MKW_PS5_RELEASE=1)
endif()
add_library(mkw_ps5_gx_viewport STATIC "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_viewport.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_video_state.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_video_backend.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_viewport_backend.cpp")
target_compile_features(mkw_ps5_gx_viewport PUBLIC cxx_std_20)
target_compile_options(mkw_ps5_gx_viewport PRIVATE -O2 -march=x86-64-v3 -fno-fast-math -ffp-contract=off -Wall -Wextra -include cstdlib)
target_include_directories(mkw_ps5_gx_viewport PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../gpu")
target_link_libraries(mkw_ps5_gx_viewport PUBLIC mkw_ps5_gx_registers PRIVATE mkw_ps5_libcxx)
set_target_properties(mkw_ps5_gx_viewport PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(mkw_ps5_gx_copy_commands STATIC "${CMAKE_CURRENT_LIST_DIR}/../gpu/gx_copy_commands.cpp")
target_compile_features(mkw_ps5_gx_copy_commands PUBLIC cxx_std_20)
target_compile_options(mkw_ps5_gx_copy_commands PRIVATE -O2 -march=x86-64-v3 -fno-fast-math -ffp-contract=off -Wall -Wextra -include cstdlib)
target_link_libraries(mkw_ps5_gx_copy_commands PUBLIC mkw_ps5_gx_registers mkw_ps5_gx_frontend PRIVATE mkw_ps5_libcxx)
set_target_properties(mkw_ps5_gx_copy_commands PROPERTIES POSITION_INDEPENDENT_CODE ON)
