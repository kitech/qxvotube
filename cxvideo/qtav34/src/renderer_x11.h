#ifndef RENDERER_X11_H
#define RENDERER_X11_H

#include "renderer_base.h"
#include "cxplatform.h"

#ifdef CX_HAVE_X11

#include <qwidget.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>

// X11 直渲(无 GL):XImage + MIT-SHM 双缓冲;
// SHM 不可用(远程 X / 部分 Xvfb)自动回退普通 XPutImage。
// 见 docs/cxvideo-vo.md §6 实现约束(SHM 生命周期、Qt 不绘制)。
class CXRendererX11 : public QWidget, public CXVideoRenderer, public CxRendererCore {
public:
    explicit CXRendererX11(QWidget* parent = 0);
    ~CXRendererX11();

    void showFrame(const CXVideoFrame* f)
    { setTargetSize(size()); CxRendererCore::showFrame(f); }
    void setViewport(const QRect& r) { CxRendererCore::setViewport(r); }
    void setVideoEq(const CXVideoEq& eq) { CxRendererCore::setVideoEq(eq); }
    bool saveScreenshot(const QString& p) { return CxRendererCore::saveScreenshot(p); }

    static bool isAvailable();

protected:
    bool present(const QImage& img, const QRect& content);
    void onFrameReady() { update(); }
    void paintEvent(QPaintEvent* e);
    void resizeEvent(QResizeEvent* e);

private:
    bool ensureX();
    bool ensureBuffer(int w, int h);
    void destroyBuffer();
    void drainShm();
    bool shmAlloc(int idx, int w, int h);

    Display* m_dpy;
    int      m_screen;
    GC       m_gc;
    Window   m_win;
    bool     m_shmOk;
    int      m_bw;
    int      m_bh;
    int      m_cur;
    int      m_pending;
    XImage*  m_xi[2];       // 双缓冲 XImage(勿与基类 QImage m_img 混淆)
    XShmSegmentInfo m_seg[2];
};

#endif // CX_HAVE_X11
#endif // RENDERER_X11_H
