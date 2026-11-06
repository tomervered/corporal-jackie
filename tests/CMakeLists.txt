function(cj_add_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE cj_common Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

cj_add_test(tst_common)
