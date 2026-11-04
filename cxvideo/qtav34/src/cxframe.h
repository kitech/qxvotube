#ifndef CXFRAME_H
#define CXFRAME_H

#include "cxvideo_global.h"
#include <qimage.h>

// 帧定义(第 5 节;指针传递避免深拷贝)
class CXVideoFrame {
public:
    CXVideoFrame() : ptsMs(0), width(0), height(0), isKey(false) { }

    long ptsMs;          // 毫秒
    int width;
    int height;
    QImage img;          // 首版统一 RGB32(sws 已转好)
    bool isKey;
};

#endif // CXFRAME_H
