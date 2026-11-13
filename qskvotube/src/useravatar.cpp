#include "useravatar.h"

#include "identicon.h"   // qlcomp:generateIdenticon(seed, size)

#include <QPainter>
#include <QPainterPath>
#include <QQuickPaintedItem>

// 内层自绘项:随父 item 尺寸伸缩,画圆形裁切的 identicon
class CxAvatarPaintItem : public QQuickPaintedItem
{
public:
    explicit CxAvatarPaintItem(QQuickItem* parent = nullptr)
        : QQuickPaintedItem(parent)
    {
        setAntialiasing(true);
    }

    void setPixmap(const QPixmap& pix)
    {
        m_pix = pix;
        update();
    }

    void paint(QPainter* painter) override
    {
        if (m_pix.isNull() || width() <= 0 || height() <= 0)
            return;
        painter->setRenderHint(QPainter::Antialiasing, true);
        QPainterPath path;
        path.addEllipse(boundingRect());
        painter->setClipPath(path);
        painter->drawPixmap(boundingRect().toRect(), m_pix);
    }

private:
    QPixmap m_pix;
};

CxUserAvatar::CxUserAvatar(QQuickItem* parent)
    : QskControl(parent)
{
    m_paint = new CxAvatarPaintItem(this);
    connect(this, &QQuickItem::widthChanged, this, [this]() {
        if (m_paint != 0) {
            m_paint->setWidth(width());
            m_paint->setHeight(height());
        }
    });
    connect(this, &QQuickItem::heightChanged, this, [this]() {
        if (m_paint != 0) {
            m_paint->setWidth(width());
            m_paint->setHeight(height());
        }
    });
    setSeed(QStringLiteral("anonymous"));
}

void CxUserAvatar::setSeed(const QString& seed)
{
    if (seed == m_seed && !m_pix.isNull())
        return;
    m_seed = seed;
    rebuild();
}

void CxUserAvatar::rebuild()
{
    // 固定 64px 生成(布局 28dp×dpr 足够清晰,缩放由 painter 处理)
    m_pix = generateIdenticon(m_seed, 64);
    if (m_paint != 0)
        m_paint->setPixmap(m_pix);
}
