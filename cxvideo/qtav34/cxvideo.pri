
# usage: include($$PWD/../cxvideo/qtav34/cxvideo.pri)
# qtav34 = cxvideo 自研 Qt3 优先实现(接口层 M0;内核 eng_mpv=M1/eng_ffmpeg=M2)

CXVIDEO = $$PWD
QLCOMP34 = $$PWD/../../qlcomp

CXVIDEO_CPP = \
    $$CXVIDEO/src/cxmpv_api.cpp \
    $$CXVIDEO/src/eng_mpv_cmd.cpp \
    $$CXVIDEO/src/cxthread.cpp \
    $$CXVIDEO/src/cxengine.cpp \
    $$CXVIDEO/src/eng_mpv.cpp \
    $$CXVIDEO/src/eng_ffmpeg.cpp \
    $$CXVIDEO/src/cxffdec.cpp \
    $$CXVIDEO/src/cxffdeca.cpp \
    $$CXVIDEO/src/cffao.cpp \
    $$CXVIDEO/src/cffao_pulse.cpp \
    $$CXVIDEO/src/cffao_alsa.cpp \
    $$CXVIDEO/src/cffao_sdl.cpp \
    $$CXVIDEO/src/cxffathread.cpp \
    $$CXVIDEO/src/cxvideoplayer.cpp \
    $$CXVIDEO/src/renderer_software.cpp \
    $$CXVIDEO/src/renderer_probe.cpp

# compatcore34 归属 qlcomp;qlite.pri 的 compat34.cpp 内部已 include 它,
# 宿主工程若已含 compat34(qlite)则不重复编,独立工程(cxvideo 自测)才补编:
BASEHOST_HAS_CORE = $$find(SOURCES, compat34)
isEmpty(BASEHOST_HAS_CORE): CXVIDEO_CPP = $$QLCOMP34/compatcore34.cpp $$CXVIDEO_CPP

CXVIDEO_HDR = \
    $$CXVIDEO/src/cxvideo_global.h \
    $$CXVIDEO/src/cxthread.h \
    $$CXVIDEO/src/cxframe.h \
    $$CXVIDEO/src/cxpqueue.h \
    $$CXVIDEO/src/cxff_wrap.h \
    $$CXVIDEO/src/cxffdec.h \
    $$CXVIDEO/src/cxffdeca.h \
    $$CXVIDEO/src/cxffclock.h \
    $$CXVIDEO/src/cffao.h \
    $$CXVIDEO/src/cxffdemux.h \
    $$CXVIDEO/src/cxffvthread.h \
    $$CXVIDEO/src/cxffathread.h \
    $$CXVIDEO/src/cxrenderer.h \
    $$CXVIDEO/src/renderer_software.h \
    $$CXVIDEO/src/renderer_probe.h \
    $$CXVIDEO/src/cxengine.h \
    $$CXVIDEO/src/eng_mpv_cmd.h \
    $$CXVIDEO/src/eng_mpv.h \
    $$CXVIDEO/src/eng_ffmpeg.h \
    $$CXVIDEO/src/cxvideoplayer.h

INCLUDEPATH += $$CXVIDEO/src $$QLCOMP34

HEADERS += $$CXVIDEO_HDR
SOURCES += $$CXVIDEO_CPP

unix {
    LIBS += -ldl
}

# 内核启用(M1/M2):
# mpv 内核走 dlopen(源内 cxmpv_api),不链 -lmpv;缺失包时
# CXEngineMpv::isAvailable() 恒 false,自动降级为"无可用内核"
DEFINES += WITH_CX_ENGINE_MPV
# FFmpeg 内核(M2):pkg-config 探测 libavformat/libavcodec/libswscale,
# 命中即编入 eng_ffmpeg/cxffdec 等,定义 WITH_CX_ENGINE_FFMPEG;
# 探测失败仅留 M1 双引擎(cxmpv dlopen 与 cmdmpv 不受影响)
FF_DEPS = $$system(pkg-config --libs libavformat libavcodec libswscale libswresample libavutil 2>/dev/null)
!isEmpty(FF_DEPS) {
    FF_CFLAGS = $$system(pkg-config --cflags libavformat libavcodec libswscale libswresample 2>/dev/null)
    QMAKE_CXXFLAGS += $$FF_CFLAGS
    # FF 库传递依赖(如 avformat→zmq→sodium;vcpkg 副本坏件 libssp 缺失)
    # 的 DT_NEEDED 解析:GNU ld 对 -L 是后追加者优先,-L 前置无效;须用
    # rpath-link,且应取 **FFmpeg 自己的 libdir**(--variable=libdir 推导,
    # 不写死路径;多架构/自定义 prefix 均随之走)。放 LIBS 内(命令行位于
    # Qt/宿主 rpath-link 之后,DBus 等先由 Qt 目录命中,不误拉)
    FF_LIBDIR = $$system(pkg-config --variable=libdir libavformat 2>/dev/null)
    !isEmpty(FF_LIBDIR): LIBS += -Wl,-rpath-link,$$FF_LIBDIR
    LIBS += $$FF_DEPS
    DEFINES += WITH_CX_ENGINE_FFMPEG

    # M2.1 音频输出后端:silent 恒编(无设备/CI 兜底);其余 pkg-config
    # 命中才编入,以 WITH_CX_FF_AO_* 门控
    FF_AO_PULSE = $$system(pkg-config --libs libpulse-simple 2>/dev/null)
    !isEmpty(FF_AO_PULSE) {
        QMAKE_CXXFLAGS += $$system(pkg-config --cflags libpulse-simple 2>/dev/null)
        LIBS += $$FF_AO_PULSE
        DEFINES += WITH_CX_FF_AO_PULSE
    }
    FF_AO_ALSA = $$system(pkg-config --libs alsa 2>/dev/null)
    !isEmpty(FF_AO_ALSA) {
        QMAKE_CXXFLAGS += $$system(pkg-config --cflags alsa 2>/dev/null)
        LIBS += $$FF_AO_ALSA
        DEFINES += WITH_CX_FF_AO_ALSA
    }
    FF_AO_SDL = $$system(pkg-config --libs sdl2 2>/dev/null)
    !isEmpty(FF_AO_SDL) {
        QMAKE_CXXFLAGS += $$system(pkg-config --cflags sdl2 2>/dev/null)
        LIBS += $$FF_AO_SDL
        DEFINES += WITH_CX_FF_AO_SDL
    }
}
