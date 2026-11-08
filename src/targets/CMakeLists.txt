find_package(Qt6 REQUIRED COMPONENTS OpenGL Quick)

add_library(cj_targets STATIC
    MpvTarget.cpp    MpvTarget.h
    MpvVideoItem.cpp MpvVideoItem.h
    SleepBlocker.cpp SleepBlocker.h
)
target_include_directories(cj_targets PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cj_targets
    PUBLIC  cj_common cj_session Qt6::Core Qt6::Quick Qt6::OpenGL
    PRIVATE cj::mpv)

if(APPLE)
    target_link_libraries(cj_targets PRIVATE "-framework IOKit" "-framework CoreFoundation")
endif()
