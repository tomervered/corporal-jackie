add_library(cj_broker STATIC
    BrokerEngine.cpp
    BrokerEngine.h
    BrokerScheduling.cpp
    BrokerTypes.h
    BrokerUpkeep.cpp
    MemoryStorage.cpp
    MemoryStorage.h
    StreamBroker.cpp
    StreamBroker.h
)
target_include_directories(cj_broker PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cj_broker PUBLIC cj_common Qt6::Core PRIVATE cj::libtorrent)
if(WIN32)
    # libtorrent's headers need these on Windows.
    target_compile_definitions(cj_broker PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
