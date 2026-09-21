add_library(mkw_ps5_native_input STATIC
    "${CMAKE_CURRENT_LIST_DIR}/../input/native_pad.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../input/pad_sample.cpp")
target_include_directories(mkw_ps5_native_input PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../input")
target_compile_features(mkw_ps5_native_input PUBLIC cxx_std_20)
target_link_libraries(mkw_ps5_native_input PRIVATE mkw_ps5_libcxx)
target_compile_options(mkw_ps5_native_input PRIVATE -O2 -Wall -Wextra -Werror)
set_target_properties(mkw_ps5_native_input PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(mkw_ps5_virtual_pad STATIC "${CMAKE_CURRENT_LIST_DIR}/../input/virtual_pad.cpp")
target_include_directories(mkw_ps5_virtual_pad PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/../input" "${MKW_DEPS_ROOT}/SDL3-3.4.4/include")
target_compile_features(mkw_ps5_virtual_pad PUBLIC cxx_std_20)
target_link_libraries(mkw_ps5_virtual_pad PRIVATE mkw_ps5_libcxx)
target_compile_options(mkw_ps5_virtual_pad PRIVATE -O2 -Wall -Wextra -Werror)
set_target_properties(mkw_ps5_virtual_pad PROPERTIES POSITION_INDEPENDENT_CODE ON)
