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
    int volume() const { return m_volume; }
    double rate() const { return m_rate; }
    void setVolume(int pct);
    void setRate(double r);
    void retranslateUi();

signals:
    void playStateChanged(bool playing);
    void playPauseRequested();
    void prevRequested();
    void nextRequested();
    void seekRequested(int ms);
    /* M5-A2:音量/倍速/EQ/截图 */
    void volumeChanged(int pct);
    void rateChanged(double r);
    void eqRequested();
    void shotRequested();
    void subtitleRequested();   /* M5-A3:字幕轨循环 */

protected:
    void paintEvent(QPaintEvent* e);
    void mousePressEvent(QMouseEvent* e);
    void mouseMoveEvent(QMouseEvent* e);
    void mouseReleaseEvent(QMouseEvent* e);
    void leaveEvent(QEvent* e);

private:
    enum Hit { HitNone, HitPrev, HitPlay, HitNext, HitProgress,
               HitVol, HitRate, HitEq, HitShot, HitSub };
    void setProgressFromX(int x);
    void setVolumeFromX(int x);
    QRect playRect() const;
    QRect prevRect() const;
    QRect nextRect() const;
    QRect progressRect() const;
    QRect volRect() const;
    QRect rateRect() const;
    QRect eqRect() const;
    QRect shotRect() const;
    QRect ccRect() const;

    Track m_track;
    bool m_hasTrack;
    bool m_playing;
    int m_progressMs;
    int m_durationMs;
    Hit m_hover;
    bool m_dragging;
    bool m_volDrag;
    int m_volume;
    double m_rate;
};

#endif // PLAYERBAR_H
