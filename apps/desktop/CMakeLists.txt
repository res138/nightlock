find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

qt_add_executable(nightlock-desktop
    src/main.cpp
    src/demovault.cpp
    src/entrycolors.cpp
    src/appearancesettings.cpp
    src/fonts.cpp
    src/generalsettings.cpp
    src/graphsettings.cpp
    src/hotkeys.cpp
    src/respaths.cpp
    src/standardicons.cpp
    src/touchid.hpp
    src/vaultservice.cpp
    src/models/grouptreemodel.cpp
    src/models/entrylistmodel.cpp
    src/widgets/copylabel.cpp
    src/widgets/entrydetailview.cpp
    src/widgets/entrycolorpicker.cpp
    src/widgets/entrylistdelegate.cpp
    src/widgets/frostedpanel.cpp
    src/widgets/grouptreeview.cpp
    src/widgets/icongallerypopup.cpp
    src/widgets/iconpicker.cpp
    src/widgets/nlmenu.cpp
    src/widgets/overlayscrollbar.cpp
    src/widgets/patternbackdrop.cpp
    src/widgets/patternpicker.cpp
    src/widgets/searchwindow.cpp
    src/widgets/spoilerlabel.cpp
    src/widgets/totpring.cpp
    src/windows/entryeditdialog.cpp
    src/windows/graphwindow.cpp
    src/windows/lockscreen.cpp
    src/windows/mainwindow.cpp
    src/windows/passwordgeneratorwindow.cpp
    src/windows/settingswindow.cpp
    resources/resources.qrc
)

target_include_directories(nightlock-desktop PRIVATE src)

# The P* icon packs are loaded from disk at runtime — ~190 MB is too
# much to embed as Qt resources. Dev builds read the source tree
# (these definitions); installed builds carry copies inside the
# package, which respaths prefers when they exist.
target_compile_definitions(nightlock-desktop PRIVATE
    NIGHTLOCK_VERSION="${PROJECT_VERSION}"
    NIGHTLOCK_ICONS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
    # San Francisco for non-Apple platforms is side-loaded from here
    # (Apple's license keeps the files out of the repository).
    NIGHTLOCK_FONTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts")

if(APPLE)
    target_sources(nightlock-desktop PRIVATE
        src/platform/macwindow.mm
        src/platform/touchid_mac.mm)
    set_source_files_properties(src/platform/touchid_mac.mm PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(nightlock-desktop PRIVATE
        "-framework AppKit"
        "-framework LocalAuthentication"
        "-framework Security")
else()
    target_sources(nightlock-desktop PRIVATE src/touchid.cpp)
endif()
target_link_libraries(nightlock-desktop PRIVATE Qt6::Widgets Qt6::Svg nightlock-core)

# --- application identity per platform ------------------------------
if(APPLE)
    set_source_files_properties(resources/nightlock.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    target_sources(nightlock-desktop PRIVATE resources/nightlock.icns)
    set_target_properties(nightlock-desktop PROPERTIES
        OUTPUT_NAME Nightlock
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME Nightlock
        MACOSX_BUNDLE_GUI_IDENTIFIER com.nightlock.Nightlock
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_ICON_FILE nightlock.icns)
elseif(WIN32)
    # The GUI is Nightlock.exe at the install root; the CLI keeps the
    # lowercase name inside bin\ (the PATH entry), where the
    # case-insensitive filesystem cannot clash the two.
    target_sources(nightlock-desktop PRIVATE resources/nightlock.rc)
    set_target_properties(nightlock-desktop PROPERTIES
        OUTPUT_NAME Nightlock
        WIN32_EXECUTABLE TRUE)
else()
    set_target_properties(nightlock-desktop PROPERTIES
        OUTPUT_NAME nightlock-desktop)
endif()

# --- install layout (the packaging scripts build on this) -----------
# macOS: everything inside Nightlock.app; the CLI ships as
#   Contents/Helpers/nightlock (MacOS/ would collide with "Nightlock"
#   on a case-insensitive disk) and is symlinked into PATH from there.
# Windows: GUI + resources at the root, CLI in bin\ for the PATH.
# Linux: GUI in lib/nightlock (bundled Qt lands next to it), the CLI
#   is bin/nightlock, resources under share/nightlock.
if(APPLE)
    set(NIGHTLOCK_BUNDLE_RESOURCES Nightlock.app/Contents/Resources)
    install(TARGETS nightlock-desktop BUNDLE DESTINATION .)
    install(DIRECTORY resources/icons DESTINATION ${NIGHTLOCK_BUNDLE_RESOURCES})
    install(DIRECTORY resources/fonts DESTINATION ${NIGHTLOCK_BUNDLE_RESOURCES})
    if(TARGET nightlock-cli)
        install(PROGRAMS $<TARGET_FILE:nightlock-cli>
                DESTINATION Nightlock.app/Contents/Helpers RENAME nightlock)
    endif()
elseif(WIN32)
    install(TARGETS nightlock-desktop RUNTIME DESTINATION .)
    install(DIRECTORY resources/icons DESTINATION .)
    install(DIRECTORY resources/fonts DESTINATION .)
    if(TARGET nightlock-cli)
        install(PROGRAMS $<TARGET_FILE:nightlock-cli>
                DESTINATION bin RENAME nightlock.exe)
    endif()
else()
    install(TARGETS nightlock-desktop RUNTIME DESTINATION lib/nightlock)
    install(DIRECTORY resources/icons DESTINATION share/nightlock)
    install(DIRECTORY resources/fonts DESTINATION share/nightlock)
    if(TARGET nightlock-cli)
        install(PROGRAMS $<TARGET_FILE:nightlock-cli>
                DESTINATION bin RENAME nightlock)
    endif()
    install(FILES ${CMAKE_SOURCE_DIR}/packaging/linux/nightlock.desktop
            DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/packaging/linux/icons/nightlock-512.png
            DESTINATION share/icons/hicolor/512x512/apps RENAME nightlock.png)
    install(FILES ${CMAKE_SOURCE_DIR}/packaging/linux/icons/nightlock-256.png
            DESTINATION share/icons/hicolor/256x256/apps RENAME nightlock.png)
endif()

# Qt runtime deployment at `cmake --install` time: frameworks into the
# bundle on macOS, DLLs + plugins beside the exe on Windows. On Linux
# packaging/linux/make_deb.sh bundles Qt instead — the generic deploy
# path is not battle-tested enough there.
if(APPLE OR WIN32)
    qt_generate_deploy_app_script(
        TARGET nightlock-desktop
        OUTPUT_SCRIPT nightlock_deploy_script
        # Some Qt distributions install the PDF image-format plugin without
        # the QtPdf framework it needs. Nightlock does not read PDF files.
        # windeployqt honors this filter; macdeployqt does not expose plugin
        # filtering, so make_dmg.sh removes the same unused plugin from its
        # private staging copy before validating and signing the bundle.
        EXCLUDE_PLUGINS qpdf
        # Windows Setup installs the signed Microsoft redistributable as one
        # prerequisite. Do not also copy an SDK-specific compiler runtime from
        # the build machine, and keep unused Qt translations out of artifacts.
        NO_COMPILER_RUNTIME
        NO_TRANSLATIONS
        NO_UNSUPPORTED_PLATFORM_ERROR)

    # qt_generate_deploy_app_script() defaults QT_DEPLOY_BIN_DIR to
    # CMAKE_INSTALL_BINDIR (normally "bin").  Our Windows GUI deliberately
    # lives at the install root while only the CLI lives in bin\, so accepting
    # that default puts Qt6Core.dll and qt.conf where the Windows loader cannot
    # find them.  These variables have to be set in the install-script scope,
    # immediately before Qt's generated script is included.
    if(WIN32)
        install(CODE [[
            set(QT_DEPLOY_BIN_DIR ".")
            set(QT_DEPLOY_LIB_DIR ".")
            set(QT_DEPLOY_PLUGINS_DIR "plugins")
            set(QT_DEPLOY_QML_DIR "qml")
            set(QT_DEPLOY_TRANSLATIONS_DIR "translations")
        ]])
    endif()
    install(SCRIPT ${nightlock_deploy_script})
endif()
