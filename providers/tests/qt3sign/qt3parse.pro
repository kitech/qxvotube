# Qt3 离线解析/签名验证工程(fixtures 回放,不联网)
# 覆盖:douyin awemeDetail/streamsOf/searchList + acsig golden
#       + youtube guideItems/browseItems(三种 renderer)
TEMPLATE = app
TARGET = qt3parse
DEFINES += QT3_BUILD
QMAKE_CXXFLAGS += -std=c++14
QMAKE_CFLAGS += -std=c11
QMAKE_LIBDIR += /opt/qt338sh/lib
LIBS += -L/opt/qt338sh/lib

INCLUDEPATH += ../../..
INCLUDEPATH += ../..
INCLUDEPATH += ../../../../qlcomp   # cJSON.h
INCLUDEPATH += /opt/qt338sh/include

SOURCES += \
    ../qt3parse.test.cpp \
    ../../douyin/dy_parse.cpp \
    ../../douyin/acsig.cpp \
    ../../youtube/yt_parse.cpp \
    ../../../../qlcomp/cJSON.c \
    ../../../../qlcomp/compat34.cpp
