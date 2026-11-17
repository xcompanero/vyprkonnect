cmake_minimum_required(VERSION 3.24)

project(vyprkonnect
    VERSION 0.2.3
    DESCRIPTION "Qt6 GUI client for VyprVPN on Linux (OpenVPN via NetworkManager)"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 Network Widgets)

# Optional: encrypted credential storage on KDE. Install libkf6wallet-dev and
# configure with -DHAVE_KWALLET=ON to enable.
find_package(KF6Wallet QUIET)
if(KF6Wallet_FOUND)
    message(STATUS "KWallet support enabled")
else()
    message(STATUS "KWallet NOT found — using fallback credential store (plaintext, 0600)")
endif()

qt_standard_project_setup(REQUIRES 6.10)

qt_add_executable(vyprkonnect
    src/main.cpp
    src/core/nmbackendbase.cpp
    src/core/servermodel.cpp
    src/core/credentialstore.cpp
    src/core/nmopenvpnbackend.cpp
    src/core/wanipfetcher.cpp
    src/core/nmwireguardbackend.cpp
)

qt_add_qml_module(vyprkonnect
    URI VyprKonnect
    VERSION 1.0
    QML_FILES
        src/qml/Main.qml
        src/qml/ConnectButton.qml
)

target_include_directories(vyprkonnect PRIVATE src src/core)

# ---- Installation & packaging ----
include(GNUInstallDirs)
install(TARGETS vyprkonnect DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY data/ DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/vyprkonnect/data)
install(FILES packaging/vyprkonnect.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
# NM dispatcher hook: prevents the WG encapsulation loop (see script header).
install(PROGRAMS packaging/nm-dispatcher/90-vyprkonnect-wg
        DESTINATION /etc/NetworkManager/dispatcher.d)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/vyprkonnect)

set(CPACK_PACKAGE_NAME "vyprkonnect")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "VyprKonnect project")
set(CPACK_PACKAGE_CONTACT "Aidan <aidan@localhost>") # adjust before publishing
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Qt6 GUI client for VyprVPN on Linux (OpenVPN via NetworkManager)")
set(CPACK_PACKAGE_DESCRIPTION
    "Unaffiliated free-software GUI for managing VyprVPN OpenVPN connections
through NetworkManager. Bundles the official server location set; credentials
are stored in KWallet. Not a product of Golden Frog / VyprVPN / Certida.")
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libqt6quick6, libqt6quickcontrols2-6, libqt6network6, libqt6qml6, qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-templates, qml6-module-qtquick-window, network-manager-openvpn, kwallet6")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR "DEB")
include(CPack)

target_link_libraries(vyprkonnect PRIVATE
    Qt6::Quick Qt6::QuickControls2 Qt6::Network Qt6::DBus Qt6::Widgets)
if(KF6Wallet_FOUND)
    target_link_libraries(vyprkonnect PRIVATE KF6::Wallet)
    target_compile_definitions(vyprkonnect PRIVATE HAVE_KWALLET)
endif()

# Bundled server data location (dev builds point at the source tree).
target_compile_definitions(vyprkonnect PRIVATE
    VYPR_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

