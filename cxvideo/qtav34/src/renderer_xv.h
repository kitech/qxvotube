#ifndef RENDERER_XV_H
#define RENDERER_XV_H

#include "renderer_base.h"
#include "cxplatform.h"

#ifdef CX_HAVE_XV

#include <qwidget.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xvlib.h>

// 所选 adaptor 支持的 (depth, visualid) 记录上限(仅用于诊断 + 兼容检查)
#define CX_XV_MAX_FMT 8

// Xv 输出:X server 做 YUV 显示与缩放(CPU 只做一次 RGB32→YUYV)。
// 失败原因分四类(no-display/no-adaptor/no-yuv-format/port-busy/visual-mismatch),
// 由 lastError() 交给宿主写日志并回退(见 docs/cxvideo-vo.md §6)。
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
    // 非内联:避免头文件布局变动时旧 .o 内联展开读到错位成员(跨 TU ODR 隐患)
    bool ready() const;
    QString lastError() const;

protected:
    bool present(const QImage& img, const QRect& content);
    void onFrameReady() { }
    void paintEvent(QPaintEvent* e);
    void resizeEvent(QResizeEvent* e);

private:
    bool initXv();
    void freeXv();
    void clearScreen();   // 全窗填黑:letterbox 区不得残留/透出背景窗口
    bool ensureConvert(int w, int h);
    void checkWindowVisual();     // 首帧核对窗口 visual 是否被 adaptor 支持
    void setErr(const char* why);

    Display* m_dpy;
    int      m_screen;
    Window   m_win;
    GC       m_gc;
    GC       m_clearGc;   // 基于 m_win 的清屏 GC(与 m_gc 的 root drawable 分开)
    XvPortID m_port;
    int      m_formatId;
    XvImage* m_xvImg;
    char*    m_buf;
    int      m_vw;
    int      m_vh;
    void*    m_sws;      // SwsContext*
    // 诊断:所选 adaptor 的格式表 + 上报给宿主的失败原因
    unsigned long m_fmtDepth[CX_XV_MAX_FMT];
    unsigned long m_fmtVisual[CX_XV_MAX_FMT];
    int      m_fmtCount;
    bool     m_visualChecked;
    QString  m_err;
};

#endif // CX_HAVE_XV
#endif // RENDERER_XV_H
