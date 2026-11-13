#ifndef USERAVATAR_H
#define USERAVATAR_H

#include <QskControl.h>
#include <QPixmap>
#include <QString>

class CxAvatarPaintItem;

// 圆形 identicon 头像(与 qlvotube/qxmusic 同视觉)。
// 外层 QskControl 负责布局尺寸(setPreferredSize 等),内层 QQuickPaintedItem
// 自绘(QskControl::updatePaintNode 为 final,不能直接重写)。
class CxUserAvatar : public QskControl
{
    Q_OBJECT
public:
    explicit CxUserAvatar(QQuickItem* parent = nullptr);

    void setSeed(const QString& seed);
    QString seed() const { return m_seed; }

private:
    void rebuild();

    QString m_seed;
    QPixmap m_pix;
    CxAvatarPaintItem* m_paint = nullptr;
};

#endif
