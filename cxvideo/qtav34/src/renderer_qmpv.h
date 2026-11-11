#ifndef RENDERER_QMPV_H
#define RENDERER_QMPV_H

// M5-B:mpv Quick 渲染(QQuickFramebufferObject;要求窗口 GL API)
// - render_context 由引擎懒挂:首帧 render() 时 context current 才 create
//   (engine->attachQuickRender();proc thunk 用 currentContext,renderer_mpvgl 同款)
// - update 泵:mpv 线程回调 → postEvent(User+4710) → item update()(同型)
// - 无 Q_OBJECT(renderer_mpvgl 同款先例)
#ifndef QT3_BUILD

#include "cxrenderer.h"
#include <QQuickFramebufferObject>
#include <qimage.h>

class CXEngineMpv;

class CXQuickMpvFboItem : public QQuickFramebufferObject, public CXVideoRenderer {
public:
    explicit CXQuickMpvFboItem(QQuickItem* parent = 0);
    ~CXQuickMpvFboItem();

    void bindEngine(CXEngineMpv* eng); // setQuickRender(true) 须在 engine open 前
    void showFrame(const CXVideoFrame* f); // mpv 路不消费视频帧,留空
    void setViewport(const QRect& r);
    void setVideoEq(const CXVideoEq& eq) { } // EQ 走引擎 mpv 属性直写
    bool saveScreenshot(const QString& path); // 转发引擎 screenshot-to-file

    const char* backendName() const { return "mpv-qfbo"; }
    bool attached() const { return m_attached; } // 探针断言位

    class Renderer : public QQuickFramebufferObject::Renderer {
    public:
        explicit Renderer(CXQuickMpvFboItem* o) : owner(o) { }
        void synchronize(QQuickFramebufferObject* o);
        void render();
        QOpenGLFramebufferObject* createFramebufferObject(const QSize& size);
    private:
        CXQuickMpvFboItem* owner;
    };

    QQuickFramebufferObject::Renderer* createRenderer() const {
        return new Renderer(const_cast<CXQuickMpvFboItem*>(this));
    }

protected:
    bool event(QEvent* ev); // User+4710 泵

private:
    friend class Renderer;
    CXEngineMpv* m_eng;
    bool m_attached;
};

// update 钩子(eng_mpv quick 路回调入口;renderer_qmpv.cpp 导出)
void cxMpvQuickUpdateHook(void* ctx);

#endif // !QT3_BUILD
#endif // RENDERER_QMPV_H
