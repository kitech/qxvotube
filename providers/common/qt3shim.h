#pragma once
// Qt3/Qt4 兼容补充(仅在 Qt<5 引用 provider 头时生效)
//
// providers 的实现只编 Qt>=5(QUrlQuery / QMetaObject::invokeMethod(lambda) /
// QDateTime::currentMSecsSinceEpoch 等 Qt5 API);Qt3/Qt4 走
// common/provider_stub.cpp,但**头文件**仍可被老 Qt 引用,因此这里补齐类型。
#ifdef QT3_BUILD
#include <qstring.h>
#include <qmap.h>
#include <qvariant.h>
#ifndef QVariantMap
typedef QMap<QString, QVariant> QVariantMap;   // Qt3 无此 typedef
#endif
#endif
