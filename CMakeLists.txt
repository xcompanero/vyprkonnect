cmake_minimum_required(VERSION 3.24)

project(vyprkonnect
    VERSION 0.1.0
    DESCRIPTION "Qt6 GUI client for VyprVPN on Linux (OpenVPN via NetworkManager)"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 Network)

# Optional: encrypted credential storage on KDE. Install libkf6wallet-dev and
# configure with -DHAVE_KWALLET=ON to enable.
find_package(KF6Wallet QUIET)
if(KF6Wallet_FOUND)
    message(STATUS "KWallet support enabled")
else()
    message(STATUS "KWallet NOT found — using fallback credential store (plaintext, 0600)")
endif()

qt_standard_project_setup()

qt_add_executable(vyprkonnect
    src/main.cpp
    src/core/servermodel.cpp
    src/core/credentialstore.cpp
    src/core/nmopenvpnbackend.cpp
    src/core/wanipfetcher.cpp
)

qt_add_qml_module(vyprkonnect
    URI VyprKonnect
    VERSION 1.0
    QML_FILES
        src/qml/Main.qml
        src/qml/ConnectButton.qml
)

target_include_directories(vyprkonnect PRIVATE src src/core)

target_link_libraries(vyprkonnect PRIVATE
    Qt6::Quick Qt6::QuickControls2 Qt6::Network)
if(KF6Wallet_FOUND)
    target_link_libraries(vyprkonnect PRIVATE KF6::Wallet)
    target_compile_definitions(vyprkonnect PRIVATE HAVE_KWALLET)
endif()

# Bundled server data location (dev builds point at the source tree).
target_compile_definitions(vyprkonnect PRIVATE
    VYPR_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

install(TARGETS vyprkonnect DESTINATION bin)
