#ifndef CXEQ_H
#define CXEQ_H

#include "cxvideo_global.h" // CXVideoEq
#include <qimage.h>

// M3-B/M4-A 共用软路 EQ 工具(renderer_software / renderer_gl 双吃):
// - brightness/contrast 入单张 256 LUT(RGB 三通道共表)
// - saturation 走 luma(299/587/114)加权 permil
// - hue 仅 GL 色矩阵/shader 路线(软路不做,见 detail.md §7.6)

inline void cxEqBuildLut(const CXVideoEq& eq, unsigned char* lut)
{
    int br = eq.brightness; // -100..100 → ±64/255 偏移
    int ct = eq.contrast;   // 每点 ±3‰ 乘强,绕 128
    for (int i = 0; i < 256; i++) {
        int v = i;
        if (ct != 0)
            v = 128 + (v - 128) * (100 + ct * 3) / 100;
        v += br * 64 / 100;
        if (v < 0)
            v = 0;
        if (v > 255)
            v = 255;
        lut[i] = (unsigned char)v;
    }
}

inline bool cxEqNeutral(const CXVideoEq& eq)
{
    return eq.brightness == 0 && eq.contrast == 0 &&
           eq.saturation == 0 && eq.hue == 0;
}

inline int cxEqSatPermil(const CXVideoEq& eq)
{
    return 1000 + eq.saturation * 10; // 0..2000 permil
}

// RGB32 CPU 路径(LUT+saturation;M3-B 原实现共享化)
inline QImage cxEqApplyRgb(const QImage& in, const unsigned char* lut,
                           int sat)
{
    QImage out = in;
    int w = out.width(), h = out.height();
    for (int y = 0; y < h; y++) {
        unsigned int* ln = (unsigned int*)out.scanLine(y);
        for (int x = 0; x < w; x++) {
            unsigned int c = ln[x];
            int r = (int)lut[(c >> 16) & 0xff];
            int g = (int)lut[(c >> 8) & 0xff];
            int b = (int)lut[c & 0xff];
            if (sat != 1000) {
                int luma = (r * 299 + g * 587 + b * 114) / 1000;
                r = luma + (r - luma) * sat / 1000;
                g = luma + (g - luma) * sat / 1000;
                b = luma + (b - luma) * sat / 1000;
                if (r < 0)   r = 0; else if (r > 255) r = 255;
                if (g < 0)   g = 0; else if (g > 255) g = 255;
                if (b < 0)   b = 0; else if (b > 255) b = 255;
            }
            ln[x] = (c & 0xff000000u) | ((unsigned)r << 16) |
                    ((unsigned)g << 8) | (unsigned)b;
        }
    }
    return out;
}

#endif // CXEQ_H
