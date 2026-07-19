find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

qt_add_executable(nightlock-desktop
    src/main.cpp
    src/demovault.cpp
    src/standardicons.cpp
    src/models/grouptreemodel.cpp
    src/models/entrylistmodel.cpp
    src/widgets/entrydetailview.cpp
    src/widgets/entrylistdelegate.cpp
    src/widgets/frostedpanel.cpp
    src/widgets/grouptreeview.cpp
    src/widgets/icongallerypopup.cpp
    src/widgets/iconpicker.cpp
    src/widgets/nlmenu.cpp
    src/widgets/scrollbarfader.cpp
    src/widgets/spoilerlabel.cpp
    src/widgets/totpring.cpp
    src/windows/entryeditdialog.cpp
    src/windows/mainwindow.cpp
    resources/resources.qrc
)

target_include_directories(nightlock-desktop PRIVATE src)

# The P* icon packs are loaded from disk at runtime — ~190 MB is too
# much to embed as Qt resources. Dev builds read the source tree.
target_compile_definitions(nightlock-desktop PRIVATE
    NIGHTLOCK_ICONS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources/icons")

if(APPLE)
    target_sources(nightlock-desktop PRIVATE src/platform/macwindow.mm)
    target_link_libraries(nightlock-desktop PRIVATE "-framework AppKit")
endif()
target_link_libraries(nightlock-desktop PRIVATE Qt6::Widgets Qt6::Svg nightlock-core)

set_target_properties(nightlock-desktop PROPERTIES OUTPUT_NAME nightlock)
