cmake_minimum_required(VERSION 3.16)

# CI passes the tag of the release being built, e.g. -DBTEG_VERSION=1.2.3.
set(BTEG_VERSION "3.0.0" CACHE STRING "Version reported by the installer")

project(bteginstaller VERSION ${BTEG_VERSION} LANGUAGES C CXX)

# Qt Creator's code model (clangd) resolves Qt headers through this file. Without
# it every Qt type shows up as "Unknown type name" in the editor.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

include(FetchContent)
FetchContent_Declare(cpr GIT_REPOSITORY https://github.com/libcpr/cpr.git
                         GIT_TAG 1.14.1)
# cpr and its bundled libcurl default to shared libraries. Link them statically
# so the shipped installer is a single executable next to the Qt runtime instead
# of an executable plus libcpr.dll and libcurl.dll.
set(BUILD_SHARED_LIBS OFF)
FetchContent_MakeAvailable(cpr)

# The .icns is not checked in; the release workflow generates it from
# assets/logo.png with Apple's own tools. Without it the bundle simply uses the
# default icon.
set(MACOS_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/AppIcon.icns")
if(APPLE AND EXISTS "${MACOS_ICON}")
    set(MACOS_ICON_SOURCE "${MACOS_ICON}")
    set_source_files_properties("${MACOS_ICON}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

# On Windows the icon comes from a resource script compiled into the executable.
set(WINDOWS_ICON_RC "${CMAKE_CURRENT_SOURCE_DIR}/assets/app.rc")
if(WIN32 AND EXISTS "${WINDOWS_ICON_RC}"
        AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/AppIcon.ico")
    set(WINDOWS_ICON_SOURCE "${WINDOWS_ICON_RC}")
endif()

set(PROJECT_SOURCES
        main.cpp
        bteginstaller.cpp
        bteginstaller.h
        bteginstaller.ui
        canceltoken.h
        httpclient.cpp
        httpclient.h
        installercore.cpp
        installercore.h
        launchersetup.cpp
        launchersetup.h
        mcpaths.cpp
        mcpaths.h
        modpackversion.cpp
        modpackversion.h
        mrpackindex.cpp
        mrpackindex.h
        optionalmodsdialog.cpp
        optionalmodsdialog.h
        progressdialog.cpp
        progressdialog.h
        ziputil.cpp
        ziputil.h
        third_party/miniz/miniz.c
        third_party/miniz/miniz.h
        ${MACOS_ICON_SOURCE}
        ${WINDOWS_ICON_SOURCE}
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(bteginstaller
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        resources.qrc
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET bteginstaller APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(bteginstaller SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(bteginstaller
            ${PROJECT_SOURCES}
            resources.qrc
        )
    endif()
endif()

# miniz only has to read archives here. Dropping the zlib compatible names
# keeps its macros away from libcurl's zlib, and dropping stdio makes sure every
# file access goes through QFile (Unicode paths on Windows).
target_compile_definitions(bteginstaller PRIVATE
    BTEG_VERSION_STRING="${PROJECT_VERSION}"
    MINIZ_NO_ZLIB_COMPATIBLE_NAMES
    MINIZ_NO_ARCHIVE_WRITING_APIS
    MINIZ_NO_STDIO
)

target_link_libraries(bteginstaller PRIVATE cpr::cpr)
target_link_libraries(bteginstaller PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

# A stable bundle identifier is required to notarize the macOS build.
set_target_properties(bteginstaller PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER de.bte-germany.modpackinstaller
    MACOSX_BUNDLE_BUNDLE_NAME "BTE Germany Modpack Installer"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE_ICON_FILE AppIcon.icns
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

# MSVC assumes the local ANSI code page for source files; the German strings in
# this project are UTF-8 and would otherwise be mangled.
if(MSVC)
    target_compile_options(bteginstaller PRIVATE /utf-8)
endif()

include(GNUInstallDirs)
install(TARGETS bteginstaller
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(bteginstaller)
endif()
