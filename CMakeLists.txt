cmake_minimum_required(VERSION 3.21)

# Version: -DREPLAYER_VERSION=2.0.0 (set by the release workflow from the tag),
# else derived from the latest v* tag, e.g. 2.0.0-5-gabc1234 for 5 commits
# after v2.0.0. Without git or tags it is 0.0.0-dev.
if(NOT REPLAYER_VERSION)
    find_program(GIT_EXECUTABLE git)
    if(GIT_EXECUTABLE)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --tags --match "v[0-9]*" --dirty
            WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
            OUTPUT_VARIABLE REPLAYER_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        string(REGEX REPLACE "^v" "" REPLAYER_VERSION "${REPLAYER_VERSION}")
    endif()
    if(NOT REPLAYER_VERSION)
        set(REPLAYER_VERSION "0.0.0-dev")
    endif()
endif()

# project() only accepts the numeric part, so 1.0.0-rc1 becomes 1.0.0 there.
# The full string is used for the app version and the package names.
if(NOT REPLAYER_VERSION MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)")
    message(FATAL_ERROR "Invalid version '${REPLAYER_VERSION}', expected x.y.z[-suffix]")
endif()
project(replayer VERSION ${CMAKE_MATCH_1} LANGUAGES CXX)
message(STATUS "Replayer version: ${REPLAYER_VERSION}")

# For build.ps1 and build.sh, which put the version into the package names
file(WRITE "${CMAKE_BINARY_DIR}/version.txt" "${REPLAYER_VERSION}")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

include(GNUInstallDirs)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets Svg)

# FFmpeg 5.1 or newer (libavcodec 59.37). Found via pkg-config, or, where
# that isn't available (Windows), from an unpacked shared build given as
# FFMPEG_ROOT (with include/ and lib/, e.g. from BtbN/FFmpeg-Builds).
set(FFMPEG_ROOT "" CACHE PATH "Root of a prebuilt FFmpeg (instead of pkg-config)")
if(FFMPEG_ROOT)
    file(STRINGS "${FFMPEG_ROOT}/include/libavcodec/version_major.h"
        AVCODEC_MAJOR REGEX "#define LIBAVCODEC_VERSION_MAJOR")
    string(REGEX MATCH "[0-9]+" AVCODEC_MAJOR "${AVCODEC_MAJOR}")
    if(AVCODEC_MAJOR LESS 59)
        message(FATAL_ERROR "FFmpeg in ${FFMPEG_ROOT} is too old (libavcodec ${AVCODEC_MAJOR})")
    endif()
    add_library(ffmpeg INTERFACE)
    target_include_directories(ffmpeg INTERFACE "${FFMPEG_ROOT}/include")
    foreach(lib avcodec avformat avutil swscale)
        find_library(FFMPEG_${lib}_LIBRARY ${lib}
            PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
        target_link_libraries(ffmpeg INTERFACE "${FFMPEG_${lib}_LIBRARY}")
    endforeach()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
        libavcodec>=59.37
        libavformat
        libavutil
        libswscale
    )
    add_library(ffmpeg ALIAS PkgConfig::FFMPEG)
endif()

add_executable(replayer WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/mainwindow.cpp      src/mainwindow.h
    src/playercontroller.cpp src/playercontroller.h
    src/decoder.cpp         src/decoder.h
    src/framequeue.cpp      src/framequeue.h
    src/videoframe.cpp      src/videoframe.h
    src/videorenderer.cpp   src/videorenderer.h
    src/clipview.cpp        src/clipview.h
    src/clipref.h
    src/clipsplits.cpp      src/clipsplits.h
    src/clipmarks.cpp       src/clipmarks.h
    src/splitdialog.cpp     src/splitdialog.h
    src/markerbar.cpp       src/markerbar.h
    src/motionanalyzer.cpp  src/motionanalyzer.h
    src/clipgrid.cpp        src/clipgrid.h
    src/filebrowser.cpp     src/filebrowser.h
    src/transportbar.cpp    src/transportbar.h
    src/jumpsliderstyle.h
    src/infopanel.cpp       src/infopanel.h
    src/collapsiblesection.cpp src/collapsiblesection.h
    src/zoomnavigator.cpp   src/zoomnavigator.h
    src/viewzoom.h
    src/overlay.h
    src/annotations.cpp     src/annotations.h
    src/theme.cpp           src/theme.h
    src/timeformat.h
)

# App icon: icon.svg and icon-small.svg are the only sources. iconrender turns
# them into PNGs (sizes up to ICON_SMALL_MAX from icon-small.svg) and an ICO.
set(ICON_SIZES 16 20 24 32 40 48 64 96 128 256)
set(ICON_SMALL_MAX 32)
set(ICON_DIR "${CMAKE_CURRENT_BINARY_DIR}/icons")

add_executable(iconrender packaging/iconrender.cpp)
target_link_libraries(iconrender PRIVATE Qt6::Gui Qt6::Svg)
set_target_properties(iconrender PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/iconrender")
if(WIN32)
    # Windows finds DLLs only next to the exe or on PATH
    add_custom_command(TARGET iconrender POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:iconrender> $<TARGET_FILE_DIR:iconrender>
        COMMAND_EXPAND_LISTS)
endif()

set(ICON_PNGS "")
foreach(size ${ICON_SIZES})
    list(APPEND ICON_PNGS "${ICON_DIR}/replayer-${size}.png")
endforeach()
add_custom_command(
    OUTPUT ${ICON_PNGS} "${ICON_DIR}/replayer.ico"
    COMMAND iconrender
        "${CMAKE_CURRENT_SOURCE_DIR}/icon.svg" "${CMAKE_CURRENT_SOURCE_DIR}/icon-small.svg"
        ${ICON_SMALL_MAX} "${ICON_DIR}" ${ICON_SIZES}
    DEPENDS iconrender icon.svg icon-small.svg
    COMMENT "Rendering app icon"
    VERBATIM)

# Window icon at runtime, all sizes so Qt never has to scale
qt_add_resources(replayer "icons"
    PREFIX "/icons"
    BASE "${ICON_DIR}"
    FILES ${ICON_PNGS}
)

if(WIN32)
    # Icon for Explorer and the taskbar
    file(WRITE "${ICON_DIR}/replayer.rc" "IDI_ICON1 ICON \"replayer.ico\"\n")
    target_sources(replayer PRIVATE "${ICON_DIR}/replayer.rc")
    set_source_files_properties("${ICON_DIR}/replayer.rc" PROPERTIES
        OBJECT_DEPENDS "${ICON_DIR}/replayer.ico")
endif()

target_compile_definitions(replayer PRIVATE
    REPLAYER_VERSION="${REPLAYER_VERSION}"
    QT_NO_CAST_FROM_BYTEARRAY
    QT_DISABLE_DEPRECATED_UP_TO=0x060200
)

if(MSVC)
    target_compile_options(replayer PRIVATE /W4 /utf-8)
else()
    target_compile_options(replayer PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(replayer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGLWidgets
    ffmpeg
)

set_target_properties(replayer PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Replayer"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.replayer.Replayer"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

install(TARGETS replayer
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(UNIX AND NOT APPLE)
    install(FILES packaging/replayer.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    # The sizes icon themes (and linuxdeploy) accept; 20 and 40 are Windows-only
    foreach(size 16 24 32 48 64 96 128 256)
        install(FILES "${ICON_DIR}/replayer-${size}.png"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
            RENAME replayer.png)
    endforeach()
    install(FILES icon.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME replayer.svg)
    install(FILES LICENSE THIRD-PARTY-NOTICES.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR})
    install(DIRECTORY licenses
        DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()
