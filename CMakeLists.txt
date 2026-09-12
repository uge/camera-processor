cmake_minimum_required(VERSION 3.16)
project(camera_processor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(TURBOJPEG REQUIRED libturbojpeg)

add_executable(camera-processor
    src/main.cpp
    src/v4l2_capture.h
    src/v4l2_capture.cpp
    src/v4l2_output.h
    src/v4l2_output.cpp
    src/curve_editor.h
    src/curve_editor.cpp
    src/crop_overlay.h
    src/crop_overlay.cpp
    src/gl_processor.h
    src/gl_processor.cpp
    src/main_window.h
    src/main_window.cpp
)

target_include_directories(camera-processor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${TURBOJPEG_INCLUDE_DIRS}
)

target_link_libraries(camera-processor PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    ${TURBOJPEG_LIBRARIES}
    GL
    pthread
)

install(TARGETS camera-processor DESTINATION bin)
install(FILES README.md LICENSE DESTINATION share/doc/camera-processor)
install(FILES resources/camera-processor.desktop DESTINATION share/applications)

set(CPACK_PACKAGE_NAME "camera-processor")
set(CPACK_PACKAGE_VENDOR "uge")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Real-Time GPU Camera Processor & V4L2 Virtual Device")
set(CPACK_PACKAGE_VERSION_MAJOR "1")
set(CPACK_PACKAGE_VERSION_MINOR "0")
set(CPACK_PACKAGE_VERSION_PATCH "0")
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "uge")
set(CPACK_DEBIAN_PACKAGE_SECTION "video")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6core6, libqt6gui6, libqt6widgets6, libqt6opengl6, libqt6openglwidgets6, libturbojpeg0, libgl1, v4l2loopback-dkms | linux-modules-v4l2loopback-generic")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "v4l-utils, pkexec, v4l2loopback-utils")

include(CPack)
