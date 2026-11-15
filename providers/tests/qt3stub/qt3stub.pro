# Qt3/Qt4 stub 接入验证:只 include providers.pri,检查能编译并拿到空注册表。
TEMPLATE = app
TARGET = qt3stub
CONFIG += console
CONFIG -= app_bundle
QT = core

SOURCES += main.cpp
include(../../providers.pri)
