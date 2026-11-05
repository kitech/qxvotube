#include "renderer_software.h"
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
