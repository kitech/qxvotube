#ifndef QUEUEPANEL_H
#define QUEUEPANEL_H

#include "compat34.h"
#include "musicmodel.h"
#include <qwidget.h>

class QueuePanel : public QWidget {
    Q_OBJECT
public:
    explicit QueuePanel(QWidget* parent = 0);
    void setQueue(const std::vector<Track>& queue, int startIndex);
    void setCurrentTrack(const Track& t);
    void retranslateUi();

protected:
    void paintEvent(QPaintEvent* e);

private:
    Track m_current;
    bool m_hasCurrent;
    std::vector<Track> m_queue;
    int m_currentIdx;
    QString m_title;
};

#endif // QUEUEPANEL_H
