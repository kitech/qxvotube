
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
# FF_DEPS = $$system(pkg-config --libs libavformat libavcodec 2>/dev/null)
# !isEmpty(FF_DEPS) {
#   LIBS += $$FF_DEPS -lswresample -lswscale
#   DEFINES += WITH_CX_ENGINE_FFMPEG
# }
