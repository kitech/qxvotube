cmake_minimum_required(VERSION 3.16)

project(qskvotube
    VERSION 0.1.0
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# QSkinny 安装位置可通过 -DCMAKE_PREFIX_PATH 覆盖
#   本机: /opt/qt/qskinny/lib/cmake
#   例:   cmake -B build -DCMAKE_PREFIX_PATH="/opt/qt/qskinny/lib/cmake"
list(APPEND CMAKE_PREFIX_PATH "/opt/qt/qskinny/lib/cmake")

find_package(Qt6 REQUIRED COMPONENTS Core Quick Widgets Network Multimedia OpenGL OpenGLWidgets)
set(QT_LIBS Qt6::Quick Qt6::Widgets Qt6::Network Qt6::Multimedia Qt6::OpenGL)

# M5-B:cxvideo(ffmpeg+mpv 双内核 + Quick 渲染器;cxvideo.cmake 提供 CXV_*)
find_package(PkgConfig REQUIRED)
pkg_check_modules(CXV_FFMPEG REQUIRED IMPORTED_TARGET
    libavformat libavcodec libavutil libswscale libswresample)
include(${CMAKE_CURRENT_SOURCE_DIR}/../cxvideo/qtav34/cxvideo.cmake)

# 兼容垫片：/opt/qt/qskinny 的导出目标引用旧命名空间 Qt::Core / Qt::CorePrivate，
# 对接系统 Qt6（Qt6::*）前先建 INTERFACE 导入目标转发即可。
if(NOT TARGET Qt::Core)
    add_library(Qt::Core INTERFACE IMPORTED)
    set_target_properties(Qt::Core PROPERTIES INTERFACE_LINK_LIBRARIES "Qt6::Core")
    add_library(Qt::CorePrivate INTERFACE IMPORTED)
    set_target_properties(Qt::CorePrivate PROPERTIES
        INTERFACE_LINK_LIBRARIES "Qt6::CorePrivate")
endif()

# QSkinny 引用不带命名空间的 Qt6Qml/Qt6Quick/Qt6OpenGL——同样垫片转发,
# 避免解析到 /usr/lib 的发行版 Qt(ABI 不匹配)
foreach(_t Qml Quick OpenGL OpenGLWidgets)
    if(NOT TARGET Qt6${_t})
        add_library(Qt6${_t} INTERFACE IMPORTED)
        set_target_properties(Qt6${_t} PROPERTIES INTERFACE_LINK_LIBRARIES "Qt6::${_t}")
    endif()
endforeach()
find_package(QSkinny REQUIRED)

# qskcomp 自绘组件库（符号链接引用，不拷贝；CI 由 scripts/pollup_deps.sh 拉取）
include(${CMAKE_CURRENT_SOURCE_DIR}/../qskcomp/qskcomp.cmake)

add_executable(qskvotube
    src/main.cpp
    ${QSKCOMP_SOURCES}
    ${CXV_SOURCES}
    ${CXV_EXTRA}
)

target_include_directories(qskvotube PRIVATE
    src
    /opt/qt/6.7.3/gcc_64/include/QtOpenGLWidgets
    /opt/qt/6.7.3/gcc_64/include/QtOpenGL
    ${QSKCOMP_INCLUDE_DIRS}
    ${CXV_INCLUDE_DIRS}
    ${Qt6Core_PRIVATE_INCLUDE_DIRS}
    ${Qt6Quick_PRIVATE_INCLUDE_DIRS}
    ${Qt6Qml_PRIVATE_INCLUDE_DIRS}
)

target_link_libraries(qskvotube PRIVATE
    ${QT_LIBS}
    Qsk::QSkinny
    Qt6::OpenGLWidgets
    PkgConfig::CXV_FFMPEG
    -Wl,-rpath,/opt/qt/6.7.3/gcc_64/lib
    GL
    ${CMAKE_DL_LIBS}   # eng_mpv dlopen libmpv
)

# cxvideo 内核门控(镜像 cxvideo.pri 的 pkg-config 探测)
target_compile_definitions(qskvotube PRIVATE
    WITH_CX_ENGINE_MPV
    WITH_CX_ENGINE_FFMPEG
    WITH_CX_RENDERER_GL
)
pkg_check_modules(CXV_PULSE libpulse-simple)
if(CXV_PULSE_FOUND)
    target_compile_definitions(qskvotube PRIVATE WITH_CX_FF_AO_PULSE)
    target_include_directories(qskvotube PRIVATE ${CXV_PULSE_INCLUDE_DIRS})
    target_link_libraries(qskvotube PRIVATE ${CXV_PULSE_LIBRARIES})
endif()
pkg_check_modules(CXV_ALSA alsa)
if(CXV_ALSA_FOUND)
    target_compile_definitions(qskvotube PRIVATE WITH_CX_FF_AO_ALSA)
    target_include_directories(qskvotube PRIVATE ${CXV_ALSA_INCLUDE_DIRS})
    target_link_libraries(qskvotube PRIVATE ${CXV_ALSA_LIBRARIES})
endif()
pkg_check_modules(CXV_SDL sdl2)
if(CXV_SDL_FOUND)
    target_compile_definitions(qskvotube PRIVATE WITH_CX_FF_AO_SDL)
    target_include_directories(qskvotube PRIVATE ${CXV_SDL_INCLUDE_DIRS})
    target_link_libraries(qskvotube PRIVATE ${CXV_SDL_LIBRARIES})
endif()
