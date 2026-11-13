cmake_minimum_required(VERSION 3.16)

project(qskvotube
    VERSION 0.1.0
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# QSkinny 安装位置可通过 QSKINNY_CMAKE 覆盖
#   缺省: /opt/qt/qskinny/lib/cmake(master 预编译;与 anystik 同法:
#         qt-cmake 6.7.3 构建,运行期 LD_LIBRARY_PATH 前置 6.7.3)
#   例:   QSKINNY_CMAKE=/opt/qt/qskinny.6.7/lib/cmake ./buildqt6.sh
if(DEFINED ENV{QSKINNY_CMAKE})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{QSKINNY_CMAKE}")
else()
    list(APPEND CMAKE_PREFIX_PATH "/opt/qt/qskinny/lib/cmake")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Quick Widgets Network Multimedia OpenGL OpenGLWidgets)
set(QT_LIBS Qt6::Quick Qt6::Widgets Qt6::Network Qt6::Multimedia Qt6::OpenGL)

# M5-B:cxvideo(ffmpeg+mpv 双内核 + Quick 渲染器;cxvideo.cmake 提供 CXV_*)
if(ANDROID)
    # Android:ffmpeg arm64 预编译库(build-android.sh 已 fail-fast 校验并
    # export CXV_FFMPEG_ARM64_DIR)。IMPORTED_NO_SONAME 防 DT_NEEDED
    # 记录绝对路径(APK 运行期 dlopen 必崩,qskmusic libncm 同法)。
    set(CXV_FF_DIR "$ENV{CXV_FFMPEG_ARM64_DIR}")
    if(CXV_FF_DIR STREQUAL "")
        set(CXV_FF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/android-ffmpeg/arm64-v8a")
    endif()
    add_library(CXV_FFMPEG_PREBUILT INTERFACE IMPORTED)
    target_include_directories(CXV_FFMPEG_PREBUILT INTERFACE "${CXV_FF_DIR}/include")
    target_link_directories(CXV_FFMPEG_PREBUILT INTERFACE "${CXV_FF_DIR}/lib")
    target_link_libraries(CXV_FFMPEG_PREBUILT INTERFACE
        avformat avcodec avutil swscale swresample)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CXV_FFMPEG REQUIRED IMPORTED_TARGET
        libavformat libavcodec libavutil libswscale libswresample)
endif()
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

# Android:qskinny-arm64 的导出目标把 Qt 库写成裸名(Qt6Gui_arm64-v8a 等),
# 不映射回真实目标的话链接期会变 -lQt6Gui_arm64-v8a 找不到库(qskmusic 同法)
if(ANDROID)
    macro(_fix_qskinny_abi_target old_target new_target)
        if(NOT TARGET "${old_target}" AND TARGET "${new_target}")
            add_library("${old_target}" INTERFACE IMPORTED)
            target_link_libraries("${old_target}" INTERFACE "${new_target}")
        endif()
    endmacro()
    _fix_qskinny_abi_target(Qt6Gui_arm64-v8a   Qt6::Gui)
    _fix_qskinny_abi_target(Qt6Qml_arm64-v8a   Qt6::Qml)
    _fix_qskinny_abi_target(Qt6Quick_arm64-v8a Qt6::Quick)
    _fix_qskinny_abi_target(Qt6OpenGL_arm64-v8a Qt6::OpenGL)
endif()

# qskcomp 自绘组件库（符号链接引用，不拷贝；CI 由 scripts/pollup_deps.sh 拉取）
include(${CMAKE_CURRENT_SOURCE_DIR}/../qskcomp/qskcomp.cmake)

# qt_add_executable:Android 上自动产出 lib<name>_arm64-v8a.so
# (androiddeployqt 消费;桌面仍为普通可执行)
qt_add_executable(qskvotube
    src/main.cpp
    src/page.cpp
    src/pagemanager.cpp
    src/mainpage.cpp
    src/settingspage.cpp
    src/aboutpage.cpp
    src/keepalive.cpp
    src/androidutils.cpp
    src/uistyle.cpp
    src/useravatar.cpp
    ../qlcomp/identicon.cpp
    ${QSKCOMP_SOURCES}
    ${CXV_SOURCES}
    ${CXV_EXTRA}
)

target_include_directories(qskvotube PRIVATE
    src
    /opt/qt/6.7.3/gcc_64/include/QtOpenGLWidgets
    /opt/qt/6.7.3/gcc_64/include/QtOpenGL
    ${QSKCOMP_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/../qlcomp
    ${CXV_INCLUDE_DIRS}
    ${Qt6Core_PRIVATE_INCLUDE_DIRS}
    ${Qt6Quick_PRIVATE_INCLUDE_DIRS}
    ${Qt6Qml_PRIVATE_INCLUDE_DIRS}
)

target_link_libraries(qskvotube PRIVATE
    ${QT_LIBS}
    Qsk::QSkinny
    Qt6::OpenGLWidgets
)
if(ANDROID)
    target_link_libraries(qskvotube PRIVATE CXV_FFMPEG_PREBUILT)
else()
    target_link_libraries(qskvotube PRIVATE
        PkgConfig::CXV_FFMPEG
        -Wl,-rpath,/opt/qt/6.7.3/gcc_64/lib
        -L/opt/qt/6.7.3/gcc_64/lib # 未版本化 -lQt6* 须先命中 6.7.3(anystik 同法)
        GL
        ${CMAKE_DL_LIBS}   # eng_mpv dlopen libmpv
    )
endif()
# Android 也需要 dlopen(libmpv/libssl 等)与 EGL 系统库
if(ANDROID)
    target_link_libraries(qskvotube PRIVATE ${CMAKE_DL_LIBS})
endif()

# cxvideo 内核门控(镜像 cxvideo.pri 的 pkg-config 探测)
target_compile_definitions(qskvotube PRIVATE
    WITH_CX_ENGINE_MPV
    WITH_CX_ENGINE_FFMPEG
    WITH_CX_RENDERER_GL
)
# 音频输出后端(桌面 pkg-config 探测;ANDROID 跳过——宿主 pkg-config
# 会探到 x86-64 库污染交叉链接;Android 走 CX_FF_AO=silent 兜底)
if(NOT ANDROID)
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
endif() # NOT ANDROID
