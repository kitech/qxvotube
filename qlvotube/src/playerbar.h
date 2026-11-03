#ifndef PLAYERBAR_H
#define PLAYERBAR_H

#include "compat34.h"
#include "musicmodel.h"
#include <qwidget.h>

class PlayerBar : public QWidget {
    Q_OBJECT
public:
    explicit PlayerBar(QWidget* parent = 0);
    void setTrack(const Track& t);
    void setPlaying(bool on);
    bool isPlaying() const { return m_playing; }
    void setProgress(int ms);
    void setDuration(int ms);
    void retranslateUi();

signals:
    void playStateChanged(bool playing);
    void playPauseRequested();
    void prevRequested();
    void nextRequested();
    void seekRequested(int ms);

protected:
    void paintEvent(QPaintEvent* e);
    void mousePressEvent(QMouseEvent* e);
    void mouseMoveEvent(QMouseEvent* e);
    void mouseReleaseEvent(QMouseEvent* e);
    void leaveEvent(QEvent* e);

private:
    enum Hit { HitNone, HitPrev, HitPlay, HitNext, HitProgress };
    void setProgressFromX(int x);
    QRect playRect() const;
    QRect prevRect() const;
    QRect nextRect() const;
    QRect progressRect() const;

    Track m_track;
    bool m_hasTrack;
    bool m_playing;
    int m_progressMs;
    int m_durationMs;
    Hit m_hover;
    bool m_dragging;
};

#endif // PLAYERBAR_H
