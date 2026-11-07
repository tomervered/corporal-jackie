add_library(cj_session STATIC
    PlaybackTypes.h
    PlaybackTarget.h
    Session.cpp Session.h
)
target_include_directories(cj_session PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cj_session PUBLIC cj_common Qt6::Core)
