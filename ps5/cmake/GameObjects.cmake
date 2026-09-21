# Consume the translator's complete base-product graph. Never glob individual
# functions or silently omit a shard that fails to compile.
set(MKW_GAME_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../Wiicompiled")
include("${MKW_GAME_ROOT}/generated/build_shards/shards.cmake")
if(NOT MKW_BASE_COMMON_SHARDS)
    message(FATAL_ERROR "Run Translate-Game.ps1 to generate the base-product shards")
endif()
# Retro Rewind product: links the mod shards and the "retro_rewind" dispatch
# profile instead of the base one. Requires Translate-Game.ps1 -RetroRewind.
option(MKW_PS5_RETRO_REWIND "Build the Retro Rewind product" OFF)
set(MKW_PS5_REGISTRATION_SOURCES ${MKW_BASE_REGISTRATION_SOURCES})
set(MKW_PS5_PRODUCT_SOURCE "${MKW_GAME_ROOT}/runtime/src/product/base_product.cpp")
set(MKW_PS5_PRODUCT_NAME "base")
set(MKW_PS5_GAME_DATA_ASM)
if(MKW_PS5_RETRO_REWIND)
    if(NOT MKW_HAVE_RETRO_REWIND_SHARDS)
        message(FATAL_ERROR "Retro Rewind shards missing; run Translate-Game.ps1 -RetroRewind first")
    endif()
    # The retro registration sources are a superset of the base set: they
    # register every winning base function plus the mod functions and publish
    # the "retro_rewind" dispatch table. Linking both sets aborts at startup
    # ("Multiple generated indirect-dispatch profiles").
    set(MKW_PS5_REGISTRATION_SOURCES ${MKW_RETRO_REGISTRATION_SOURCES})
    set(MKW_PS5_PRODUCT_SOURCE "${MKW_GAME_ROOT}/runtime/src/product/retro_rewind_product.cpp")
    set(MKW_PS5_PRODUCT_NAME "retro_rewind")
    foreach(source IN LISTS MKW_RETRO_EXTRA_SOURCES)
        if(source MATCHES "\\.cpp$")
            list(APPEND MKW_PS5_RETRO_EXTRA_CPP "${source}")
        elseif(source MATCHES "\\.S$")
            # Same COFF section-directive fix as data_sections_init_blobs.S.
            file(READ "${source}" MKW_MOD_BLOB_ASM)
            string(REPLACE ".section .rdata,\"dr\"" ".section .rodata,\"a\",@progbits"
                MKW_MOD_BLOB_ASM "${MKW_MOD_BLOB_ASM}")
            get_filename_component(blobName "${source}" NAME_WE)
            set(adapted "${CMAKE_CURRENT_BINARY_DIR}/${blobName}_ps5.S")
            file(WRITE "${adapted}" "${MKW_MOD_BLOB_ASM}")
            list(APPEND MKW_PS5_GAME_DATA_ASM "${adapted}")
        endif()
    endforeach()
endif()
# The *_PORTABLE_SENSITIVE shards are per-profile re-emissions of the same base
# function symbols (direct calls bound to each profile's dispatch winners), so a
# product links exactly one of the two sets - never both.
set(MKW_PS5_SENSITIVE_SHARDS ${MKW_BASE_PORTABLE_SENSITIVE_SHARDS})
if(MKW_PS5_RETRO_REWIND)
    set(MKW_PS5_SENSITIVE_SHARDS ${MKW_RETRO_PORTABLE_SENSITIVE_SHARDS})
endif()
set(MKW_PS5_TRANSLATED_SOURCES ${MKW_BASE_COMMON_SHARDS} ${MKW_PS5_SENSITIVE_SHARDS}
    ${MKW_PS5_REGISTRATION_SOURCES} ${MKW_PS5_RETRO_EXTRA_CPP})
if(MKW_PS5_RETRO_REWIND)
    list(APPEND MKW_PS5_TRANSLATED_SOURCES ${MKW_RETRO_MOD_SHARDS})
endif()
list(REMOVE_DUPLICATES MKW_PS5_TRANSLATED_SOURCES)
add_library(mkw_ps5_translated STATIC ${MKW_PS5_TRANSLATED_SOURCES})
target_link_libraries(mkw_ps5_translated PRIVATE mkw_ps5_libcxx)
target_include_directories(mkw_ps5_translated PRIVATE "${MKW_GAME_ROOT}/runtime/include" "${MKW_GAME_ROOT}")
target_compile_features(mkw_ps5_translated PRIVATE cxx_std_17)
target_compile_definitions(mkw_ps5_translated PRIVATE MKW_PLATFORM_PS5=1 TARGET_PC)
target_compile_options(mkw_ps5_translated PRIVATE -O2 -march=x86-64-v3 -fno-fast-math -ffp-contract=off -fno-slp-vectorize -w)
set_target_properties(mkw_ps5_translated PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_precompile_headers(mkw_ps5_translated PRIVATE "${MKW_GAME_ROOT}/runtime/include/mkw_pch.h")
set_property(GLOBAL APPEND PROPERTY JOB_POOLS mkw_ps5_translation=2)
# DIAGNOSTIC variant, configured in its own build tree (Compile-Game.ps1
# -FunctionProfile): translated functions call the exact function profiler
# after inlining, native calls and fiber switches report to it as well.
option(MKW_PS5_FUNCTION_PROFILE "Instrument translated functions for ps5/runtime/function_profiler_ps5.cpp" OFF)
if(MKW_PS5_FUNCTION_PROFILE)
    target_compile_options(mkw_ps5_translated PRIVATE -finstrument-functions-after-inlining)
    target_compile_definitions(mkw_ps5_translated PRIVATE MKW_PS5_FPROF=1)
    target_compile_definitions(mkw_ps5_host_context PRIVATE MKW_PS5_FPROF=1)
    target_compile_definitions(mkw_ps5_engine_memory PRIVATE MKW_PS5_FPROF=1)
    add_library(mkw_ps5_fprof OBJECT "${CMAKE_CURRENT_LIST_DIR}/../runtime/function_profiler_ps5.cpp")
    target_link_libraries(mkw_ps5_fprof PRIVATE mkw_ps5_libcxx)
    target_include_directories(mkw_ps5_fprof PRIVATE "${MKW_GAME_ROOT}/runtime/include" "${MKW_GAME_ROOT}"
        "${CMAKE_CURRENT_LIST_DIR}/../runtime")
    target_compile_features(mkw_ps5_fprof PRIVATE cxx_std_17)
    target_compile_definitions(mkw_ps5_fprof PRIVATE MKW_PLATFORM_PS5=1 TARGET_PC)
    target_compile_options(mkw_ps5_fprof PRIVATE -O2 -march=x86-64-v3 -Wall -Wextra)
    set_target_properties(mkw_ps5_fprof PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
set_property(TARGET mkw_ps5_translated PROPERTY JOB_POOL_COMPILE mkw_ps5_translation)
add_library(mkw_ps5_cpu_baseline OBJECT "${MKW_GAME_ROOT}/runtime/src/host_cpu_baseline.cpp")
target_link_libraries(mkw_ps5_cpu_baseline PRIVATE mkw_ps5_libcxx)
target_compile_features(mkw_ps5_cpu_baseline PRIVATE cxx_std_17)
set_target_properties(mkw_ps5_cpu_baseline PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The Windows-hosted translator currently writes a COFF section directive.
# Adapt that directive in the build tree only; keep all original data bytes.
file(READ "${MKW_GAME_ROOT}/generated/data_sections_init_blobs.S" MKW_BLOB_ASM)
string(REPLACE ".section .rdata,\"dr\"" ".section .rodata,\"a\",@progbits" MKW_BLOB_ASM "${MKW_BLOB_ASM}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/data_sections_init_blobs_ps5.S" "${MKW_BLOB_ASM}")
# The product descriptor lives in the game build (not mkw_ps5_runtime) so one
# runtime-build tree serves both the base and Retro Rewind game variants.
add_library(mkw_ps5_game_data OBJECT
    "${MKW_GAME_ROOT}/generated/data_sections_init.cpp"
    "${MKW_GAME_ROOT}/generated/guest_symbol_table.cpp"
    "${MKW_PS5_PRODUCT_SOURCE}"
    "${CMAKE_CURRENT_BINARY_DIR}/data_sections_init_blobs_ps5.S"
    ${MKW_PS5_GAME_DATA_ASM})
target_link_libraries(mkw_ps5_game_data PRIVATE mkw_ps5_libcxx)
target_include_directories(mkw_ps5_game_data PRIVATE "${MKW_GAME_ROOT}/runtime/include" "${MKW_GAME_ROOT}")
target_compile_features(mkw_ps5_game_data PRIVATE cxx_std_17)
target_compile_definitions(mkw_ps5_game_data PRIVATE MKW_PLATFORM_PS5=1 TARGET_PC)
set_target_properties(mkw_ps5_game_data PROPERTIES POSITION_INDEPENDENT_CODE ON)
list(LENGTH MKW_PS5_TRANSLATED_SOURCES MKW_PS5_TRANSLATED_SOURCE_COUNT)
if(NOT DEFINED MKW_RETRO_REWIND_FUNCTION_COUNT)
    set(MKW_RETRO_REWIND_FUNCTION_COUNT 0)
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/game-graph.json"
    "{\"product\":\"${MKW_PS5_PRODUCT_NAME}\",\"baseFunctions\":${MKW_BASE_FUNCTION_COUNT},\"modFunctions\":${MKW_RETRO_REWIND_FUNCTION_COUNT},\"translationUnits\":${MKW_PS5_TRANSLATED_SOURCE_COUNT},\"linkedGame\":false}\n")
