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

# The torrent stream broker against a local swarm.
cj_add_test(tst_broker TorrentHarness.h)
target_link_libraries(tst_broker PRIVATE cj_broker cj::libtorrent)
if(WIN32)
    target_compile_definitions(tst_broker PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
set_tests_properties(tst_broker PROPERTIES TIMEOUT 300)

# Magnet links played end to end: Session, mpv, the torrent:// protocol and
# the broker, against local seeders.
cj_add_test(tst_torrentstream TorrentHarness.h)
target_link_libraries(tst_torrentstream PRIVATE cj_targets cj_transports cj_broker cj::libtorrent)
target_compile_definitions(tst_torrentstream PRIVATE CJ_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
if(WIN32)
    target_compile_definitions(tst_torrentstream PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
set_tests_properties(tst_torrentstream PROPERTIES TIMEOUT 300)

# Source adapters and the browse model, against a fake add-on server.
cj_add_test(tst_sources FakeHttpServer.h FakeTarget.h)
target_link_libraries(tst_sources PRIVATE cj_sources)

# Things picked in the browse model play in mpv: add-on web and torrent
# streams, and a local file with its subtitle.
cj_add_test(tst_browseplay FakeHttpServer.h TorrentHarness.h)
target_link_libraries(tst_browseplay PRIVATE cj_sources cj_targets cj_transports cj_broker cj::libtorrent)
target_compile_definitions(tst_browseplay PRIVATE CJ_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
if(WIN32)
    target_compile_definitions(tst_browseplay PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
set_tests_properties(tst_browseplay PROPERTIES TIMEOUT 300)

# The database: migrations, persistence, history, library, export/import;
# and the Library against the fake target.
cj_add_test(tst_storage FakeTarget.h)
target_link_libraries(tst_storage PRIVATE cj_storage cj::sqlite)

# The app's core as main() builds it, against the fake target: settings kept
# between runs, defaults, import, old settings; preferences and caches.
cj_add_test(tst_app FakeTarget.h)
target_link_libraries(tst_app PRIVATE cj_app)
