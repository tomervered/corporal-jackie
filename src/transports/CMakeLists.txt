# Gets bytes to a playback target. Step 4: torrents into mpv through mpv's
# stream callback API. The HTTP transport for Cast comes later.
add_library(cj_transports STATIC
    TorrentTransport.cpp TorrentTransport.h
)
target_include_directories(cj_transports PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cj_transports
    PUBLIC  cj_common cj_session cj_broker Qt6::Core
    PRIVATE cj::mpv)
