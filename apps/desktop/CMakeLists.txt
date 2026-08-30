find_package(Qt6 REQUIRED COMPONENTS Widgets Svg Network)

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
    src/updatemanager.cpp
    src/vaultservice.cpp
    src/models/grouptreemodel.cpp
    src/models/entrylistmodel.cpp
    src/widgets/copylabel.cpp
    src/widgets/applicationiconpicker.cpp
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
target_link_libraries(nightlock-desktop PRIVATE
    Qt6::Widgets Qt6::Svg Qt6::Network nightlock-core)

if(NIGHTLOCK_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)

    qt_add_executable(nightlock-desktop-icon-tests
        tests/test_application_icons.cpp
        src/appearancesettings.cpp
        src/fonts.cpp
        src/respaths.cpp
        src/standardicons.cpp
        src/widgets/applicationiconpicker.cpp
        resources/resources.qrc
    )
    target_include_directories(nightlock-desktop-icon-tests PRIVATE src)
    target_compile_definitions(nightlock-desktop-icon-tests PRIVATE
        NIGHTLOCK_ICONS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
        NIGHTLOCK_FONTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts")
    target_link_libraries(nightlock-desktop-icon-tests PRIVATE
        Qt6::Widgets Qt6::Svg Qt6::Test)

    add_test(NAME desktop_app_icons COMMAND nightlock-desktop-icon-tests)
    set_tests_properties(desktop_app_icons PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(nightlock-desktop-update-tests
        tests/test_updates.cpp
        src/updatemanager.cpp
    )
    target_include_directories(nightlock-desktop-update-tests PRIVATE src)
    target_compile_definitions(nightlock-desktop-update-tests PRIVATE
        NIGHTLOCK_VERSION="${PROJECT_VERSION}"
        NIGHTLOCK_UPDATE_TESTING=1)
    target_link_libraries(nightlock-desktop-update-tests PRIVATE
        Qt6::Widgets Qt6::Network Qt6::Test)

    add_test(NAME desktop_updates COMMAND nightlock-desktop-update-tests)
    set_tests_properties(desktop_updates PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(nightlock-desktop-menu-tests
        tests/test_nlmenu.cpp
        src/appearancesettings.cpp
        src/fonts.cpp
        src/respaths.cpp
        src/widgets/frostedpanel.cpp
        src/widgets/nlmenu.cpp
        resources/resources.qrc
    )
    target_include_directories(nightlock-desktop-menu-tests PRIVATE src)
    target_compile_definitions(nightlock-desktop-menu-tests PRIVATE
        NIGHTLOCK_ICONS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
        NIGHTLOCK_FONTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts")
    target_link_libraries(nightlock-desktop-menu-tests PRIVATE
        Qt6::Widgets Qt6::Svg Qt6::Test)

    add_test(NAME desktop_menus COMMAND nightlock-desktop-menu-tests)
    set_tests_properties(desktop_menus PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# --- application identity per platform ------------------------------
if(APPLE)
    # Give icon revisions a semantic bundle filename instead of reusing the
    # historical nightlock.icns cache key. SecurityAgent renders the bundle
    # icon in legacy Keychain prompts through IconServices, whose cache may
    # otherwise keep showing artwork from an earlier build.
    set(NIGHTLOCK_MACOS_ICON_NAME NightlockPetalKeyhole.icns)
    set(NIGHTLOCK_MACOS_ICON
        "${CMAKE_CURRENT_BINARY_DIR}/${NIGHTLOCK_MACOS_ICON_NAME}")
    configure_file(resources/nightlock.icns "${NIGHTLOCK_MACOS_ICON}" COPYONLY)
    set_source_files_properties("${NIGHTLOCK_MACOS_ICON}" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    target_sources(nightlock-desktop PRIVATE "${NIGHTLOCK_MACOS_ICON}")
    set_target_properties(nightlock-desktop PROPERTIES
        OUTPUT_NAME Nightlock
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME Nightlock
        MACOSX_BUNDLE_GUI_IDENTIFIER com.nightlock.Nightlock
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_ICON_FILE "${NIGHTLOCK_MACOS_ICON_NAME}")

    # CMake's command-line generators only linker-sign the Mach-O executable.
    # Sign the complete development bundle so macOS can safely resolve the
    # bound Info.plist and current icon in SecurityAgent. The DMG pipeline
    # deploys dependencies and signs the staged bundle again afterwards.
    add_custom_command(TARGET nightlock-desktop POST_BUILD
        COMMAND /usr/bin/codesign --force --sign -
                --identifier com.nightlock.Nightlock
                "$<TARGET_BUNDLE_DIR:nightlock-desktop>"
        COMMENT "Ad-hoc signing the complete Nightlock.app bundle"
        VERBATIM)

    # Launching the executable inside the bundle directly bypasses Launch
    # Services, which can leave macOS system dialogs attached to stale bundle
    # metadata and icon caches. This development target registers the freshly
    # built bundle first, then asks Launch Services for a new app instance.
    add_custom_target(run-nightlock
        COMMAND
            /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister
            -f "$<TARGET_BUNDLE_DIR:nightlock-desktop>"
        COMMAND /usr/bin/open -n "$<TARGET_BUNDLE_DIR:nightlock-desktop>"
        DEPENDS nightlock-desktop
        COMMENT "Registering and launching the development Nightlock.app bundle"
        VERBATIM
        USES_TERMINAL)
elseif(WIN32)
    # The GUI is Nightlock.exe at the install root; the CLI keeps the
    # lowercase name inside bin\ (the PATH entry), where the
    # case-insensitive filesystem cannot clash the two. Configure the PE
    # resource so Explorer, Task Manager and installed-apps surfaces see the
    # same semantic version as the application itself.
    set(NIGHTLOCK_WINDOWS_ICON
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/nightlock.ico")
    set(NIGHTLOCK_WINDOWS_MANIFEST
        "${CMAKE_CURRENT_BINARY_DIR}/nightlock.manifest")
    configure_file(resources/nightlock.manifest.in
                   "${NIGHTLOCK_WINDOWS_MANIFEST}" @ONLY)
    # MSVC's manifest tool merges .manifest sources with linker-generated
    # dependencies. GNU-style Windows linkers instead need RT_MANIFEST in the
    # compiled resource file.
    if(MSVC)
        set(NIGHTLOCK_WINDOWS_MANIFEST_RESOURCE "")
    else()
        set(NIGHTLOCK_WINDOWS_MANIFEST_RESOURCE
            "1 RT_MANIFEST \"${NIGHTLOCK_WINDOWS_MANIFEST}\"")
    endif()
    set(NIGHTLOCK_WINDOWS_RESOURCE
        "${CMAKE_CURRENT_BINARY_DIR}/nightlock.rc")
    configure_file(resources/nightlock.rc.in
                   "${NIGHTLOCK_WINDOWS_RESOURCE}" @ONLY)
    set_source_files_properties("${NIGHTLOCK_WINDOWS_RESOURCE}" PROPERTIES
        OBJECT_DEPENDS
            "${NIGHTLOCK_WINDOWS_ICON};${NIGHTLOCK_WINDOWS_MANIFEST}")
    target_sources(nightlock-desktop PRIVATE
        "${NIGHTLOCK_WINDOWS_RESOURCE}")
    if(MSVC)
        target_sources(nightlock-desktop PRIVATE
            "${NIGHTLOCK_WINDOWS_MANIFEST}")
    endif()
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
