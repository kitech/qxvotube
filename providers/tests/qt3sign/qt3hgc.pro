# Qt3 兼容验收:hgcenc(spade/mp4box/cenc) + localstream + ytdlp 真实编译/运行
TEMPLATE = app
TARGET = qt3hgc
DEFINES += QT3_BUILD
QMAKE_CXXFLAGS += -std=c++14
QMAKE_CFLAGS += -std=c11
INCLUDEPATH += ../../..
INCLUDEPATH += ../..
INCLUDEPATH += ../../common
INCLUDEPATH += ../../../../qlcomp
INCLUDEPATH += /opt/qt338sh/include
LIBS += -L/opt/qt338sh/lib -lqt-mt -lssl -lcrypto -lz -lpthread

HEADERS += ../../common/ytdlp.h

SOURCES += \
    ../hgcompat.test.cpp \
    ../../common/hgcenc/spade.cpp \
    ../../common/hgcenc/mp4box.cpp \
    ../../common/hgcenc/cenc.cpp \
    ../../common/localstream/localstream.cpp \
    ../../common/ytdlp.cpp \
    ../../common/debuglog.cpp \
    ../../../../qlcomp/compatcore34.cpp \
    ../../../../qlcomp/cJSON.c
