#ifndef RENDERER_METAL_H
#define RENDERER_METAL_H

#include "renderer_base.h"
#include "cxplatform.h"

#if defined(CX_OS_MAC) && defined(WITH_CX_RENDERER_METAL)

#include <qwidget.h>

// macOS 输出:Metal(CAMetalLayer + blit 到 drawable)。
// Qt5+ 限定(Qt3 macOS 不支持 layer-backed NSView 的现代 Metal 路径)。
class CXRendererMetal : public QWidget, public CXVideoRenderer, public CxRendererCore {
public:
    explicit CXRendererMetal(QWidget* parent = 0);
    ~CXRendererMetal();

    void showFrame(const CXVideoFrame* f)
    { setTargetSize(size()); CxRendererCore::showFrame(f); }
    void setViewport(const QRect& r) { CxRendererCore::setViewport(r); }
    void setVideoEq(const CXVideoEq& eq) { CxRendererCore::setVideoEq(eq); }
    bool saveScreenshot(const QString& p) { return CxRendererCore::saveScreenshot(p); }

    static bool isAvailable();

protected:
    bool present(const QImage& img, const QRect& content);
    void onFrameReady() { }
    void resizeEvent(QResizeEvent* e);

private:
    bool initMetal();
    void freeMetal();

    void* m_dev;     // id<MTLDevice>
    void* m_queue;   // id<MTLCommandQueue>
    void* m_layer;   // CAMetalLayer*
    void* m_view;    // NSView*
};

#endif
#endif // RENDERER_METAL_H
