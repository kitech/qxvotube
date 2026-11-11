#include "renderer_qmpv.h"
#include "eng_mpv.h"
#include "cxmpv_def.h"

#ifndef QT3_BUILD

#include <QOpenGLFramebufferObject>
#include <qapplication.h>

static CXQuickMpvFboItem* cxMpvQuickTarget = 0; // 单例:一 engine 一视图

// 自定事件类型与 renderer_mpvgl 同值(4710;两域不共存一窗,不冲突)
const int kCxMpvQuickUpdateEv = 4710;

void cxMpvQuickUpdateHook(void*)
{
    CXQuickMpvFboItem* v = cxMpvQuickTarget;
    if (v != 0)
        QApplication::postEvent(v, new QEvent((QEvent::Type)kCxMpvQuickUpdateEv));
    // 事件由 QApplication::sendPostedEvents 统一析构(renderer_mpvgl 同规约)
}

CXQuickMpvFboItem::CXQuickMpvFboItem(QQuickItem* parent)
    : QQuickFramebufferObject(parent), m_eng(0), m_attached(false)
{
    cxMpvQuickTarget = this;
}

CXQuickMpvFboItem::~CXQuickMpvFboItem()
{
    if (cxMpvQuickTarget == this)
        cxMpvQuickTarget = 0;
}

void CXQuickMpvFboItem::bindEngine(CXEngineMpv* eng)
{
    m_eng = eng;
}

void CXQuickMpvFboItem::showFrame(const CXVideoFrame*) { }

void CXQuickMpvFboItem::setViewport(const QRect&) { }

bool CXQuickMpvFboItem::saveScreenshot(const QString& path)
{
    return m_eng != 0 ? m_eng->screenshot(path) : false;
}

bool CXQuickMpvFboItem::event(QEvent* ev)
{
    if ((int)ev->type() == kCxMpvQuickUpdateEv) {
        update(); // 唤醒场景图:同步 FBO 帧并 render
        return true;
    }
    return QQuickFramebufferObject::event(ev);
}

void CXQuickMpvFboItem::Renderer::synchronize(QQuickFramebufferObject*) { }

QOpenGLFramebufferObject* CXQuickMpvFboItem::Renderer::createFramebufferObject(
    const QSize& size)
{
    return new QOpenGLFramebufferObject(
        size, QOpenGLFramebufferObject::CombinedDepthStencil);
}

void CXQuickMpvFboItem::Renderer::render()
{
    CxMpvApi& api = CxMpvApi::instance();
    // 懒挂:首次 render 时本线程 context current,engine 补建 render_context
    if (owner->m_eng != 0 && !owner->m_attached)
        owner->m_attached = owner->m_eng->attachQuickRender();
    void* rc = owner->m_eng != 0 ? owner->m_eng->renderObj() : 0;
    if (rc == 0 || api.mpv_render_context_render == 0)
        return;
    CxMpvOpenglFbo fbo;
    fbo.fbo = (unsigned)framebufferObject()->handle(); // QFBO 私有 FBO(非默认 0)
    fbo.w = framebufferObject()->width();
    fbo.h = framebufferObject()->height();
    fbo.internal_format = 0;
    int flipY = 0; // QFBO 纹理路径 Qt 回读已含方向;仅默认 framebuffer 才需翻转
    CxMpvRenderParam params[3];
    params[0].type = CxMpvRenderParamOpenGLFbo;
    params[0].data = &fbo;
    params[1].type = CxMpvRenderParamFlipY;
    params[1].data = &flipY;
    params[2].type = CxMpvRenderParamInvalid;
    params[2].data = 0;
    if (api.mpv_render_context_render((CxMpvRenderCtx*)rc, params) == 0 &&
        api.mpv_render_context_report_swap != 0)
        api.mpv_render_context_report_swap((CxMpvRenderCtx*)rc);
}

#endif // !QT3_BUILD
