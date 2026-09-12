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
