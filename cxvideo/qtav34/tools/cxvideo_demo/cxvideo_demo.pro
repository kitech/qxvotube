TEMPLATE = app
TARGET = cxvideo_demo
QT = core gui widgets

CONFIG += moc
VERSION = 0.1.0

SOURCES = main.cpp

include(../../cxvideo.pri)

MOC_DIR = .
OBJECTS_DIR = .

QMAKE_CXXFLAGS += -O0
QMAKE_CFLAGS += -O0

!isEmpty(QT_VERSION) {
    message("cxvideo_demo: Building for Qt4+ (QT_VERSION=$$QT_VERSION)")
    greaterThan(QT_VERSION, 5.0.0) {
        QMAKE_CXXFLAGS += -std=c++17
        QMAKE_CFLAGS += -std=c11
    } else {
        QMAKE_CXXFLAGS += -std=c++11
        QMAKE_CFLAGS += -std=c11
    }
} else {
    message("cxvideo_demo: Building for Qt3 - adding QT3_BUILD")
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
