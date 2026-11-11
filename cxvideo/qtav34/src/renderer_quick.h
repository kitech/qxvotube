#ifndef RENDERER_QUICK_H
#define RENDERER_QUICK_H

// M5-B:Quick 域 ffmpeg 渲染(QQuickItem + QSGSimpleTextureNode;纯 RHI 无 GL)
// - showFrame 恒 UI 线程(引擎事件投递保证),直接存帧 + update()
// - 无 Q_OBJECT(moc 不解析条件基类段;renderer_gl 同款先例)
#ifndef QT3_BUILD

#include "cxrenderer.h"
#include <QQuickItem>
#include <qimage.h>

struct CXVideoEq;

class CXQuickVideoItem : public QQuickItem, public CXVideoRenderer {
public:
    explicit CXQuickVideoItem(QQuickItem* parent = 0);

    void showFrame(const CXVideoFrame* f);
    void setViewport(const QRect& contentRect);
    void setVideoEq(const CXVideoEq& eq);
    bool saveScreenshot(const QString& path);

    const char* backendName() const { return "rhi-texture"; }

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData* d);

private:
    QImage m_pendingImg;  // UI 线程写侧
    QImage m_lastImg;     // 截图备件
    CXVideoEq m_eq;
    QRect m_viewport;
};

#endif // !QT3_BUILD
#endif // RENDERER_QUICK_H
