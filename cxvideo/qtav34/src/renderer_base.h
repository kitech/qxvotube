#ifndef RENDERER_BASE_H
#define RENDERER_BASE_H

#include "cxrenderer.h"
#include "cxvideo_global.h"   // CXVideoEq / CXVideoFrame
#include <qimage.h>
#include <qrect.h>
#include <qsize.h>
#include <qstring.h>

// RGB32 缩放(sws,ffmpeg 引擎侧已链接)。
// 为什么不用 QImage::smoothScale / QPainter 变换:
//   本机 Qt 3.3.8 下 32bpp 图像的"缩放绘制"会整体变黑(未缩放 drawImage 正常),
//   实测 smoothScale/scale/QPainter::scale 三条路全黑,而 sws 输出正常。
//   统一走 sws 还能省掉 Qt3 的逐像素平滑缩放。
// 无 ffmpeg 的构建返回原图(调用方按需降级)。
QImage cxScaleRgb(const QImage& in, int dstW, int dstH);

// 统一转成 RGB32 布局(Qt3 用 convertDepth,Qt4+ 用 convertToFormat)
inline QImage cxToRgb32(const QImage& in)
{
    if (in.depth() == 32)
        return in;
#ifdef QT3_BUILD
    return in.convertDepth(32);
#else
    return in.convertToFormat(QImage::Format_RGB32);
#endif
}

// 各后端共用逻辑(组合,不继承 QWidget):
//   最近帧缓存 + EQ(CPU LUT,与软渲同源) + letterbox 计算 + 截图
// 子类只需实现 present():把"已带黑边、已缩放"的整幅图提交到屏幕。
class CxRendererCore {
public:
    CxRendererCore();

    void showFrame(const CXVideoFrame* f);
    void setViewport(const QRect& r) { m_viewport = r; }
    void setVideoEq(const CXVideoEq& eq);
    bool saveScreenshot(const QString& path);
    void setTargetSize(const QSize& s) { m_targetSize = s; }
    const QImage& lastImage() const { return m_img; }

    // 等比装入 widget 的目标矩形(黑边居中)
    QRect letterbox(const QSize& widget, const QSize& video) const;

protected:
    virtual bool present(const QImage& img, const QRect& content) = 0;
    virtual void onFrameReady() { }      // 子类需要 update()/重绘时实现

    QImage m_img;           // 最近一帧(原始尺寸,EQ 前的源)
    QRect  m_viewport;      // 引擎下发的内容区(空则按 letterbox 计算)
    QSize  m_targetSize;    // 目标控件尺寸
    CXVideoEq m_eq;
    unsigned char m_lut[256];
    int  m_sat;
    bool m_eqOn;
};

#endif // RENDERER_BASE_H
