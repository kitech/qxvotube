#include "renderer_software.h"
#include "cxeq.h"
#include "renderer_base.h"   // cxScaleRgb
#include <qpainter.h>
#include <qimage.h>

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
img = cxToRgb32(img);
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
    if (m_eqOn)
        img = cxEqApply(img); // 每次重绘均为 LUT 后画面(缩放照旧)
img = cxToRgb32(img);
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
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    int dx = (s.width() - dw) / 2;
    int dy = (s.height() - dh) / 2;
    // 缩放走 sws(cxScaleRgb):Qt3 下 32bpp 图像经 QImage 缩放/QPainter 变换
    // 实测会整体变黑,而"未缩放 drawImage"正常 —— 见 renderer_base.h 注释。
    if (dw != iw || dh != ih)
        img = cxScaleRgb(img, dw, dh);
    p.drawImage(dx, dy, img);
}
