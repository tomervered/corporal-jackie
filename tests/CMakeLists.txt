function(cj_add_test name)
    qt_add_executable(${name} ${name}.cpp ${ARGN})
    target_link_libraries(${name} PRIVATE cj_common Qt6::Test)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

cj_add_test(tst_common)

# Session against the fake target: no mpv needed.
cj_add_test(tst_session FakeTarget.h)
target_link_libraries(tst_session PRIVATE cj_session)

# The real mpv target, headless, on a bundled clip.
cj_add_test(tst_mpvtarget)
target_link_libraries(tst_mpvtarget PRIVATE cj_targets)
target_compile_definitions(tst_mpvtarget PRIVATE CJ_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
