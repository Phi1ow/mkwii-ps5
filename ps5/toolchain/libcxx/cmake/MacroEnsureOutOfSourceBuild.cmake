# The separate LLVM 18 libc++ archive references this legacy build guard.
# Keep the guard local instead of modifying the downloaded library sources.
macro(MACRO_ENSURE_OUT_OF_SOURCE_BUILD message_text)
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
        message(FATAL_ERROR "${message_text}")
    endif()
endmacro()
