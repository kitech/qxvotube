TEMPLATE = app
TARGET = qlvotube
QT = core gui widgets
CONFIG += moc
CONFIG += sdk_no_version_check

VERSION = 0.1.0

GIT_COMMIT = $$system(git -C $$PWD rev-parse --short=7 HEAD 2>/dev/null)
isEmpty(GIT_COMMIT): GIT_COMMIT = unknown
DEFINES += GIT_COMMIT=$$GIT_COMMIT

GIT_DIRTY = $$system(git -C $$PWD status --porcelain 2>/dev/null)
!isEmpty(GIT_DIRTY): DEFINES += GIT_DIRTY

QMAKE_MACOSX_DEPLOYMENT_TARGET = 11.7

SOURCES = src/main.cpp src/mainwindow.cpp src/appearance.cpp src/config.cpp \
          ../qlcomp/cJSON.c \
          src/api.cpp \
          src/storage.cpp src/channel_db.cpp src/message_db.cpp \
          src/pending_db.cpp src/cache_db.cpp src/cache_fs.cpp \
          src/sticker_db.cpp \
          src/sidenav.cpp src/contentview.cpp src/queuepanel.cpp src/playerbar.cpp \
          src/videopane.cpp \
          src/eqdialog.cpp \
          src/userinfopanel.cpp \
          ../qlappcom/eventpoller.cpp

HEADERS = src/mainwindow.h src/appearance.h src/config.h src/globaluiutil.h \
          src/api.h src/storage.h \
          src/channel_db.h src/message_db.h src/pending_db.h \
          src/cache_db.h src/cache_fs.h src/sticker_db.h \
          src/musicmodel.h src/musicstyle.h \
          src/sidenav.h src/contentview.h src/queuepanel.h src/playerbar.h \
          src/videopane.h \
          src/eqdialog.h \
          src/userinfopanel.h \
          ../qlappcom/eventpoller.h

include(../qlcomp/qlite.pri)
# 播放引擎:cmdmpv → libmpv 探测顺序(见 docs/cxvideo-detail.md)
include(../cxvideo/qtav34/cxvideo.pri)
INCLUDEPATH += ../qlcomp
INCLUDEPATH += ../qlappcom
INCLUDEPATH += src
INCLUDEPATH += $$PWD

MOC_DIR = .
OBJECTS_DIR = .

QMAKE_CXXFLAGS += -O0
QMAKE_CFLAGS += -O0
QMAKE_CXXFLAGS += -fstack-protector-strong
QMAKE_CFLAGS += -fstack-protector-strong

contains(CONFIG, asan) {
    QMAKE_CXXFLAGS += -fsanitize=address
    QMAKE_CFLAGS += -fsanitize=address
    QMAKE_LFLAGS += -fsanitize=address
}

!win32: QMAKE_LFLAGS += -rdynamic

!isEmpty(QT_VERSION) {
    message("qlvotube: Building for Qt4+ - QT3_BUILD not defined")
    greaterThan(QT_VERSION, 5.0.0) {
        QMAKE_CXXFLAGS += -std=c++17
        QMAKE_CFLAGS += -std=c11
    } else {
        QMAKE_CXXFLAGS += -std=c++11
        QMAKE_CFLAGS += -std=c11
    }
} else {
    message("qlvotube: Building for Qt3 - adding QT3_BUILD")
    DEFINES += QT3_BUILD
    QMAKE_CXXFLAGS += -std=c++11
    QMAKE_CFLAGS += -std=c11
    QMAKE_EXE = $$system(ps -p $PPID -o args= | head -1 | awk '{print $1}')
    QTDIR_AUTO = $$system(dirname $(dirname $$QMAKE_EXE))
    isEmpty(QTDIR) {
        message("Auto QTDIR ... $$QTDIR_AUTO")
        QTDIR = $$QTDIR_AUTO
        INCLUDEPATH += $$QTDIR/include
        QMAKE_INCDIR_QT    = $$QTDIR/include
        QMAKE_LIBDIR_QT    = $$QTDIR/lib
        QMAKE_MOC          = $$QTDIR/bin/moc
        QMAKE_UIC          = $$QTDIR/bin/uic
        QMAKE_QMAKE        = $$QMAKE_EXE
        QMAKE              = $$QMAKE_EXE
        QMAKE_LRELEASE     = $$QTDIR/bin/lrelease
    }
}

FREETYPE_LIBS = $$system(pkg-config --libs freetype2 2>/dev/null)
!isEmpty(FREETYPE_LIBS) {
    QMAKE_CXXFLAGS += $$system(pkg-config --cflags freetype2 2>/dev/null)
    LIBS += -ldl $$FREETYPE_LIBS
    message("FreeType2: detected via pkg-config")
} else {
    INCLUDEPATH += /usr/include/freetype2
    LIBS += -lfreetype -ldl
    message("FreeType2: pkg-config not found, using fallback paths")
}

macx {
    INCLUDEPATH += /opt/vcpkg/installed/x64-osx-dynamic/include
    LIBS += -L/opt/vcpkg/installed/x64-osx-dynamic/lib -Wl,-rpath,/opt/vcpkg/installed/x64-osx-dynamic/lib -lhjson
    # sleepblocker.cpp 用 CFString/CFRelease(CoreFoundation);IOKit/CoreGraphics
    # 亦在 macx 链接面显式声明,避免依赖其它 .pri 的隐式注入
    LIBS += -framework CoreFoundation -framework IOKit -framework CoreGraphics
} else {
    INCLUDEPATH += /opt/vcpkg/installed/x64-linux-dynamic/include
    LIBS += -L/opt/vcpkg/installed/x64-linux-dynamic/lib -Wl,-rpath,/opt/vcpkg/installed/x64-linux-dynamic/lib -lhjson
}

unix:!macx: LIBS += -lX11
LIBS += -lcurl

# SQLite 依赖检测（照抄 qltox.pro:155-164）
SQLITE_CFLAGS = $$system(pkg-config --cflags sqlite3 2>/dev/null)
SQLITE_LIBS   = $$system(pkg-config --libs sqlite3 2>/dev/null)
isEmpty(SQLITE_LIBS) { error("sqlite3 not found - install sqlite3 dev package") }
INCLUDEPATH += $$SQLITE_CFLAGS
LIBS += $$SQLITE_LIBS
DEFINES += HAVE_SQLITE

DEFINES += EMOJI_RENDER_QT34

target.path = /usr/local/bin
INSTALLS += target

translation.path = lang
translation.files = lang/*.json
INSTALLS += translation

QMAKE_ICON = app_icon.icns
RC_FILE = app_icon.rc
