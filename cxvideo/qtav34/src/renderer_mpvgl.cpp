#include "renderer_mpvgl.h"
#include "cxmpv_def.h"
#ifdef QT3_BUILD
#include <GL/glx.h> // glXGetProcAddressARB(Qt3 QGLContext 无 getProcAddress)
#else
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#endif
#include <qapplication.h>

// ── swap/update 驱动(mpv 内部线程 → GUI 线程)──
static CXRendererMpvGl* cxMpvGlView = 0; // 单例:一 engine 一视图

// 自定事件类型(避开 qlappcom User+100~113 与 mpv 泵 4002)
const int kCxMpvGlUpdateEv = 4710;

static void cxMpvGlUpdateCbNotify(void* /*ctx*/)
{
    CXRendererMpvGl* v = cxMpvGlView;
    if (v != 0)
        QApplication::postEvent(v, new QEvent((QEvent::Type)kCxMpvGlUpdateEv));
}

// 导出钩子(eng_mpv 以函数指针交 mpv_render_context_set_update_callback)
void cxMpvGlUpdateHook(void* /*ctx*/)
{
    cxMpvGlUpdateCbNotify(0);
}

// get_proc_address:调用时须 GL context current(engine 侧 makeCurrent 后创建)
static void* cxMpvGlProcThunk(void* /*ctx*/, const char* name)
{
#ifdef QT3_BUILD
    return (void*)glXGetProcAddressARB((const GLubyte*)name);
#else
    QOpenGLContext* c = QOpenGLContext::currentContext();
    return c != 0 ? (void*)c->getProcAddress(name) : 0;
#endif
}

CXRendererMpvGl::CXRendererMpvGl(QWidget* parent)
#ifdef QT3_BUILD
    : QGLWidget(parent, 0)
#else
    : QOpenGLWidget(parent)
#endif
{
    m_rc = 0;
#ifndef QT3_BUILD
    m_vao = 0;
#endif
    cxMpvGlView = this;
}

void CXRendererMpvGl::setRenderObj(void* rc)
{
    m_rc = rc;
    if (m_rc != 0)
        update();
}

bool CXRendererMpvGl::event(QEvent* ev)
{
    if ((int)ev->type() == kCxMpvGlUpdateEv) {
        update();
        return true; // 事件由 QApplication::sendPostedEvents 统一析构
    }
#ifdef QT3_BUILD
    return QGLWidget::event(ev);
#else
    return QOpenGLWidget::event(ev);
#endif
}

void CXRendererMpvGl::initializeGL()
{
#ifndef QT3_BUILD
    // Qt6 core profile:建空 VAO(mpv 自家 render 不动 attrib 状态,
    // 但 core context 无 VAO 时 attrib 相关默认态均非法)
    m_vao = new QOpenGLVertexArrayObject(this);
    m_vao->create();
    m_vao->bind();
#endif
}

void CXRendererMpvGl::resizeGL(int w, int h)
{
    (void)w; (void)h; // mpv render 直取 fbo 尺寸
}

void CXRendererMpvGl::paintGL()
{
    CxMpvApi& api = CxMpvApi::instance();
    if (m_rc == 0 || api.mpv_render_context_render == 0)
        return;
    CxMpvOpenglFbo fbo;
    fbo.fbo = 0; // QGLWidget/QOpenGLWidget 默认 FBO
    fbo.w = width();
    fbo.h = height();
    fbo.internal_format = 0;
    int flipY = 1; // GL 原点在左下,mpv 自家渲需 y 翻转
    CxMpvRenderParam params[3];
    params[0].type = CxMpvRenderParamOpenGLFbo;
    params[0].data = &fbo;
    params[1].type = CxMpvRenderParamFlipY;
    params[1].data = &flipY;
    params[2].type = CxMpvRenderParamInvalid;
    params[2].data = 0;
    if (api.mpv_render_context_render((CxMpvRenderCtx*)m_rc, params) == 0 &&
        api.mpv_render_context_report_swap != 0)
        api.mpv_render_context_report_swap((CxMpvRenderCtx*)m_rc);
}

// ── 供 engine 调用的创建/销毁助手(集中在此,eng_mpv 只见 void*)──

void* cxMpvGlViewCreate(QWidget* parent)
{
    return new CXRendererMpvGl(parent);
}

void cxMpvGlViewSet(void* view, void* rc)
{
    if (view != 0)
        ((CXRendererMpvGl*)view)->setRenderObj(rc);
}

void cxMpvGlViewGeometry(void* view, int w, int h)
{
    if (view != 0)
        ((CXRendererMpvGl*)view)->setGeometry(0, 0, w, h);
}

void cxMpvGlViewDelete(void* view)
{
    delete (CXRendererMpvGl*)view;
}

// GL context current 前提下出 init 参数(须在 mpv_render_context_create 前)
void cxMpvGlViewInitParams(CxMpvOpenglInitParams* ip)
{
    ip->get_proc_address = cxMpvGlProcThunk;
    ip->get_proc_address_ctx = 0;
}

// update 回调闭包:参数只带 this 指针即可(单例表)
void cxMpvGlStartNotify(void* view)
{
    cxMpvGlView = (CXRendererMpvGl*)view;
}

void cxMpvGlStopNotify()
{
    cxMpvGlView = 0;
}
