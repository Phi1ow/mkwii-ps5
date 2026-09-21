cmake_minimum_required(VERSION 3.20)
project(mkw_ps5_libcxx LANGUAGES C CXX ASM)
add_library(mkw_sysroot_abi OBJECT "${CMAKE_CURRENT_LIST_DIR}/../../tests/sysroot_abi.cpp")
get_filename_component(MKW_WORKSPACE "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(MKW_LLVM_SOURCES "${MKW_WORKSPACE}/.tools/cxx-src")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
set(LLVM_BINARY_DIR "${CMAKE_BINARY_DIR}")
set(LLVM_LIBRARY_OUTPUT_INTDIR "${CMAKE_BINARY_DIR}/lib")
set(LIBCXX_ENABLE_SHARED OFF CACHE BOOL "" FORCE)
set(LIBCXX_ENABLE_STATIC ON CACHE BOOL "" FORCE)
set(LIBCXX_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
set(LIBCXX_INCLUDE_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(LIBCXX_INCLUDE_DOCS OFF CACHE BOOL "" FORCE)
set(LIBCXX_ENABLE_ABI_LINKER_SCRIPT OFF CACHE BOOL "" FORCE)
set(LIBCXX_ENABLE_STATIC_ABI_LIBRARY ON CACHE BOOL "" FORCE)
set(LIBCXX_STATICALLY_LINK_ABI_IN_STATIC_LIBRARY ON CACHE BOOL "" FORCE)
set(LIBCXX_CXX_ABI "libcxxabi" CACHE STRING "" FORCE)
set(LIBCXX_CXX_ABI_INCLUDE_PATHS "${MKW_LLVM_SOURCES}/libcxxabi-18.1.8.src/include" CACHE STRING "" FORCE)
set(LIBCXX_HAS_PTHREAD_API ON CACHE BOOL "" FORCE)
set(LIBCXX_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
set(LIBCXXABI_ENABLE_SHARED OFF CACHE BOOL "" FORCE)
set(LIBCXXABI_ENABLE_STATIC ON CACHE BOOL "" FORCE)
set(LIBCXXABI_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
# Cross try_compile builds archives, so check_library_exists cannot prove a
# device export. The inspected PS5 libc has no __cxa_thread_atexit_impl.
# Use LLVM's pthread-key fallback (static executable, no dlclose).
set(LIBCXXABI_HAS_CXA_THREAD_ATEXIT_IMPL OFF CACHE INTERNAL "" FORCE)
set(LLVM_ENABLE_RUNTIMES "libunwind;libcxxabi;libcxx")
set(LIBCXXABI_USE_LLVM_UNWINDER ON CACHE BOOL "" FORCE)
set(LIBCXXABI_ENABLE_STATIC_UNWINDER ON CACHE BOOL "" FORCE)
set(LIBCXXABI_STATICALLY_LINK_UNWINDER_IN_STATIC_LIBRARY ON CACHE BOOL "" FORCE)
set(LIBUNWIND_ENABLE_SHARED OFF CACHE BOOL "" FORCE)
set(LIBUNWIND_ENABLE_STATIC ON CACHE BOOL "" FORCE)
set(LIBUNWIND_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
set(LIBUNWIND_INCLUDE_DOCS OFF CACHE BOOL "" FORCE)
set(LIBUNWIND_LIBCXX_PATH "${MKW_LLVM_SOURCES}/libcxx-18.1.8.src" CACHE PATH "" FORCE)
set(LIBCXXABI_HAS_PTHREAD_API ON CACHE BOOL "" FORCE)
set(LIBCXXABI_LIBCXX_PATH "${MKW_LLVM_SOURCES}/libcxx-18.1.8.src" CACHE PATH "" FORCE)
# Bundle matching libc++abi definitions; do not mix libc++ stdexcept object
# layouts with unknown definitions from the console's library. The platform
# unwind implementation is bundled too; SharpProspero supplies the main
# module's frame index through dl_iterate_phdr. Cross-module C++ exceptions
# are not supported by that adapter and must not cross native C API boundaries.
add_subdirectory("${MKW_LLVM_SOURCES}/libunwind-18.1.8.src" libunwind)
add_subdirectory("${MKW_LLVM_SOURCES}/libcxxabi-18.1.8.src" libcxxabi)
# Full standard library facilities are retained. ABI compatibility and exports
# are audited separately before linking this archive into a native application.
add_subdirectory("${MKW_LLVM_SOURCES}/libcxx-18.1.8.src" libcxx)

# PS5 has not validated FreeBSD's private _umtx_op syscall ABI. Keep libc++'s
# portable atomic wait implementation (timed backoff) instead. Normal mutexes
# and condition variables retain the pthread path. Do not edit downloaded code.
set(MKW_ATOMIC_SOURCE "${MKW_LLVM_SOURCES}/libcxx-18.1.8.src/src/atomic.cpp")
file(READ "${MKW_ATOMIC_SOURCE}" MKW_ATOMIC_TEXT)
string(REPLACE "#elif defined(__FreeBSD__)" "#elif defined(__FreeBSD__) && !defined(MKW_PLATFORM_PS5)" MKW_ATOMIC_TEXT "${MKW_ATOMIC_TEXT}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/atomic_ps5.cpp" "${MKW_ATOMIC_TEXT}")
get_target_property(MKW_CXX_SOURCE_DIR cxx_static SOURCE_DIR)
get_target_property(MKW_CXX_SOURCES cxx_static SOURCES)
set(MKW_CXX_NATIVE_SOURCES)
foreach(source IN LISTS MKW_CXX_SOURCES)
    if(NOT IS_ABSOLUTE "${source}" AND NOT source MATCHES "^\\$<")
        set(source "${MKW_CXX_SOURCE_DIR}/${source}")
    endif()
    if(NOT source STREQUAL MKW_ATOMIC_SOURCE)
        list(APPEND MKW_CXX_NATIVE_SOURCES "${source}")
    endif()
endforeach()
set_property(TARGET cxx_static PROPERTY SOURCES "${MKW_CXX_NATIVE_SOURCES};${CMAKE_CURRENT_BINARY_DIR}/atomic_ps5.cpp")
target_compile_definitions(cxx_static PRIVATE MKW_PLATFORM_PS5=1)
# Application libkernel does not publish openat/fdopendir/unlinkat. Select
# libc++'s existing pathname implementation for platforms without that family.
target_compile_definitions(cxx_static PRIVATE REMOVE_ALL_USE_DIRECTORY_ITERATOR=1)
add_dependencies(cxx_static mkw_sysroot_abi)
