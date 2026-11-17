#ifndef RENDERER_XV_H
#define RENDERER_XV_H

#include "renderer_base.h"
#include "cxplatform.h"

#ifdef CX_HAVE_XV

#include <qwidget.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xvlib.h>

// Xv 输出:X server 做 YUV 显示与缩放(CPU 只做一次 RGB32→YUYV)。
// 无可用 adaptor/端口被占用 → 构造期不可用,宿主回退(见 cxvideo-vo.md §6)。
class CXRendererXv : public QWidget, public CXVideoRenderer, public CxRendererCore {
public:
    explicit CXRendererXv(QWidget* parent = 0);
    ~CXRendererXv();

    void showFrame(const CXVideoFrame* f)
    { setTargetSize(size()); CxRendererCore::showFrame(f); }
    void setViewport(const QRect& r) { CxRendererCore::setViewport(r); }
    void setVideoEq(const CXVideoEq& eq) { CxRendererCore::setVideoEq(eq); }
    bool saveScreenshot(const QString& p) { return CxRendererCore::saveScreenshot(p); }

    static bool isAvailable();
    bool ready() const { return m_port != 0; }

protected:
    bool present(const QImage& img, const QRect& content);
    void onFrameReady() { }
    void paintEvent(QPaintEvent* e);
    void resizeEvent(QResizeEvent* e);

private:
    bool initXv();
    void freeXv();
    bool ensureConvert(int w, int h);

    Display* m_dpy;
    int      m_screen;
    Window   m_win;
    GC       m_gc;
    XvPortID m_port;
    int      m_formatId;
    XvImage* m_xvImg;
    char*    m_buf;
    int      m_vw;
    int      m_vh;
    void*    m_sws;      // SwsContext*
};

#endif // CX_HAVE_XV
#endif // RENDERER_XV_H
