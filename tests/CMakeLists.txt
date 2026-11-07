function(cj_add_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE cj_common Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

cj_add_test(tst_common)

qt_add_executable(tst_mpvplayer tst_mpvplayer.cpp)
target_link_libraries(tst_mpvplayer PRIVATE cj_targets Qt6::Test)
target_compile_definitions(tst_mpvplayer PRIVATE CJ_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME tst_mpvplayer COMMAND tst_mpvplayer)
