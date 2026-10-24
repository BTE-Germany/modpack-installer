cmake_minimum_required(VERSION 3.16)

# CI passes the tag of the release being built, e.g. -DBTEG_VERSION=1.2.3.
set(BTEG_VERSION "1.0.0" CACHE STRING "Version reported by the installer")

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
# Since curl 8.13 cpr pulls in libpsl, which it builds with meson - an extra
# build dependency nobody has by default. libpsl only backs curl's cookie
# domain checks and this installer never sends a cookie, so it is turned off.
set(CPR_CURL_USE_LIBPSL OFF)
# Use native Windows crypto backend (Schannel) so libcurl utilizes the OS trust store
if(WIN32)
    set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
endif()

# libcurl only inflates Content-Encoding when linked against zlib. Prefer the
# system library (Linux/macOS); Windows CI has none, so curl was previously
# built there without gzip. cpr fetches zlib-ng but never exposes ZLIB::ZLIB
# for curl's find_package, so we fall back to a static zlib-ng build.
# Drop stale FindZLIB cache entries that point at missing paths (or a bare
# target name treated as a filename) so a broken prior configure cannot stick.
foreach(_bteg_zlib_var ZLIB_LIBRARY ZLIB_LIBRARY_RELEASE ZLIB_LIBRARY_DEBUG)
    if(DEFINED CACHE{${_bteg_zlib_var}} AND NOT EXISTS "${${_bteg_zlib_var}}")
        unset(${_bteg_zlib_var} CACHE)
    endif()
endforeach()
if(ZLIB_INCLUDE_DIR AND NOT EXISTS "${ZLIB_INCLUDE_DIR}/zlib.h")
    unset(ZLIB_INCLUDE_DIR CACHE)
endif()
unset(_bteg_zlib_var)

find_package(ZLIB QUIET)
if(NOT ZLIB_FOUND)
    # Record the archive path FindZLIB expects; it must match what zlib-ng
    # actually produces (libz.a with MinGW, zlibstatic.lib with MSVC).
    set(ZLIB_COMPAT ON CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF)
    unset(ZLIB_ENABLE_TESTS CACHE)
    FetchContent_Declare(zlib
        GIT_REPOSITORY https://github.com/zlib-ng/zlib-ng
        GIT_TAG 2.3.3
    )
    FetchContent_MakeAvailable(zlib)
    if(NOT TARGET zlib-ng)
        message(FATAL_ERROR "zlib-ng did not produce its static library target")
    endif()
    # Parent CMAKE_AUTOMOC would otherwise attach a moc step to this C library.
    set_target_properties(zlib-ng PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

    set(ZLIB_INCLUDE_DIR "${zlib_BINARY_DIR}" CACHE PATH "" FORCE)
    if(MSVC)
        set(_bteg_zlib_lib "${zlib_BINARY_DIR}/zlibstatic.lib")
    else()
        set(_bteg_zlib_lib "${zlib_BINARY_DIR}/libz.a")
    endif()
    set(ZLIB_LIBRARY "${_bteg_zlib_lib}" CACHE FILEPATH "" FORCE)
    unset(_bteg_zlib_lib)
    unset(ZLIB_LIBRARY_RELEASE CACHE)
    unset(ZLIB_LIBRARY_DEBUG CACHE)
endif()
set(CURL_ZLIB ON CACHE BOOL "" FORCE)

set(BUILD_TESTING OFF)
FetchContent_MakeAvailable(cpr)
unset(ZLIB_ENABLE_TESTS CACHE)

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
        installmanifest.cpp
        installmanifest.h
        launchersetup.cpp
        launchersetup.h
        mcpaths.cpp
        mcpaths.h
        modpackversion.cpp
        modpackversion.h
        modrinthapi.cpp
        modrinthapi.h
        mrpackindex.cpp
        mrpackindex.h
        optionalmodsdialog.cpp
        optionalmodsdialog.h
        pathsdialog.cpp
        pathsdialog.h
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

