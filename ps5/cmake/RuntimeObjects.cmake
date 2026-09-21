set(MKW_GAME_ROOT "${MKW_WORKSPACE}/Wiicompiled")
set(MKW_DEPS_ROOT "${MKW_WORKSPACE}/.tools/game-deps")
foreach(header SDL3-3.4.4/include/SDL3/SDL.h imgui-1.91.9b-docking/imgui.h xxHash-0.8.3/xxhash.h)
    if(NOT EXISTS "${MKW_DEPS_ROOT}/${header}")
        message(FATAL_ERROR "Missing dependency ${header}; run Prepare-GameDependencies.ps1")
    endif()
endforeach()
file(GLOB_RECURSE MKW_PS5_RUNTIME_SOURCES CONFIGURE_DEPENDS "${MKW_GAME_ROOT}/runtime/src/*.cpp")
# Implemented by the PS5 platform targets, or product descriptors compiled in
# the game-build tree so one runtime build serves both product variants.
foreach(source guest_flat_memory.cpp guest_flat_memory_macos.cpp host_context.cpp host_cpu_baseline.cpp memory.cpp
    product/base_product.cpp product/retro_rewind_product.cpp)
    list(REMOVE_ITEM MKW_PS5_RUNTIME_SOURCES "${MKW_GAME_ROOT}/runtime/src/${source}")
endforeach()
add_library(mkw_ps5_runtime OBJECT ${MKW_PS5_RUNTIME_SOURCES})
target_link_libraries(mkw_ps5_runtime PRIVATE mkw_ps5_libcxx)
target_include_directories(mkw_ps5_runtime PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../runtime"
    "${MKW_GAME_ROOT}" "${MKW_GAME_ROOT}/runtime/include" "${MKW_GAME_ROOT}/runtime/src"
    "${MKW_GAME_ROOT}/aurora-main/include" "${MKW_GAME_ROOT}/runtime/third_party"
    "${MKW_GAME_ROOT}/runtime/third_party/pugixml" "${MKW_GAME_ROOT}/runtime/third_party/toml11"
    "${MKW_DEPS_ROOT}/SDL3-3.4.4/include" "${MKW_DEPS_ROOT}/imgui-1.91.9b-docking" "${MKW_DEPS_ROOT}/xxHash-0.8.3")
target_compile_features(mkw_ps5_runtime PRIVATE cxx_std_20)
target_compile_definitions(mkw_ps5_runtime PRIVATE MKW_PLATFORM_PS5=1 TARGET_PC SDL_MAIN_HANDLED
    CRYPTOPP_DISABLE_ASM CRYPTOPP_NO_GLOBAL_BYTE)
target_compile_options(mkw_ps5_runtime PRIVATE -O2 -march=x86-64-v3 -ffast-math -w)
set_source_files_properties("${MKW_GAME_ROOT}/runtime/src/ppc_helpers.cpp" "${MKW_GAME_ROOT}/runtime/src/fpu_helpers.cpp"
    PROPERTIES COMPILE_OPTIONS "-fno-fast-math;-ffp-contract=off")
set_target_properties(mkw_ps5_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)
list(LENGTH MKW_PS5_RUNTIME_SOURCES MKW_PS5_RUNTIME_SOURCE_COUNT)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/runtime-graph.json" "{\"translationUnits\":${MKW_PS5_RUNTIME_SOURCE_COUNT},\"linkedGame\":false}\n")

# Build the same dependencies as WiiCompiled, from its vendored or pinned
# sources. SDL and Aurora still need their PS5 device backends.
enable_language(C)
add_library(mkw_ps5_xxhash STATIC "${MKW_DEPS_ROOT}/xxHash-0.8.3/xxhash.c")
add_library(mkw_ps5_pugixml STATIC "${MKW_GAME_ROOT}/runtime/third_party/pugixml/pugixml.cpp")
add_library(mkw_ps5_imgui STATIC
    "${MKW_DEPS_ROOT}/imgui-1.91.9b-docking/imgui.cpp"
    "${MKW_DEPS_ROOT}/imgui-1.91.9b-docking/imgui_draw.cpp"
    "${MKW_DEPS_ROOT}/imgui-1.91.9b-docking/imgui_tables.cpp"
    "${MKW_DEPS_ROOT}/imgui-1.91.9b-docking/imgui_widgets.cpp")
file(GLOB MKW_PS5_CRYPTO_SOURCES CONFIGURE_DEPENDS "${MKW_GAME_ROOT}/runtime/third_party/cryptopp/*.cpp")
list(FILTER MKW_PS5_CRYPTO_SOURCES EXCLUDE REGEX "/(adhoc|bench[123]|datatest|dlltest|fipsalgt|fipstest|pch|regtest[1234]|simple|test|validat[0-9]+)\\.cpp$")
list(FILTER MKW_PS5_CRYPTO_SOURCES EXCLUDE REGEX "(_avx|_ppc|_simd|_sse)\\.cpp$")
add_library(mkw_ps5_cryptopp STATIC ${MKW_PS5_CRYPTO_SOURCES})
target_compile_definitions(mkw_ps5_cryptopp PUBLIC CRYPTOPP_DISABLE_ASM CRYPTOPP_NO_GLOBAL_BYTE MKW_PLATFORM_PS5=1)
foreach(target mkw_ps5_pugixml mkw_ps5_imgui mkw_ps5_cryptopp)
    target_link_libraries(${target} PRIVATE mkw_ps5_libcxx)
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_compile_options(${target} PRIVATE -O2 -w)
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endforeach()
set_target_properties(mkw_ps5_xxhash PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_custom_target(mkw_ps5_runtime_dependencies DEPENDS mkw_ps5_pugixml mkw_ps5_imgui mkw_ps5_cryptopp mkw_ps5_xxhash)
