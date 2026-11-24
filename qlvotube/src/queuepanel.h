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
    void setCurrentIndex(int index);   /* 仅更新高亮行,不动队列/滚动 */
    void retranslateUi();
    void dismissTip();                 /* Esc/外部主动收起标题提示 */

protected:
    void paintEvent(QPaintEvent* e);
    void wheelEvent(QWheelEvent* e);
    void resizeEvent(QResizeEvent* e);
    void customEvent(CustomEventBase* e);   // 封面解码完成
    void mouseMoveEvent(QMouseEvent* e);    // 悬停命中 + 延时
    void mousePressEvent(QMouseEvent* e);   // 点击即收
    void leaveEvent(QEvent* e);             // 移出即收
    void hideEvent(QHideEvent* e);          // 面板隐藏即收

private:
    void updateScrollBar();
    QString queueTitle() const;             // "播放队列 (i/total)"
    void cancelTip();                       // 停表 + 隐藏 + 清目标
    QRect currentTitleRect() const;         // 封面下方大标题矩形
    QRect rowTitleRect(int row) const;      // 列表行标题矩形

private slots:
    void onScroll(int v);
    void onTipTimer();                      // 500ms 到点显示

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
    /* 标题 Tooltip:-1=无,-2=当前大标题,>=0=列表行;timer 活动=延时中 */
    int m_tipRow;
    QTimer* m_tipTimer;
};

#endif // QUEUEPANEL_H
