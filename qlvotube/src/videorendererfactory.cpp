#include "videorendererfactory.h"

#include "renderer_software.h"
#include "renderer_gl.h"
#ifdef WITH_CX_RENDERER_X11
#  include "renderer_x11.h"
#endif
#ifdef WITH_CX_RENDERER_XV
#  include "renderer_xv.h"
#endif
#ifdef WITH_CX_RENDERER_SDL
#  include "renderer_sdl.h"
#endif
#ifdef WITH_CX_RENDERER_D3D11
#  include "renderer_d3d11.h"
#endif
#ifdef WITH_CX_RENDERER_METAL
#  include "renderer_metal.h"
#endif

QString cxResolveVideoOut(const QString& out, const QString& envOverride)
{
    QString v = envOverride;
    if (v.isEmpty())
        v = out;
    if (v.isEmpty())
        v = qFromUtf8("auto");
    return v;
}

// 平台链:无 GL 优先,gl 最后(cxvideo-vo.md §2)
static QStringList cxAutoChain()
{
    QStringList l;
#ifdef WITH_CX_RENDERER_XV
    l << qFromUtf8("xv");
#endif
#ifdef WITH_CX_RENDERER_X11
    l << qFromUtf8("x11");
#endif
#ifdef WITH_CX_RENDERER_D3D11
    l << qFromUtf8("d3d11");
#endif
#ifdef WITH_CX_RENDERER_METAL
    l << qFromUtf8("metal");
#endif
#ifdef WITH_CX_RENDERER_SDL
    l << qFromUtf8("sdl");
#endif
    l << qFromUtf8("raster");
    l << qFromUtf8("gl");
    return l;
}

static CxVideoOut cxTryCreate(QWidget* host, const QString& name)
{
    CxVideoOut r;
    r.widget = 0;
    r.renderer = 0;
    r.name = name;

    if (name == qFromUtf8("gl")) {
        CXRendererGl* w = new CXRendererGl(host);
        r.widget = w;
        r.renderer = w;
    } else if (name == qFromUtf8("raster")) {
        CXRendererSoftware* w = new CXRendererSoftware(host);
        r.widget = w;
        r.renderer = w;
    }
#ifdef WITH_CX_RENDERER_X11
    else if (name == qFromUtf8("x11")) {
        if (CXRendererX11::isAvailable()) {
            CXRendererX11* w = new CXRendererX11(host);
            r.widget = w;
            r.renderer = w;
        }
    }
#endif
#ifdef WITH_CX_RENDERER_XV
    else if (name == qFromUtf8("xv")) {
        /* 不再用 isAvailable() 预检(会重复 XOpenDisplay);
         * 构造函数内部完成 adaptor 选择,失败用 lastError() 带出具体原因。 */
        CXRendererXv* w = new CXRendererXv(host);
        if (w->ready()) {
            r.widget = w;
            r.renderer = w;
        } else {
            r.why = w->lastError().isEmpty() ? qFromUtf8("unavailable")
                                             : w->lastError();
            delete w;
        }
    }
#endif
#ifdef WITH_CX_RENDERER_SDL
    else if (name == qFromUtf8("sdl")) {
        if (CXRendererSdl::isAvailable()) {
            CXRendererSdl* w = new CXRendererSdl(host);
            r.widget = w;
            r.renderer = w;
        }
    }
#endif
#ifdef WITH_CX_RENDERER_D3D11
    else if (name == qFromUtf8("d3d11")) {
        if (CXRendererD3D11::isAvailable()) {
            CXRendererD3D11* w = new CXRendererD3D11(host);
            r.widget = w;
            r.renderer = w;
        }
    }
#endif
#ifdef WITH_CX_RENDERER_METAL
    else if (name == qFromUtf8("metal")) {
        if (CXRendererMetal::isAvailable()) {
            CXRendererMetal* w = new CXRendererMetal(host);
            r.widget = w;
            r.renderer = w;
        }
    }
#endif
    if (r.widget == 0 && r.why.isEmpty())
        r.why = qFromUtf8("不可用");
    return r;
}

QStringList cxAvailableVideoOuts()
{
    QStringList l;
    l << qFromUtf8("auto");
#ifdef WITH_CX_RENDERER_XV
    l << qFromUtf8("xv");
#endif
#ifdef WITH_CX_RENDERER_X11
    l << qFromUtf8("x11");
#endif
#ifdef WITH_CX_RENDERER_D3D11
    l << qFromUtf8("d3d11");
#endif
#ifdef WITH_CX_RENDERER_METAL
    l << qFromUtf8("metal");
#endif
#ifdef WITH_CX_RENDERER_SDL
    l << qFromUtf8("sdl");
#endif
    l << qFromUtf8("raster") << qFromUtf8("gl");
    return l;
}

CxVideoOut cxCreateVideoOut(QWidget* host, const QString& out,
                            const QString& envOverride)
{
    const QString want = cxResolveVideoOut(out, envOverride);
    if (want != qFromUtf8("auto")) {
        CxVideoOut r = cxTryCreate(host, want);
        if (r.widget != 0) {
            r.why = qFromUtf8("config/env");
            return r;
        }
        CxVideoOut re = cxTryCreate(host, qFromUtf8("raster"));
        re.why = QString::fromUtf8("回退:%1(%2)")
                     .arg(want)
                     .arg(r.why.isEmpty() ? qFromUtf8("不可用") : r.why);
        return re;
    }

    const QStringList chain = cxAutoChain();
    QString tried;
    for (int i = 0; i < chain.size(); ++i) {
        CxVideoOut r = cxTryCreate(host, chain[i]);
        if (r.widget != 0) {
            r.why = tried.isEmpty()
                    ? qFromUtf8("auto")
                    : QString::fromUtf8("auto:%1").arg(tried);
            return r;
        }
        if (!tried.isEmpty())
            tried += qFromUtf8(",");
        // 带原因:xv(no-adaptor) / sdl(unavailable) ...
        tried += chain[i];
        if (!r.why.isEmpty())
            tried += qFromUtf8("(") + r.why + qFromUtf8(")");
    }
    CxVideoOut none;
    none.widget = 0;
    none.renderer = 0;
    none.name = qFromUtf8("none");
    none.why = qFromUtf8("无可用后端");
    return none;
}
