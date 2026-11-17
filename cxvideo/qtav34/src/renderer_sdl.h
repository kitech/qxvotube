#ifndef RENDERER_SDL_H
#define RENDERER_SDL_H

#include "renderer_base.h"

#ifdef WITH_CX_RENDERER_SDL

#include <qwidget.h>
#include <qstring.h>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

// SDL 输出(跨平台兜底,一套代码覆盖 X11/Wayland/Windows/macOS)。
// 注意:SDL 的 renderer 后端可能是 opengl / direct3d11 / metal / software,
// 因此**不宣称无 GL**;实际后端名由 backendName() 提供给日志。
class CXRendererSdl : public QWidget, public CXVideoRenderer, public CxRendererCore {
public:
    explicit CXRendererSdl(QWidget* parent = 0);
    ~CXRendererSdl();

    void showFrame(const CXVideoFrame* f)
    { setTargetSize(size()); CxRendererCore::showFrame(f); }
    void setViewport(const QRect& r) { CxRendererCore::setViewport(r); }
    void setVideoEq(const CXVideoEq& eq) { CxRendererCore::setVideoEq(eq); }
    bool saveScreenshot(const QString& p) { return CxRendererCore::saveScreenshot(p); }

    static bool isAvailable();
    QString backendName() const { return m_backend; }

protected:
    bool present(const QImage& img, const QRect& content);
    void onFrameReady() { }
    void resizeEvent(QResizeEvent* e);

private:
    bool initSdl();
    void freeSdl();

    SDL_Window*   m_win;
    SDL_Renderer* m_r;
    SDL_Texture*  m_tex;
    int           m_tw;
    int           m_th;
    QString       m_backend;
};

#endif // WITH_CX_RENDERER_SDL
#endif // RENDERER_SDL_H
