#ifndef RENDERER_MPVGL_H
#define RENDERER_MPVGL_H

// M4-B:mpv render_context GL 显示窗(detail.md §7.7)
// - 无 Q_OBJECT(无信号槽;moc 不解析条件基类段——renderer_gl 同款)
// - 不产自家几何:mpv_render_context_render 直渲当前 FBO,零自家 GL 状态
// - update 驱动:mpv 渲染线程回调 → QApplication::postEvent 自定事件进
//   GUI 线程(直接 QWidget::update 跨线程 UB),event() 醒来蹬 update()

#include "cxvideo_global.h"
#include "cxmpv_def.h" // CxMpvOpenglInitParams(cxMpvGlViewInitParams)

#include <qglobal.h> // QT_VERSION(_CHECK)
// Qt3/Qt4 同走 QGLWidget 固定管线(renderer_gl.h 同款判定)
#if defined(QT3_BUILD) || \
    (defined(QT_VERSION) && QT_VERSION < QT_VERSION_CHECK(5, 0, 0))
#define CXV_LEGACY_GL 1
#else
#define CXV_LEGACY_GL 0
#endif

#if CXV_LEGACY_GL
#include <qgl.h>
#else
#include <QOpenGLWidget>
#include <QOpenGLVertexArrayObject>
#endif
#include <qevent.h>

class CXRendererMpvGl
#if CXV_LEGACY_GL
    : public QGLWidget {
#else
    : public QOpenGLWidget {
#endif
public:
    explicit CXRendererMpvGl(QWidget* parent = 0);

    // 引擎侧一次性注入(render_context_create 之后;空=未挂不渲染)
    void setRenderObj(void* rc);

protected:
    bool event(QEvent* ev);

    void initializeGL();
    void resizeGL(int w, int h);
    void paintGL();

#if !CXV_LEGACY_GL
    QOpenGLVertexArrayObject* m_vao; // core profile 下无 VAO 默认即 INVALID_OPERATION
#endif
    void* m_rc; // mpv_render_context*(engine 注入)
};

// ── engine 侧助手(eng_mpv 只见 void*,不 include 实现细节)──
void* cxMpvGlViewCreate(QWidget* parent);
void  cxMpvGlViewSet(void* view, void* rc);
void  cxMpvGlViewGeometry(void* view, int w, int h);
void  cxMpvGlViewDelete(void* view);
void  cxMpvGlViewInitParams(CxMpvOpenglInitParams* ip);
void  cxMpvGlStartNotify(void* view);
void  cxMpvGlStopNotify();
void  cxMpvGlUpdateHook(void* ctx); // void(*)(void*) 传 set_update_callback

#endif // RENDERER_MPVGL_H
