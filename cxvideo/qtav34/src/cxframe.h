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

// Qt3 的 QImage(w,h,depth) 与 Qt4+ 的 QImage(w,h,Format) 共用一个构造函数
static inline QImage cxNewImage(int w, int h)
{
#ifdef QT3_BUILD
    // Qt3 的 32bit QImage 带 alpha 通道:解码/转换只写 RGB,alpha 字节为 0,
    // 直接 drawImage 会被当成全透明(表现为纯黑)。关闭 alpha buffer 让 Qt3
    // 忽略该字节(软渲/截图路径都需要;GL/X11/Xv/SDL 不受影响)。
    QImage img(w, h, 32);
    img.setAlphaBuffer(false);
    return img;
#else
    return QImage(w, h, QImage::Format_RGB32);
#endif
}

#endif // CXFRAME_H
