cmake_minimum_required(VERSION 3.24)

project(vyprkonnect
    VERSION 0.1.0
    DESCRIPTION "Qt6 GUI client for VyprVPN on Linux (OpenVPN via NetworkManager)"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 Network DBus)

qt_standard_project_setup()

qt_add_executable(vyprkonnect
    src/main.cpp
)

qt_add_qml_module(vyprkonnect
    URI VyprKonnect
    VERSION 1.0
    QML_FILES
        src/qml/Main.qml
        src/qml/ConnectButton.qml
)

target_link_libraries(vyprkonnect PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Network Qt6::DBus)

install(TARGETS vyprkonnect DESTINATION bin)
