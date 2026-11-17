#include "renderer_sdl.h"

#ifdef WITH_CX_RENDERER_SDL

#include "cxsdl_compat.h"
#include <qimage.h>
#include <qevent.h>

bool CXRendererSdl::isAvailable()
{
    if (!SDL_WasInit(SDL_INIT_VIDEO) && SDL_InitSubSystem(SDL_INIT_VIDEO) < 0)
        return false;
    return true;
}

CXRendererSdl::CXRendererSdl(QWidget* parent)
#ifdef QT3_BUILD
    : QWidget(parent, 0, WNoAutoErase), m_win(0), m_r(0), m_tex(0), m_tw(0), m_th(0)
#else
    : QWidget(parent), m_win(0), m_r(0), m_tex(0), m_tw(0), m_th(0)
#endif
{
    setMinimumSize(320, 180);
#ifdef QT3_BUILD
    setBackgroundMode(NoBackground);   /* Qt3:无 setAttribute */
#else
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_NativeWindow, true);
#endif
    setTargetSize(size());
    /* 注意:不能在构造期窗口还没挂进布局时取 winId() —— Qt3 会把子件
     * 变成独立顶层窗,SDL 包装后引发 BadWindow。改到首帧 present() 时
     * 惰性初始化(此时宿主已 reparent/show)。 */
}

CXRendererSdl::~CXRendererSdl()
{
    freeSdl();
}

void CXRendererSdl::freeSdl()
{
    if (m_tex != 0) {
        SDL_DestroyTexture(m_tex);
        m_tex = 0;
    }
    if (m_r != 0) {
        SDL_DestroyRenderer(m_r);
        m_r = 0;
    }
    if (m_win != 0) {
        SDL_DestroyWindow(m_win);
        m_win = 0;
    }
    m_tw = 0;
    m_th = 0;
}

bool CXRendererSdl::initSdl()
{
    if (m_r != 0)
        return true;
    if (!SDL_WasInit(SDL_INIT_VIDEO) && SDL_InitSubSystem(SDL_INIT_VIDEO) < 0)
        return false;
    if (m_win == 0) {
        m_win = cxSdlWrapNative((void*)(intptr_t)winId());
        if (m_win == 0)
            return false;
    }
    if (!cxSdlMakeRenderer(m_win, &m_r)) {
        freeSdl();
        return false;
    }
    const char* nm = cxSdlRendererName(m_r);
    m_backend = nm != 0 ? QString::fromLatin1(nm) : QString();
    return true;
}

bool CXRendererSdl::present(const QImage& img, const QRect& content)
{
    if (m_r == 0 && !initSdl())
        return false;
    QImage src = img;
    src = cxToRgb32(src);
    if (m_tex == 0 || m_tw != src.width() || m_th != src.height()) {
        if (m_tex != 0)
            SDL_DestroyTexture(m_tex);
        m_tex = SDL_CreateTexture(m_r, SDL_PIXELFORMAT_ARGB8888,
                                  SDL_TEXTUREACCESS_STREAMING,
                                  src.width(), src.height());
        if (m_tex == 0)
            return false;
        m_tw = src.width();
        m_th = src.height();
    }
#if CX_SDL_MAJOR == 3
    if (!SDL_UpdateTexture(m_tex, 0, src.bits(), src.bytesPerLine()))
        return false;
#else
    if (SDL_UpdateTexture(m_tex, 0, src.bits(), src.bytesPerLine()) != 0)
        return false;
#endif
    cxSdlBlit(m_r, m_tex, content.x(), content.y(),
              content.width(), content.height());
    return true;
}

void CXRendererSdl::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    setTargetSize(e->size());
    if (!m_img.isNull())
        present(m_img, letterbox(e->size(), m_img.size()));
}

#endif // WITH_CX_RENDERER_SDL
