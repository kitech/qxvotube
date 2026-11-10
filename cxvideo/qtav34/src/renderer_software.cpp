#include "renderer_software.h"
#include "cxeq.h"
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

// M3-B 软路 EQ:走 cxeq.h 共用件(LUT 单张 + saturation permil;hue 仅 GL 路)
void CXRendererSoftware::cxEqRebuildLut()
{
    cxEqBuildLut(m_eq, m_lut);
    m_eqOn = !cxEqNeutral(m_eq);
    m_sat = cxEqSatPermil(m_eq);
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
    return cxEqApplyRgb(in, m_lut, m_sat);
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
