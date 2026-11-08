configure_file(AppInfo.h.in "${CMAKE_CURRENT_BINARY_DIR}/AppInfo.h" @ONLY)

add_library(cj_common STATIC
    AppPaths.cpp       AppPaths.h
    DependencyInfo.cpp DependencyInfo.h
    Logging.cpp        Logging.h
    CrashHandler.cpp   CrashHandler.h
)
target_include_directories(cj_common PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(cj_common
    PUBLIC  Qt6::Core
    PRIVATE cj::mpv cj::libtorrent cj::sqlite)
