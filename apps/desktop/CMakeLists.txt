find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

qt_add_executable(nightlock-desktop
    src/main.cpp
    src/demovault.cpp
    src/iconutils.cpp
    src/models/grouptreemodel.cpp
    src/models/entrylistmodel.cpp
    src/widgets/entrydetailview.cpp
    src/widgets/entrylistdelegate.cpp
    src/widgets/totpring.cpp
    src/windows/mainwindow.cpp
    resources/resources.qrc
)

target_include_directories(nightlock-desktop PRIVATE src)
target_link_libraries(nightlock-desktop PRIVATE Qt6::Widgets nightlock-core)

set_target_properties(nightlock-desktop PROPERTIES OUTPUT_NAME nightlock)
