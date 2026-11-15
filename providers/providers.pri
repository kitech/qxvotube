# providers 视频源库 —— qmake 接入(Qt3/Qt4 = stub;Qt>=5 = 真实实现)
#
# 用法(在 app .pro 里):
#     include(../providers/providers.pri)
#     // C++ 侧:auto provs = qxv::createProviders();
#
# Qt3/Qt4:只编 provider_stub.cpp → createProviders() 返回空表,
#          UI 应据此隐藏"视频源"入口(能力未实现,不做假数据)。
# Qt>=5 :编全部源文件 + qlcomp/compatcore34.cpp + cJSON.c,
#          需 OpenSSL / zlib / pthread。
#
# 注意:本 .pri 不负责 cJSON.c 的重复引入;若宿主工程已编过
#       qlcomp/cJSON.c(如 qlvotube),不会重复编译(Qt>=5 分支里也不含它)。

QXV_PROVIDERS_DIR = $$PWD
QXV_QLCOMP_DIR = $$QXV_PROVIDERS_DIR/../qlcomp
INCLUDEPATH += $$QXV_PROVIDERS_DIR
INCLUDEPATH += $$QXV_PROVIDERS_DIR/common
INCLUDEPATH += $$QXV_PROVIDERS_DIR/../qlcomp   # cJSON.h / compatcore34.h

# Qt3(qmake 1.07 不定义 QT_VERSION)/Qt4 → stub;Qt>=5 → 真实实现
isEmpty(QT_VERSION) {
    # Qt3:需要 QT3_BUILD + C++11(std::function)+ Qt 头路径
    DEFINES += QT3_BUILD
    QMAKE_CXXFLAGS += -std=c++14   # Result 聚合初始化含 NSDMI(C++11 不行)
    QMAKE_CFLAGS += -std=c11
    !isEmpty(QTDIR): INCLUDEPATH += $$QTDIR/include
    SOURCES += $$QXV_PROVIDERS_DIR/common/provider_stub.cpp
    HEADERS += \
        $$QXV_PROVIDERS_DIR/common/provider.h \
        $$QXV_PROVIDERS_DIR/common/result.h \
        $$QXV_PROVIDERS_DIR/common/compat.h \
        $$QXV_PROVIDERS_DIR/common/qt3shim.h
} else {
greaterThan(QT_MAJOR_VERSION, 4) {
    # 四个 parse.cpp 同名:必须按源码路径分对象目录(否则互相覆盖)
    CONFIG += object_parallel_to_source
    DEFINES += CPPHTTPLIB_OPENSSL_SUPPORT CPPHTTPLIB_ZLIB_SUPPORT
    QT += core
    QT += gui        # compatcore34.cpp 用 QDesktopServices

    SOURCES += \
        $$QXV_PROVIDERS_DIR/common/debuglog.cpp \
        $$QXV_PROVIDERS_DIR/common/result.cpp \
        $$QXV_PROVIDERS_DIR/common/netclient.cpp \
        $$QXV_PROVIDERS_DIR/common/potoken.cpp \
        $$QXV_PROVIDERS_DIR/common/cookiejar.cpp \
        $$QXV_PROVIDERS_DIR/common/fingerprint.cpp \
        $$QXV_PROVIDERS_DIR/common/errortext.cpp \
        $$QXV_PROVIDERS_DIR/common/hgcenc/spade.cpp \
        $$QXV_PROVIDERS_DIR/common/hgcenc/mp4box.cpp \
        $$QXV_PROVIDERS_DIR/common/hgcenc/cenc.cpp \
        $$QXV_PROVIDERS_DIR/common/hgapp/hgapp.cpp \
        $$QXV_PROVIDERS_DIR/common/localstream/localstream.cpp \
        $$QXV_PROVIDERS_DIR/common/ytdlp.cpp \
        $$QXV_PROVIDERS_DIR/common/hgstream/hgstream.cpp \
        $$QXV_PROVIDERS_DIR/bilibili/bilibiliprovider.cpp \
        $$QXV_PROVIDERS_DIR/bilibili/parse.cpp \
        $$QXV_PROVIDERS_DIR/youtube/youtubeprovider.cpp \
        $$QXV_PROVIDERS_DIR/youtube/parse.cpp \
        $$QXV_PROVIDERS_DIR/hongguo/hongguoprovider.cpp \
        $$QXV_PROVIDERS_DIR/hongguo/parse.cpp \
        $$QXV_PROVIDERS_DIR/douyin/douyinprovider.cpp \
        $$QXV_PROVIDERS_DIR/douyin/parse.cpp \
        $$QXV_PROVIDERS_DIR/xiaohongshu/xiaohongshuprovider.cpp \
        $$QXV_PROVIDERS_DIR/xiaohongshu/parse.cpp \
        $$QXV_PROVIDERS_DIR/xiaohongshu/ssr.cpp \
        $$QXV_PROVIDERS_DIR/factory.cpp

    HEADERS += \
        $$QXV_PROVIDERS_DIR/common/provider.h \
        $$QXV_PROVIDERS_DIR/common/result.h \
        $$QXV_PROVIDERS_DIR/common/netclient.h \
        $$QXV_PROVIDERS_DIR/common/quality.h \
        $$QXV_PROVIDERS_DIR/common/streamcache.h \
        $$QXV_PROVIDERS_DIR/common/qcjson.h

    # qlcomp 公共件:宿主已编过时用 DEFINES += QXV_PROVIDERS_NO_QLCOMP 跳过
    !contains(DEFINES, QXV_PROVIDERS_NO_QLCOMP) {
        SOURCES += $$QXV_QLCOMP_DIR/compatcore34.cpp
        SOURCES += $$QXV_QLCOMP_DIR/cJSON.c
    }

    LIBS += -lssl -lcrypto -lz -lpthread
} else {
    # Qt4:stub(接口一致,能力均不可用)
    # compatcore34.h 在 Qt4 会引 qapplication.h(QtGui),故显式加 gui 与头路径
    QT += gui
    !isEmpty(QTDIR): INCLUDEPATH += $$QTDIR/include/QtGui
    QMAKE_CXXFLAGS += -std=c++14
    SOURCES += $$QXV_PROVIDERS_DIR/common/provider_stub.cpp
    HEADERS += \
        $$QXV_PROVIDERS_DIR/common/provider.h \
        $$QXV_PROVIDERS_DIR/common/result.h \
        $$QXV_PROVIDERS_DIR/common/compat.h \
        $$QXV_PROVIDERS_DIR/common/qt3shim.h
}
}
