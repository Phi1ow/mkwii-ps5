cmake_minimum_required(VERSION 3.20)
project(mkw_ps5_platform LANGUAGES CXX ASM)

# Platform objects for the WiiCompiled port. This deliberately does not expose
# a game executable until the C++ runtime, memory backend and AGC renderer exist.
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The PS5 platform requires 64-bit Clang")
endif()

add_library(mkw_ps5_host_context OBJECT
    runtime/host_context_ps5.cpp
    runtime/context_x86_64.S)
target_include_directories(mkw_ps5_host_context PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../Wiicompiled/runtime/include")
target_compile_features(mkw_ps5_host_context PRIVATE cxx_std_17)
target_compile_definitions(mkw_ps5_host_context PRIVATE MKW_PLATFORM_PS5=1)
target_compile_options(mkw_ps5_host_context PRIVATE
    -Wall -Wextra -Werror
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
set_target_properties(mkw_ps5_host_context PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(mkw_ps5_guest_memory OBJECT runtime/guest_memory_ps5.cpp)
target_compile_features(mkw_ps5_guest_memory PRIVATE cxx_std_17)
target_compile_definitions(mkw_ps5_guest_memory PRIVATE MKW_PLATFORM_PS5=1)
target_compile_options(mkw_ps5_guest_memory PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
set_target_properties(mkw_ps5_guest_memory PROPERTIES POSITION_INDEPENDENT_CODE ON)

# A real PS5 C++ sysroot must be supplied through the compiler/toolchain before
# enabling this target. The freestanding object build above cannot provide STL.
option(MKW_PS5_BUILD_CPP_ADAPTER "Build the engine GuestFlat adapter with a supplied PS5 C++ sysroot" OFF)
if(MKW_PS5_BUILD_CPP_ADAPTER)
    get_filename_component(MKW_WORKSPACE "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
    set(MKW_PS5_CXX_BUILD "${MKW_WORKSPACE}/artifacts/libcxx-build" CACHE PATH "Built PS5 libc++ directory")
    if(NOT EXISTS "${MKW_PS5_CXX_BUILD}/lib/libc++.a" OR NOT EXISTS "${MKW_PS5_CXX_BUILD}/include/c++/v1/__config_site")
        message(FATAL_ERROR "Run Prepare-CxxToolchain.ps1 and Build-Libcxx.ps1 first")
    endif()
    add_library(mkw_ps5_libcxx STATIC IMPORTED)
    set_target_properties(mkw_ps5_libcxx PROPERTIES IMPORTED_LOCATION "${MKW_PS5_CXX_BUILD}/lib/libc++.a")
    target_include_directories(mkw_ps5_libcxx SYSTEM INTERFACE "${MKW_PS5_CXX_BUILD}/include/c++/v1")
    target_compile_options(mkw_ps5_libcxx INTERFACE -nostdinc++)
    add_library(mkw_ps5_guest_flat OBJECT runtime/guest_flat_memory_ps5.cpp)
    target_include_directories(mkw_ps5_guest_flat PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../Wiicompiled/runtime/include")
    target_compile_features(mkw_ps5_guest_flat PRIVATE cxx_std_17)
    target_compile_definitions(mkw_ps5_guest_flat PRIVATE MKW_PLATFORM_PS5=1)
    target_compile_options(mkw_ps5_guest_flat PRIVATE -Wall -Wextra -Werror)
    set_target_properties(mkw_ps5_guest_flat PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(mkw_ps5_guest_flat PRIVATE mkw_ps5_libcxx)

    # Actual engine source, compiled without diagnostic GX/module observers.
    # A future full link must supply those subsystems; this target is objects only.
    add_library(mkw_ps5_engine_memory OBJECT ../Wiicompiled/runtime/src/memory.cpp)
    add_library(mkw_ps5_cxx_platform OBJECT runtime/c_locale_ps5.cpp runtime/cxx_platform_ps5.cpp runtime/filesystem_ps5.cpp runtime/random_ps5.cpp runtime/addrinfo_ps5.cpp runtime/dns_native_ps5.cpp runtime/fast_clock_ps5.cpp)
    foreach(target mkw_ps5_engine_memory mkw_ps5_cxx_platform)
        target_link_libraries(${target} PRIVATE mkw_ps5_libcxx)
        target_include_directories(${target} PRIVATE "${MKW_WORKSPACE}/Wiicompiled/runtime/include")
        target_compile_features(${target} PRIVATE cxx_std_17)
        target_compile_definitions(${target} PRIVATE MKW_PLATFORM_PS5=1)
        set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endforeach()
endif()

option(MKW_PS5_BUILD_GAME_OBJECTS "Compile the translated base game for PS5 (not a linked executable)" OFF)
option(MKW_PS5_BUILD_RUNTIME_OBJECTS "Compile all base-game runtime sources for PS5" OFF)
# Player package (Build-ReleasePackage.ps1): compiles out the flag-file
# diagnostics (autopilot, microbench, sampler, placement, screenshots) and the
# periodic [mkw-frame-stats] line. Their code stays in the tree for dev builds.
option(MKW_PS5_RELEASE "Compile out development diagnostics for a player package" OFF)
if((MKW_PS5_BUILD_GAME_OBJECTS OR MKW_PS5_BUILD_RUNTIME_OBJECTS) AND NOT MKW_PS5_BUILD_CPP_ADAPTER)
    message(FATAL_ERROR "Game and runtime objects require MKW_PS5_BUILD_CPP_ADAPTER")
endif()
if(MKW_PS5_BUILD_GAME_OBJECTS)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/GameObjects.cmake")
endif()
if(MKW_PS5_BUILD_RUNTIME_OBJECTS)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/RuntimeObjects.cmake")
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/AuroraData.cmake")
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/AuroraFrontend.cmake")
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/NativeInput.cmake")
    add_subdirectory(input/aurora)
endif()
