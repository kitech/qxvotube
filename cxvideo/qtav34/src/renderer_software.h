#ifndef RENDERER_SOFTWARE_H
#define RENDERER_SOFTWARE_H

#include "cxrenderer.h"
#include <qwidget.h>

// 软渲:QImage 三缓冲 + paintEvent letterbox(全 Qt,Qt3 首选;detail.md 第 6 节)
class CXRendererSoftware : public QWidget, public CXVideoRenderer {
    Q_OBJECT
public:
    explicit CXRendererSoftware(QWidget* parent = 0);

    void showFrame(const CXVideoFrame* f);
    void setViewport(const QRect& contentRect);

signals:
    void frameShown();

protected:
    void paintEvent(QPaintEvent* e);

private:
    QImage m_img;
    QRect m_viewport;
};

#endif // RENDERER_SOFTWARE_H
