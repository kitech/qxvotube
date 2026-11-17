#include "renderer_base.h"
#include "cxeq.h"
#include "cxframe.h"      // cxNewImage

#if defined(WITH_CX_ENGINE_FFMPEG)
extern "C" {
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}
#endif

QImage cxScaleRgb(const QImage& in, int dstW, int dstH)
{
    if (in.isNull() || dstW <= 0 || dstH <= 0)
        return QImage();
    if (in.width() == dstW && in.height() == dstH)
        return in;
    QImage src = in;
    src = cxToRgb32(src);
#if defined(WITH_CX_ENGINE_FFMPEG)
    // 单线程(GUI 线程)使用,静态缓存即可;尺寸变化时 sws 自动重建
    static SwsContext* s_sws = 0;
    s_sws = sws_getCachedContext(s_sws,
                                 src.width(), src.height(), AV_PIX_FMT_RGB32,
                                 dstW, dstH, AV_PIX_FMT_RGB32,
                                 SWS_BILINEAR, 0, 0, 0);
    if (s_sws != 0) {
        QImage out = cxNewImage(dstW, dstH);
        const uint8_t* p[4] = { (const uint8_t*)src.bits(), 0, 0, 0 };
        int ps[4] = { src.bytesPerLine(), 0, 0, 0 };
        uint8_t* d[4] = { out.bits(), 0, 0, 0 };
        int ds[4] = { out.bytesPerLine(), 0, 0, 0 };
        sws_scale(s_sws, p, ps, 0, src.height(), d, ds);
        return out;
    }
#endif
    return src;   // 无 ffmpeg:不缩放(调用方按原尺寸绘制)
}

CxRendererCore::CxRendererCore()
    : m_sat(1000), m_eqOn(false)
{
    for (int i = 0; i < 256; ++i)
        m_lut[i] = (unsigned char)i;
}

void CxRendererCore::setVideoEq(const CXVideoEq& eq)
{
    m_eq = eq;
    cxEqBuildLut(m_eq, m_lut);
    m_sat = cxEqSatPermil(m_eq);
    m_eqOn = !cxEqNeutral(m_eq);
    if (m_img.isNull())
        return;
    QImage out = m_eqOn ? cxEqApplyRgb(m_img, m_lut, m_sat) : m_img;
    QRect content = (m_viewport.isValid() && !m_viewport.isEmpty())
                    ? m_viewport : letterbox(m_targetSize, out.size());
    present(out, content);
    onFrameReady();
}

void CxRendererCore::showFrame(const CXVideoFrame* f)
{
    if (f == 0 || f->img.isNull())
        return;
    m_img = f->img;
    QImage out = m_eqOn ? cxEqApplyRgb(m_img, m_lut, m_sat) : m_img;
    QRect content = (m_viewport.isValid() && !m_viewport.isEmpty())
                    ? m_viewport : letterbox(m_targetSize, out.size());
    present(out, content);
    onFrameReady();
}

QRect CxRendererCore::letterbox(const QSize& widget, const QSize& video) const
{
    if (widget.width() <= 0 || widget.height() <= 0 ||
        video.width() <= 0 || video.height() <= 0)
        return QRect(0, 0, widget.width(), widget.height());
    double s = double(widget.width()) / double(video.width());
    double sh = double(widget.height()) / double(video.height());
    if (sh < s)
        s = sh;
    int w = int(video.width() * s);
    int h = int(video.height() * s);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    return QRect((widget.width() - w) / 2, (widget.height() - h) / 2, w, h);
}

bool CxRendererCore::saveScreenshot(const QString& path)
{
    if (m_img.isNull() || path.isEmpty())
        return false;
    QImage img = m_eqOn ? cxEqApplyRgb(m_img, m_lut, m_sat) : m_img;
img = cxToRgb32(img);
    return img.save(path, "PNG");
}
