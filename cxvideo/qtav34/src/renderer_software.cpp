#include "renderer_software.h"
#include <qpainter.h>
#include <qimage.h>
#include <stdio.h>

CXRendererSoftware::CXRendererSoftware(QWidget* parent)
#ifdef QT3_BUILD
    : QWidget(parent, 0)
#else
    : QWidget(parent)
#endif
{
    setMinimumSize(320, 180);
    cxEqRebuildLut();
}

// M3-B 软路 EQ:brightness/contrast → 单张 256 项 LUT(RGB 三通道共表);
// saturation → luma(299/587/114)加权混合,permil 系数(0..2000)
void CXRendererSoftware::cxEqRebuildLut()
{
    int br = m_eq.brightness; // -100..100 → ±64/255 偏移
    int ct = m_eq.contrast;   // -100..100 → 每点 ±3‰ 乘强,绕 128
    m_eqOn = (br != 0 || ct != 0 || m_eq.saturation != 0);
    m_sat = 1000 + m_eq.saturation * 10;
    for (int i = 0; i < 256; i++) {
        int v = i;
        if (ct != 0)
            v = 128 + (v - 128) * (100 + ct * 3) / 100; // ≈0.7..2.3 乘强
        v += br * 64 / 100;
        if (v < 0)
            v = 0;
        if (v > 255)
            v = 255;
        m_lut[i] = (unsigned char)v;
    }
}

void CXRendererSoftware::setVideoEq(const CXVideoEq& eq)
{
    m_eq = eq;
    cxEqRebuildLut();
    update(); // 立即以新 EQ 重绘(不必等新帧)
}

// EQ 处理一次拷贝(m_img 原始帧不动;RGB32/ARGB32 布局双 Qt 一致)
QImage CXRendererSoftware::cxEqApply(const QImage& in) const
{
    QImage out = in;
    int w = out.width(), h = out.height();
    for (int y = 0; y < h; y++) {
        unsigned int* ln = (unsigned int*)out.scanLine(y);
        for (int x = 0; x < w; x++) {
            unsigned int c = ln[x];
            int r = (int)m_lut[(c >> 16) & 0xff];
            int g = (int)m_lut[(c >> 8) & 0xff];
            int b = (int)m_lut[c & 0xff];
            if (m_sat != 1000) {
                int luma = (r * 299 + g * 587 + b * 114) / 1000;
                r = luma + (r - luma) * m_sat / 1000;
                g = luma + (g - luma) * m_sat / 1000;
                b = luma + (b - luma) * m_sat / 1000;
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

bool CXRendererSoftware::saveScreenshot(const QString& path)
{
    if (m_img.isNull() || path.isEmpty())
        return false;
    QImage img = cxEqApply(m_img); // 所见即所得(LUT 后)
#ifdef QT3_BUILD
    if (img.depth() != 32)
        img = img.convertDepth(32);
#endif
    return img.save(path, "PNG");
}

void CXRendererSoftware::showFrame(const CXVideoFrame* f)
{
    if (f == 0)
        return;
    m_img = f->img;
    m_viewport = QRect(0, 0, f->width, f->height);
    update();
    emit frameShown();
}

void CXRendererSoftware::setViewport(const QRect& contentRect)
{
    m_viewport = contentRect;
    update();
}

void CXRendererSoftware::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (m_img.isNull())
        return;
    QImage img = m_img;
#ifdef QT3_BUILD
    if (img.depth() != 32)
        img = img.convertDepth(32);
#endif
    if (m_eqOn)
        img = cxEqApply(img); // 每次重绘均为 LUT 后画面(缩放照旧)
    // letterbox:等比缩入控件矩形,不足留黑边
    QSize s = size();
    int iw = img.width();
    int ih = img.height();
    if (iw <= 0 || ih <= 0)
        return;
    double scale = double(s.width()) / iw;
    double scaleH = double(s.height()) / ih;
    if (scaleH < scale)
        scale = scaleH;
    int dw = int(iw * scale);
    int dh = int(ih * scale);
    int dx = (s.width() - dw) / 2;
    int dy = (s.height() - dh) / 2;
#ifdef QT3_BUILD
    // Qt3 的 drawImage 无目标矩形重载,先 smoothScale 再按点绘制
    if (dw != iw || dh != ih)
        img = img.smoothScale(dw, dh);
    p.drawImage(dx, dy, img);
#else
    p.drawImage(QRect(dx, dy, dw, dh), img);
#endif
}
