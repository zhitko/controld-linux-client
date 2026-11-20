cmake_minimum_required(VERSION 3.16)

project(controld-tray VERSION 1.0.0 LANGUAGES CXX)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Install prefix" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

include(GNUInstallDirs)
include(CTest)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Network DBus Widgets Test)

function(controld_strict target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
endfunction()

set(CONTROLD_CORE_SOURCES
    src/core/Types.cpp
    src/core/Validation.cpp
    src/core/ControlD.cpp
    src/core/Verification.cpp
    src/core/ChromePolicy.cpp
    src/core/ChromePolicyHelper.cpp
    src/core/ProfileStore.cpp
    src/core/ProfileController.cpp
    src/core/AutostartManager.cpp
    src/core/ChromeProcesses.cpp
    src/core/AppImageLaunch.cpp
    src/core/HelperInstall.cpp
)

add_library(controld-core STATIC ${CONTROLD_CORE_SOURCES})
target_include_directories(controld-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(controld-core PUBLIC Qt6::Core Qt6::Network)
controld_strict(controld-core)

set(CONTROLD_HELPER_PATH "${CMAKE_INSTALL_FULL_LIBEXECDIR}/controld-tray-chrome-policy")

add_library(controld-dbus STATIC
    src/dbus/ResolvedMarshalling.cpp
    src/dbus/SystemdResolvedBackend.cpp
    src/dbus/NetworkManagerWatcher.cpp
    src/dbus/PkexecChromePolicyWriter.cpp
)
target_link_libraries(controld-dbus PUBLIC controld-core Qt6::DBus)
target_compile_definitions(controld-dbus PRIVATE CONTROLD_TRAY_HELPER_PATH="${CONTROLD_HELPER_PATH}")
controld_strict(controld-dbus)

add_executable(controld-tray
    src/main.cpp
    src/ui/StatusIcon.cpp
    src/ui/ProfileEditorDialog.cpp
    src/ui/SettingsWindow.cpp
    src/ui/TrayController.cpp
    src/ui/HelperInstallDialog.cpp
)
target_link_libraries(controld-tray PRIVATE controld-dbus Qt6::Widgets Qt6::Gui)
controld_strict(controld-tray)

add_executable(controld-tray-chrome-policy src/chrome_policy_helper_main.cpp)
target_link_libraries(controld-tray-chrome-policy PRIVATE controld-core)
controld_strict(controld-tray-chrome-policy)

option(CONTROLD_HELPER_PRIVATE_RPATH "Point the Chrome helper at /usr/lib/controld-tray" OFF)
if(CONTROLD_HELPER_PRIVATE_RPATH)
    set_target_properties(controld-tray-chrome-policy PROPERTIES
        INSTALL_RPATH "\$ORIGIN/../lib/controld-tray"
        BUILD_WITH_INSTALL_RPATH ON)
endif()

add_executable(controld-check-helper src/check_helper_main.cpp)
target_link_libraries(controld-check-helper PRIVATE controld-core)
controld_strict(controld-check-helper)

if(BUILD_TESTING)
    function(controld_test name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE controld-dbus Qt6::Test)
        target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        controld_strict(${name})
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    controld_test(test-validation tests/TestValidation.cpp)
    controld_test(test-profile-store tests/TestProfileStore.cpp)
    controld_test(test-chrome-policy tests/TestChromePolicy.cpp)
    controld_test(test-profile-controller tests/TestProfileController.cpp)
    controld_test(test-marshalling tests/TestMarshalling.cpp)
    controld_test(test-autostart tests/TestAutostart.cpp)
    controld_test(test-chrome-processes tests/TestChromeProcesses.cpp)
    controld_test(test-helper-install tests/TestHelperInstall.cpp)
    target_compile_definitions(test-helper-install PRIVATE
        CONTROLD_INSTALL_SCRIPT="${CMAKE_SOURCE_DIR}/packaging/install_system_files.py")
endif()

configure_file(
    packaging/com.vzhitko.controldtray.policy.in
    ${CMAKE_CURRENT_BINARY_DIR}/com.vzhitko.controldtray.policy
    @ONLY
)

install(TARGETS controld-tray RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS controld-tray-chrome-policy RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/com.vzhitko.controldtray.policy
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/polkit-1/actions)
install(FILES packaging/controld-tray.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
install(FILES packaging/controld-tray.svg
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps)
install(FILES packaging/controld-tray.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/256x256/apps)
install(FILES packaging/install_system_files.py
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/controld-tray)
install(FILES README.md LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})

set(CPACK_PACKAGE_NAME "controld-tray")
set(CPACK_PACKAGE_VENDOR "Vladimir Zhitko")
set(CPACK_PACKAGE_CONTACT "Vladimir Zhitko")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "System tray switcher for ControlD DNS profiles")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Vladimir Zhitko")
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "policykit-1, systemd, network-manager")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_GENERATOR "DEB")
include(CPack)
