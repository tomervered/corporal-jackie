# Source adapters: Stremio add-ons and local folders (Step 5), and the
# browse panel's model. Network access through Qt Network only.
add_library(cj_sources STATIC
    SourceTypes.h SourceAdapter.h Mailbox.h
    StremioProtocol.cpp StremioProtocol.h
    StreamRanking.cpp StreamRanking.h
    JsonFetcher.cpp JsonFetcher.h
    StremioAdapter.cpp StremioAdapter.h
    LocalNames.cpp LocalNames.h
    LocalLibrary.cpp LocalLibrary.h
    Browser.cpp Browser.h
)
target_include_directories(cj_sources PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cj_sources PUBLIC cj_common cj_session Qt6::Core Qt6::Network)
