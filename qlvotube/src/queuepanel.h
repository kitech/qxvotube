#ifndef QUEUEPANEL_H
#define QUEUEPANEL_H

#include "qlcompat34.h"
#include "musicmodel.h"
#include "LimeScrollBar.h"
#include <qwidget.h>

class LimeScrollBar;

class QueuePanel : public QWidget {
    Q_OBJECT
public:
    explicit QueuePanel(QWidget* parent = 0);
    void setQueue(const std::vector<Track>& queue, int startIndex);
    void setCurrentTrack(const Track& t);
    void retranslateUi();

protected:
    void paintEvent(QPaintEvent* e);
    void wheelEvent(QWheelEvent* e);
    void resizeEvent(QResizeEvent* e);
    void customEvent(CustomEventBase* e);   // 封面解码完成

private:
    void updateScrollBar();

private slots:
    void onScroll(int v);

private:
    Track m_current;
    bool m_hasCurrent;
    std::vector<Track> m_queue;
    int m_currentIdx;
    LimeScrollBar* m_scrollBar;
    int m_scrollY;
    int m_scrollDelta;
    int m_listTop;      /* 曲目列表区顶部（paint 时按封面实高刷新） */
    QString m_title;
};

#endif // QUEUEPANEL_H
