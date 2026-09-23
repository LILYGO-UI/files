cmake_minimum_required(VERSION 3.21)

include(cmake/LpmMetadata.cmake)
project(${LILYGO_UI_CMAKE_PROJECT_NAME} LANGUAGES C CXX)

include(GNUInstallDirs)
include(CTest)
find_package(LilyGoUI CONFIG REQUIRED)

configure_file(cmake/app_identity.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/app_identity.h @ONLY)

add_library(lilygo_ui_files_core STATIC
    src/app_config.cpp
    src/pages/files/files_model.cpp)
target_include_directories(lilygo_ui_files_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_options(lilygo_ui_files_core PRIVATE -Wall -Wextra -Wpedantic)
target_compile_features(lilygo_ui_files_core PUBLIC cxx_std_17)
if(CM0_SIMULATOR)
    target_compile_definitions(lilygo_ui_files_core PRIVATE
        FILES_SIMULATED_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/simulated-home")
endif()

add_library(lilygo_ui_files_ui STATIC
    src/app.cpp
    src/components/components.cpp
    src/pages/files/files_view.cpp
    src/pages/files/files_view_model.cpp)
target_include_directories(lilygo_ui_files_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_options(lilygo_ui_files_ui PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(lilygo_ui_files_ui PUBLIC
    lilygo_ui_files_core LilyGoUI::Runtime)

add_executable(lilygo_ui_files src/main.cpp)
target_compile_options(lilygo_ui_files PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(lilygo_ui_files PRIVATE lilygo_ui_files_ui)
set_target_properties(lilygo_ui_files PROPERTIES
    OUTPUT_NAME "${LILYGO_UI_EXECUTABLE_NAME}")

file(RELATIVE_PATH LILYGO_UI_LAUNCHER_EXECUTABLE_DIR
    "${CMAKE_INSTALL_FULL_DATAROOTDIR}/launcher/apps"
    "${CMAKE_INSTALL_FULL_BINDIR}")
configure_file(data/launcher.json.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.json @ONLY)
configure_file(data/application.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.desktop @ONLY)
configure_file(data/application.metainfo.xml.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.metainfo.xml @ONLY)
configure_file(data/conffiles.in
    ${CMAKE_CURRENT_BINARY_DIR}/conffiles @ONLY)

install(TARGETS lilygo_ui_files RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT app)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.json
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/launcher/apps COMPONENT app)
lilygo_ui_install_launcher_icon(
    "${LILYGO_UI_APP_ID}"
    "${CMAKE_CURRENT_SOURCE_DIR}/${LILYGO_UI_APP_ICON}")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications COMPONENT app)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo COMPONENT app)
install(FILES data/application.conf
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/lilygo/ui
    RENAME ${LILYGO_UI_COMPONENT_SLUG}.conf COMPONENT app)
lilygo_ui_install_font_licenses(
    "${CMAKE_INSTALL_DATAROOTDIR}/doc/${LILYGO_UI_PACKAGE_NAME}" app)

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(files-core-tests tests/test_core.cpp)
    target_compile_options(files-core-tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(files-core-tests PRIVATE lilygo_ui_files_core)
    add_test(NAME files-core COMMAND files-core-tests)

    add_executable(files-view-model-tests tests/test_view_model.cpp)
    target_compile_options(files-view-model-tests PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(files-view-model-tests PRIVATE lilygo_ui_files_ui)
    add_test(NAME files-view-model COMMAND files-view-model-tests)

    add_executable(files-render-ui tests/render_ui.cpp)
    target_compile_options(files-render-ui PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(files-render-ui PRIVATE lilygo_ui_files_ui)
    foreach(viewport IN ITEMS portrait landscape)
        if(viewport STREQUAL "portrait")
            set(_width 568)
            set(_height 1232)
        else()
            set(_width 1232)
            set(_height 568)
        endif()
        foreach(scenario IN ITEMS home folder search preview more new-folder)
            add_test(NAME files-render-${viewport}-${scenario}
                COMMAND files-render-ui
                    ${_width} ${_height} ${scenario}
                    ${CMAKE_CURRENT_BINARY_DIR}/files-${viewport}-${scenario}.ppm)
        endforeach()
    endforeach()

    foreach(viewport IN ITEMS portrait landscape compact large)
        if(viewport STREQUAL "compact")
            set(_width 320)
            set(_height 568)
        elseif(viewport STREQUAL "large")
            set(_width 1024)
            set(_height 768)
        elseif(viewport STREQUAL "portrait")
            set(_width 568)
            set(_height 1232)
        else()
            set(_width 1232)
            set(_height 568)
        endif()
        add_test(NAME files-render-${viewport}-boundaries
            COMMAND files-render-ui
                ${_width} ${_height} boundaries
                ${CMAKE_CURRENT_BINARY_DIR}/files-${viewport}-boundaries.ppm)
    endforeach()
endif()

set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME "${LILYGO_UI_PACKAGE_NAME}")
set(CPACK_PACKAGE_VERSION "${LILYGO_UI_VERSION}")
if(LILYGO_UI_VERSION MATCHES
   "^([0-9]+:)?([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(CPACK_PACKAGE_VERSION_MAJOR "${CMAKE_MATCH_2}")
    set(CPACK_PACKAGE_VERSION_MINOR "${CMAKE_MATCH_3}")
    set(CPACK_PACKAGE_VERSION_PATCH "${CMAKE_MATCH_4}")
else()
    set(CPACK_PACKAGE_VERSION_MAJOR 0)
    set(CPACK_PACKAGE_VERSION_MINOR 0)
    set(CPACK_PACKAGE_VERSION_PATCH 0)
endif()
set(CPACK_PACKAGE_CONTACT "${LILYGO_UI_PACKAGE_CONTACT}")
set(CPACK_PACKAGE_VENDOR "${LILYGO_UI_PACKAGE_CONTACT}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${LILYGO_UI_APP_SUMMARY}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${LILYGO_UI_APP_HOMEPAGE}")
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_SET_DESTDIR ON)
set(CPACK_COMPONENTS_ALL app)
set(CPACK_DEBIAN_APP_PACKAGE_NAME "${LILYGO_UI_PACKAGE_NAME}")
set(CPACK_DEBIAN_APP_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_APP_PACKAGE_DEPENDS
    "libc6, libstdc++6, libdrm2, libfreetype6, lilygo-ui-appkit-dev (>= ${LILYGO_UI_MIN_APPKIT_VERSION})")
if(LILYGO_UI_GRAPHICS_DEBIAN_DEPENDS)
    string(APPEND CPACK_DEBIAN_APP_PACKAGE_DEPENDS
        ", ${LILYGO_UI_GRAPHICS_DEBIAN_DEPENDS}")
endif()
set(CPACK_DEBIAN_APP_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_APP_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_BINARY_DIR}/conffiles")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
if(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE
        "${LILYGO_UI_PACKAGE_ARCHITECTURE}")
endif()
include(CPack)
