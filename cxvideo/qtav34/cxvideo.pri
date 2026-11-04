
# usage: include($$PWD/../cxvideo/qtav34/cxvideo.pri)
# qtav34 = cxvideo 自研 Qt3 优先实现(接口层 M0;内核 eng_mpv=M1/eng_ffmpeg=M2)

CXVIDEO = $$PWD
QLCOMP34 = $$PWD/../../qlcomp

CXVIDEO_CPP = \
    $$CXVIDEO/src/cxthread.cpp \
    $$CXVIDEO/src/cxengine.cpp \
    $$CXVIDEO/src/eng_mpv.cpp \
    $$CXVIDEO/src/eng_ffmpeg.cpp \
    $$CXVIDEO/src/cxvideoplayer.cpp \
    $$CXVIDEO/src/renderer_software.cpp \
    $$CXVIDEO/src/renderer_probe.cpp

CXVIDEO_HDR = \
    $$CXVIDEO/src/cxvideo_global.h \
    $$CXVIDEO/src/cxthread.h \
    $$CXVIDEO/src/cxframe.h \
    $$CXVIDEO/src/cxrenderer.h \
    $$CXVIDEO/src/renderer_software.h \
    $$CXVIDEO/src/renderer_probe.h \
    $$CXVIDEO/src/cxengine.h \
    $$CXVIDEO/src/eng_mpv.h \
    $$CXVIDEO/src/eng_ffmpeg.h \
    $$CXVIDEO/src/cxvideoplayer.h

INCLUDEPATH += $$CXVIDEO/src $$QLCOMP34

HEADERS += $$CXVIDEO_HDR
SOURCES += $$CXVIDEO_CPP

# 内核探测(M1/M2 启用;缺包自动关宏降级):
# MPV_DEPS = $$system(pkg-config --libs mpv 2>/dev/null)
# !isEmpty(MPV_DEPS) {
#   LIBS += -lmpv
#   DEFINES += WITH_CX_ENGINE_MPV
# }
# FF_DEPS = $$system(pkg-config --libs libavformat libavcodec 2>/dev/null)
# !isEmpty(FF_DEPS) {
#   LIBS += $$FF_DEPS -lswresample -lswscale
#   DEFINES += WITH_CX_ENGINE_FFMPEG
# }
