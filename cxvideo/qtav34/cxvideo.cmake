# cxvideo 库源(Qt6/Quick 域;qskvotube 等非 qmake 工程消费;M5-B)
# 用法: include(${CMAKE_CURRENT_SOURCE_DIR}/../cxvideo/qtav34/cxvideo.cmake)
#       宿主将 ${CXV_SOURCES} ${CXV_EXTRA} 加入 target,
#       include ${CXV_INCLUDE_DIRS},链接 ffmpeg(PkgConfig)+GL+DL。

set(CXV_DIR ${CMAKE_CURRENT_LIST_DIR})

set(CXV_SOURCES
    ${CXV_DIR}/src/cxmpv_api.cpp
    ${CXV_DIR}/src/cxthread.cpp
    ${CXV_DIR}/src/cxengine.cpp
    ${CXV_DIR}/src/eng_mpv.cpp
    ${CXV_DIR}/src/eng_mpv_cmd.cpp
    ${CXV_DIR}/src/eng_ffmpeg.cpp
    ${CXV_DIR}/src/cxffdec.cpp
    ${CXV_DIR}/src/cxffdeca.cpp
    ${CXV_DIR}/src/cffao.cpp
    ${CXV_DIR}/src/cffao_pulse.cpp
    ${CXV_DIR}/src/cffao_alsa.cpp
    ${CXV_DIR}/src/cffao_sdl.cpp
    ${CXV_DIR}/src/cxffathread.cpp
    ${CXV_DIR}/src/cffsub.cpp
    ${CXV_DIR}/src/cffhw.cpp
    ${CXV_DIR}/src/cxvideoplayer.cpp
    ${CXV_DIR}/src/renderer_mpvgl.cpp
    ${CXV_DIR}/src/renderer_quick.cpp
    ${CXV_DIR}/src/renderer_qmpv.cpp
)
set(CXV_INCLUDE_DIRS ${CXV_DIR}/src ${CXV_DIR}/../../qlcomp)

# eng_mpv_cmd 独占 qlcomp 依赖,Quick 域不编(cmdmpv 内核不参与)
# compatcore34 归 qlcomp;qskvotube 不消费 qlite/compat34,补编一份:
set(CXV_EXTRA ${CXV_DIR}/../../qlcomp/compatcore34.cpp)
