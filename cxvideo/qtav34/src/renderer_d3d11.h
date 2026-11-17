#ifndef RENDERER_D3D11_H
#define RENDERER_D3D11_H

#include "renderer_base.h"
#include "cxplatform.h"

#if defined(CX_OS_WIN) && defined(WITH_CX_RENDERER_D3D11)

#include <qwidget.h>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain1;
struct ID3D11Texture2D;

// Windows 输出:D3D11 + DXGI swap chain(HWND 取自 winId())。
// 传帧走 dynamic staging texture → CopyResource → Present,不需要 shader。
// Qt 侧必须停止绘制,否则会把 D3D 内容盖掉(见 cxvideo-vo.md §6)。
class CXRendererD3D11 : public QWidget, public CXVideoRenderer, public CxRendererCore {
public:
    explicit CXRendererD3D11(QWidget* parent = 0);
    ~CXRendererD3D11();

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
    bool initD3d();
    void freeD3d();
    bool ensureStaging(int w, int h);

    ID3D11Device*        m_dev;
    ID3D11DeviceContext* m_ctx;
    IDXGISwapChain1*     m_sc;
    ID3D11Texture2D*     m_stage;
    int m_sw;
    int m_sh;
    int m_bw;
    int m_bh;
};

#endif
#endif // RENDERER_D3D11_H
