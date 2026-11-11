#include "renderer_quick.h"
#include "cxeq.h"

#ifndef QT3_BUILD

#include <QQuickWindow>
#include <QSGSimpleTextureNode>

CXQuickVideoItem::CXQuickVideoItem(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(QQuickItem::ItemHasContents, true);
}

void CXQuickVideoItem::showFrame(const CXVideoFrame* f)
{
    if (f == 0 || f->img.isNull())
        return;
    QImage img = f->img; // RGB32(eng_ffmpeg sws 已转好)
    if (!cxEqNeutral(m_eq)) {
        unsigned char lut[256];
        cxEqBuildLut(m_eq, lut);
        img = cxEqApplyRgb(img, lut, cxEqSatPermil(m_eq)); // 与软渲/GL 同源
    }
    m_pendingImg = img;
    m_lastImg = img;
    update(); // 渲染线程侧 updatePaintNode
}

void CXQuickVideoItem::setViewport(const QRect& r) { m_viewport = r; }

void CXQuickVideoItem::setVideoEq(const CXVideoEq& eq) { m_eq = eq; }

bool CXQuickVideoItem::saveScreenshot(const QString& path)
{
    return m_lastImg.isNull() ? false : m_lastImg.save(path, "PNG");
}

QSGNode* CXQuickVideoItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*)
{
    QSGSimpleTextureNode* node = static_cast<QSGSimpleTextureNode*>(old);
    if (node == 0) {
        node = new QSGSimpleTextureNode();
        node->setOwnsTexture(true);
    }
    QImage img = m_pendingImg; // 值拷贝,渲染线程安全
    if (!img.isNull()) {
        // letterbox:内容区等比缩进 item rect(与 renderer_gl uScale 同语义)
        QRect r = m_viewport.isValid() ? m_viewport : boundingRect().toRect();
        qreal sc = qMin(qreal(r.width()) / img.width(),
                        qreal(r.height()) / img.height());
        node->setRect(QRect((r.width() - int(img.width() * sc)) / 2,
                            (r.height() - int(img.height() * sc)) / 2,
                            int(img.width() * sc), int(img.height() * sc)));
        node->setTexture(window()->createTextureFromImage(img));
    }
    return node;
}

#endif // !QT3_BUILD
