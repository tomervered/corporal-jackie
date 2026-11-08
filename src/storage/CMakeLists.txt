# The app's database (Step 6): settings, add-ons, folders, library and watch
# history in one SQLite file, queried on a worker thread; and the Library the
# UI uses for history, Continue Watching and saved items.
add_library(cj_storage STATIC
    Storage.cpp Storage.h
    Library.cpp Library.h
)
target_include_directories(cj_storage PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cj_storage PUBLIC cj_common cj_session cj_sources Qt6::Core PRIVATE cj::sqlite)
