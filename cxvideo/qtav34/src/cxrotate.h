#ifndef CXROTATE_H
#define CXROTATE_H

#include "cxframe.h"   // cxNewImage
#include <qimage.h>

// 顺时针旋转 32bpp 图像(0/90/180/270)。不用 QPainter/QImage::transformed:
// Qt3 32bpp 变换会整体变黑(见 renderer_base.h 注释)。
static inline QImage cxRotateRgb32(const QImage& in, int deg)
{
    if (in.isNull() || in.depth() != 32)
        return in;
    int d = ((deg % 360) + 360) % 360;
    if (d == 0) return in;
    const int W = in.width(), H = in.height();
    if (W <= 0 || H <= 0) return in;

    if (d == 180) {
        QImage out = cxNewImage(W, H);
        for (int y = 0; y < H; ++y) {
            const uint* s = reinterpret_cast<const uint*>(in.scanLine(y));
            uint* o = reinterpret_cast<uint*>(out.scanLine(H - 1 - y));
            for (int x = 0; x < W; ++x) o[W - 1 - x] = s[x];
        }
        return out;
    }
    QImage out = cxNewImage(H, W);   // 90/270 交换宽高
    if (d == 90) {                   // 顺时针 90
        for (int y = 0; y < H; ++y) {
            const uint* s = reinterpret_cast<const uint*>(in.scanLine(y));
            for (int x = 0; x < W; ++x) {
                uint* o = reinterpret_cast<uint*>(out.scanLine(x));
                o[H - 1 - y] = s[x];
            }
        }
    } else {                         // 顺时针 270
        for (int y = 0; y < H; ++y) {
            const uint* s = reinterpret_cast<const uint*>(in.scanLine(y));
            for (int x = 0; x < W; ++x) {
                uint* o = reinterpret_cast<uint*>(out.scanLine(W - 1 - x));
                o[y] = s[x];
            }
        }
    }
    return out;
}

#endif // CXROTATE_H
