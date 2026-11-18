# providers 视频源库 —— qmake 接入(Qt3/Qt4/Qt>=5 同一份真实实现)
#
# 用法(在 app .pro 里):
#     include(../providers/providers.pri)
#     // C++ 侧:auto provs = qxv::createProviders();
#
# 依赖:qlcomp(compatcore34/cJSON/md5)、qlappcom(EventPoller curl 引擎)、
#       libcurl + OpenSSL + zlib + pthread。
# 宿主已编过 eventpoller.cpp 时: DEFINES += QXV_EVENTPOLLER_EXTERNAL
# 宿主已编过 qlcomp 公共件时:   DEFINES += QXV_PROVIDERS_NO_QLCOMP

QXV_PROVIDERS_DIR = $$PWD
QXV_QLCOMP_DIR = $$QXV_PROVIDERS_DIR/../qlcomp
QXV_QLAPP_DIR = $$QXV_PROVIDERS_DIR/../qlappcom
INCLUDEPATH += $$QXV_PROVIDERS_DIR
INCLUDEPATH += $$QXV_PROVIDERS_DIR/common
INCLUDEPATH += $$QXV_QLCOMP_DIR
INCLUDEPATH += $$QXV_QLAPP_DIR

# Qt3(qmake 1.07 不定义 QT_VERSION)
isEmpty(QT_VERSION) {
    DEFINES += QT3_BUILD
    QMAKE_CXXFLAGS += -std=c++14
    QMAKE_CFLAGS += -std=c11
    !isEmpty(QTDIR): INCLUDEPATH += $$QTDIR/include
} else {
    greaterThan(QT_MAJOR_VERSION, 4) {
        QT += core
        QT += gui        # compatcore34.cpp 用 QDesktopServices
    } else {
        QT += gui        # Qt4:compatcore34.h 引 qapplication.h
        !isEmpty(QTDIR): INCLUDEPATH += $$QTDIR/include/QtGui
        QMAKE_CXXFLAGS += -std=c++14
    }
}

# 四个 parse.cpp 同名:按源码路径分对象目录
CONFIG += object_parallel_to_source

SOURCES += \
    $$QXV_PROVIDERS_DIR/common/debuglog.cpp \
    $$QXV_PROVIDERS_DIR/common/result.cpp \
    $$QXV_PROVIDERS_DIR/common/netclient.cpp \
    $$QXV_PROVIDERS_DIR/common/potoken.cpp \
    $$QXV_PROVIDERS_DIR/common/cookiejar.cpp \
    $$QXV_PROVIDERS_DIR/common/fingerprint.cpp \
    $$QXV_PROVIDERS_DIR/common/errortext.cpp \
    $$QXV_PROVIDERS_DIR/bilibili/bilibiliprovider.cpp \
    $$QXV_PROVIDERS_DIR/bilibili/bili_parse.cpp \
    $$QXV_PROVIDERS_DIR/youtube/youtubeprovider.cpp \
    $$QXV_PROVIDERS_DIR/youtube/yt_parse.cpp \
    $$QXV_PROVIDERS_DIR/hongguo/hongguoprovider.cpp \
    $$QXV_PROVIDERS_DIR/hongguo/hg_parse.cpp \
    $$QXV_PROVIDERS_DIR/douyin/douyinprovider.cpp \
    $$QXV_PROVIDERS_DIR/douyin/dy_parse.cpp \
    $$QXV_PROVIDERS_DIR/douyin/abogus.cpp \
    $$QXV_PROVIDERS_DIR/xiaohongshu/xiaohongshuprovider.cpp \
    $$QXV_PROVIDERS_DIR/xiaohongshu/xhs_parse.cpp \
    $$QXV_PROVIDERS_DIR/xiaohongshu/ssr.cpp \
    $$QXV_PROVIDERS_DIR/xiaohongshu/sign.cpp \
    $$QXV_PROVIDERS_DIR/xiaohongshu/xhs_api.cpp \
    $$QXV_PROVIDERS_DIR/factory.cpp

# 三档全编(2026-11-17 完成 Qt3/Qt4 兼容移植)
SOURCES += \
    $$QXV_PROVIDERS_DIR/common/hgcenc/spade.cpp \
    $$QXV_PROVIDERS_DIR/common/hgcenc/mp4box.cpp \
    $$QXV_PROVIDERS_DIR/common/hgcenc/cenc.cpp \
    $$QXV_PROVIDERS_DIR/common/hgapp/hgapp.cpp \
    $$QXV_PROVIDERS_DIR/common/localstream/localstream.cpp \
    $$QXV_PROVIDERS_DIR/common/ytdlp.cpp \
    $$QXV_PROVIDERS_DIR/common/hgstream/hgstream.cpp

HEADERS += \
    $$QXV_PROVIDERS_DIR/common/provider.h \
    $$QXV_PROVIDERS_DIR/common/result.h \
    $$QXV_PROVIDERS_DIR/common/netclient.h \
        $$QXV_PROVIDERS_DIR/common/ytdlp.h \
    $$QXV_PROVIDERS_DIR/common/quality.h \
    $$QXV_PROVIDERS_DIR/common/streamcache.h \
    $$QXV_PROVIDERS_DIR/common/qcjson.h

# curl 引擎(EventPoller):宿主已编过则跳过
!contains(DEFINES, QXV_EVENTPOLLER_EXTERNAL) {
    SOURCES += $$QXV_QLAPP_DIR/eventpoller.cpp
}

# qlcomp 公共件:宿主已编过时跳过
!contains(DEFINES, QXV_PROVIDERS_NO_QLCOMP) {
    SOURCES += $$QXV_QLCOMP_DIR/compatcore34.cpp
    SOURCES += $$QXV_QLCOMP_DIR/cJSON.c
    SOURCES += $$QXV_QLCOMP_DIR/md5.c
}

# xhs 签名器 RC4(公有领域,纯 C)
SOURCES += $$QXV_PROVIDERS_DIR/../vendor/wjcryptlib/WjCryptLib_Rc4.c

LIBS += -lcurl -lssl -lcrypto -lz -lpthread
