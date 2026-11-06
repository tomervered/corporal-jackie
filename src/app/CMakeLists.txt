qt_add_executable(corporal-jackie WIN32 MACOSX_BUNDLE main.cpp)

set_source_files_properties(../ui/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)

qt_add_qml_module(corporal-jackie
    URI CorporalJackie
    VERSION 1.0
    RESOURCE_PREFIX /
    QML_FILES ../ui/Main.qml
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qml/CorporalJackie"
)

set_target_properties(corporal-jackie PROPERTIES
    OUTPUT_NAME                    "CorporalJackie"
    MACOSX_BUNDLE_BUNDLE_NAME      "${CJ_APP_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER   "${CJ_BUNDLE_ID}"
    MACOSX_BUNDLE_BUNDLE_VERSION   "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

target_link_libraries(corporal-jackie PRIVATE cj_common Qt6::Gui Qt6::Qml Qt6::Quick)

install(TARGETS corporal-jackie
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION bin)
