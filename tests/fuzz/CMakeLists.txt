# libFuzzer target for the parsers of untrusted text. Needs clang and
# -DCJ_FUZZ=ON, which also builds everything with the fuzzer's coverage
# instrumentation and ASan/UBSan (see the top-level CMakeLists.txt).
#   ./fuzz_parsers -max_total_time=300 corpus-copy/ tests/fuzz/corpus
add_executable(fuzz_parsers fuzz_parsers.cpp)
target_link_libraries(fuzz_parsers PRIVATE cj_sources cj_storage cj_common Qt6::Core)
target_link_options(fuzz_parsers PRIVATE -fsanitize=fuzzer)
