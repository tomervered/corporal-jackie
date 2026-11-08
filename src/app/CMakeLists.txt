# Everything but the window, as a library the tests link too.
add_library(cj_app STATIC AppCore.cpp AppCore.h Preferences.cpp Preferences.h Caches.cpp Caches.h)
target_include_directories(cj_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cj_app PUBLIC cj_common cj_session cj_targets cj_transports cj_sources cj_storage
                      Qt6::Network Qt6::Qml)

qt_add_executable(corporal-jackie WIN32 MACOSX_BUNDLE main.cpp)

set_source_files_properties(../ui/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
set_source_files_properties(../ui/Pages.qml PROPERTIES QT_RESOURCE_ALIAS Pages.qml)
set_source_files_properties(../ui/SettingsPanel.qml PROPERTIES QT_RESOURCE_ALIAS SettingsPanel.qml)
set_source_files_properties(../ui/ContinueWatching.qml PROPERTIES QT_RESOURCE_ALIAS ContinueWatching.qml)

qt_add_qml_module(corporal-jackie
    URI CorporalJackie
    VERSION 1.0
    RESOURCE_PREFIX /
    QML_FILES ../ui/Main.qml ../ui/Pages.qml ../ui/SettingsPanel.qml ../ui/ContinueWatching.qml
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qml/CorporalJackie"
)

set_target_properties(corporal-jackie PROPERTIES
    OUTPUT_NAME                    "CorporalJackie"
    MACOSX_BUNDLE_BUNDLE_NAME      "${CJ_APP_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER   "${CJ_BUNDLE_ID}"
    MACOSX_BUNDLE_BUNDLE_VERSION   "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

target_link_libraries(corporal-jackie PRIVATE cj_app Qt6::Gui Qt6::Qml Qt6::Quick)

if(WIN32)
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
    target_link_libraries(corporal-jackie PRIVATE Qt6::GuiPrivate)
endif()

install(TARGETS corporal-jackie
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION bin)
