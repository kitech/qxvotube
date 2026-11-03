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

find_package(Qt6 REQUIRED COMPONENTS Core Quick Network Multimedia)
set(QT_LIBS Qt6::Quick Qt6::Network Qt6::Multimedia)

# 兼容垫片：/opt/qt/qskinny 的导出目标引用旧命名空间 Qt::Core / Qt::CorePrivate，
# 对接系统 Qt6（Qt6::*）前先建 INTERFACE 导入目标转发即可。
if(NOT TARGET Qt::Core)
    add_library(Qt::Core INTERFACE IMPORTED)
    set_target_properties(Qt::Core PROPERTIES INTERFACE_LINK_LIBRARIES "Qt6::Core")
    add_library(Qt::CorePrivate INTERFACE IMPORTED)
    set_target_properties(Qt::CorePrivate PROPERTIES
        INTERFACE_LINK_LIBRARIES "Qt6::CorePrivate")
endif()
find_package(QSkinny REQUIRED)

# qskcomp 自绘组件库（符号链接引用，不拷贝；CI 由 scripts/pollup_deps.sh 拉取）
include(${CMAKE_CURRENT_SOURCE_DIR}/../qskcomp/qskcomp.cmake)

add_executable(qskvotube
    src/main.cpp
    ${QSKCOMP_SOURCES}
)

target_include_directories(qskvotube PRIVATE
    src
    ${QSKCOMP_INCLUDE_DIRS}
    ${Qt6Core_PRIVATE_INCLUDE_DIRS}
    ${Qt6Quick_PRIVATE_INCLUDE_DIRS}
    ${Qt6Qml_PRIVATE_INCLUDE_DIRS}
)

target_link_libraries(qskvotube PRIVATE
    ${QT_LIBS}
    Qsk::QSkinny
)
