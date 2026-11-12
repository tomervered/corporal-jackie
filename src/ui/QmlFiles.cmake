# The window and its parts, as one QML module (CorporalJackie). Used by the
# app and by tst_ui, which loads the same window against a fake player.
set(CJ_QML_FILES
    Main.qml Pages.qml SettingsPanel.qml PrivacyScreen.qml ContinueWatching.qml
    # player
    ControlBar.qml BarButton.qml LinkPopup.qml Toast.qml StatsOverlay.qml ShortcutsOverlay.qml Format.js
    # pages
    HomePage.qml SearchPage.qml DiscoverPage.qml DetailPage.qml StreamsPage.qml
    LibraryPage.qml AddonsPage.qml FoldersPage.qml
    # pieces
    Theme.qml Muted.qml Heading.qml PageTitle.qml SectionLabel.qml RowItem.qml
    PosterTile.qml PosterRow.qml NavButton.qml Chip.qml)

# Adds the files to `target` as the CorporalJackie module, each served under
# its own name (not ../ui/).
function(cj_add_ui_module target)
    set(paths "")
    foreach(file IN LISTS CJ_QML_FILES)
        set(path "${CJ_UI_DIR}/${file}")
        set_source_files_properties(${path} PROPERTIES QT_RESOURCE_ALIAS ${file})
        list(APPEND paths ${path})
    endforeach()
    set_source_files_properties(${CJ_UI_DIR}/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
    qt_add_qml_module(${target}
        URI CorporalJackie
        VERSION 1.0
        RESOURCE_PREFIX /
        QML_FILES ${paths}
        OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qml/CorporalJackie"
    )
endfunction()
set(CJ_UI_DIR "${CMAKE_CURRENT_LIST_DIR}")
