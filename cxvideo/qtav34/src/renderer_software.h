#ifndef RENDERER_SOFTWARE_H
#define RENDERER_SOFTWARE_H

#include "cxrenderer.h"
#include "cxvideo_global.h" // CXVideoEq(完整型,impl 需要)
#include <qwidget.h>

// 软渲:QImage 三缓冲 + paintEvent letterbox(全 Qt,Qt3 首选;detail.md 第 6 节)
class CXRendererSoftware : public QWidget, public CXVideoRenderer {
    Q_OBJECT
public:
    explicit CXRendererSoftware(QWidget* parent = 0);

    void showFrame(const CXVideoFrame* f);
    void setViewport(const QRect& contentRect);

    // M3-B:软路 EQ + 截图(LUT 后所见即所得)
    void setVideoEq(const CXVideoEq& eq);
    bool saveScreenshot(const QString& path);

signals:
    void frameShown();

protected:
    void paintEvent(QPaintEvent* e);

private:
    QImage cxEqApply(const QImage& in) const; // EQ 处理一次拷贝(m_img 原始帧不动)
    void cxEqRebuildLut();

    QImage m_img;
    QRect m_viewport;
    CXVideoEq m_eq;              // 软路 EQ(生效在 paint 期拷贝上)
    unsigned char m_lut[256];    // brightness+contrast 复合查找表(RGB 共表)
    int m_sat;                   // 饱和度 permil(1000=原样;0=灰,2000=2x)
    bool m_eqOn;                 // 中性值旁路(全 0 时零处理)
};

#endif // RENDERER_SOFTWARE_H
