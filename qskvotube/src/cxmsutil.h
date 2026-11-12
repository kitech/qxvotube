#ifndef CXMSUTIL_H
#define CXMSUTIL_H

// 毫秒 → m:ss 文本(控制条时间标签;从 main.cpp 纯移动,便于单测)
#include <QString>

inline QString cxMsToText(long ms)
{
    long s = ms / 1000;
    return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

#endif // CXMSUTIL_H
