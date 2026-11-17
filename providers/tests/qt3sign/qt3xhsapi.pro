# Qt3 签名器验证工程:用 Qt 3.3.8sh qmake 构建,跑 KAT + 1000 组差分
# (证明 sign.{h,cpp} 在 Qt3 下真实可用,而非"代码写了但没编过")
TEMPLATE = app
TARGET = qt3xhsapi
DEFINES += QT3_BUILD
QMAKE_CXXFLAGS += -std=c++14
QMAKE_CFLAGS += -std=c11
QMAKE_LIBDIR += /opt/qt338sh/lib
LIBS += -L/opt/qt338sh/lib -lz

INCLUDEPATH += ../../..
INCLUDEPATH += ../..
INCLUDEPATH += ../../../../qlcomp   # md5.h / cJSON.h
INCLUDEPATH += ../../../../vendor/wjcryptlib
INCLUDEPATH += /opt/qt338sh/include

# 注意:sign.cpp / 测试均为纯 C++,不引任何 Qt 头(仅链接 libz);
# 用 Qt3 qmake 是为了证明"Qt3 工具链下可编可跑"。
SOURCES += \
    ../xhsapi.test.cpp \
    ../../xiaohongshu/api.cpp \
    ../../../../qlcomp/md5.c \
    ../../../../qlcomp/cJSON.c \
    ../../../../vendor/wjcryptlib/WjCryptLib_Rc4.c
