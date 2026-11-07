# Windows only: the small program at the top of the shipped folder that starts
# app\CorporalJackie.exe. See launcher.c.
enable_language(C)
add_executable(cj_launcher WIN32 launcher.c)
set_target_properties(cj_launcher PROPERTIES OUTPUT_NAME "CorporalJackie" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/launcher")
target_compile_definitions(cj_launcher PRIVATE UNICODE _UNICODE)
if(MINGW)
    target_link_options(cj_launcher PRIVATE -municode -static)
endif()
