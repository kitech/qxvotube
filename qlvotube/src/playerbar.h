#ifndef PLAYERBAR_H
#define PLAYERBAR_H

#include "qlcompat34.h"
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
    void setMuted(bool on);              /* 二期同步:静音状态 */
    bool isMuted() const { return m_muted; }
    /* 播放模式(qlmusic 同款) */
    void setRepeatMode(int mode);        /* 0=off 1=all 2=one */
    void setShuffle(bool on);
    void setAutoSkipUnplayable(bool on); /* 无法播放时自动下一条 */
    /* 下载进度 pie:0-100 / -1 未定 / -2 失败 / 100 完成(短暂留痕后隐) */
    void setDownloadState(int pct);
    /* 缓冲信息:比例=已缓冲字节/后端上限;秒数/字节/速度同刷新;常驻显示 */
    void setBufferInfo(long bufferedMs, long bufferedBytes,
                       long maxBytes, long bytesPerSec);
    void setBufferedMs(long ms) { setBufferInfo(ms, 0, 0, 0); } /* 复位/兼容 */
    /* 底部单行字幕(YouTube via yt-dlp);空串=不显示 */
    void setSubtitleLine(const QString& text);
    void retranslateUi();
    void toggleVolumePopup();
    void onPopupVolume(int v);           /* 自绘滑条 → volumeChanged */

signals:
    void playStateChanged(bool playing);
    void playPauseRequested();
    void videoSurfaceRequested();   // 点封面/标题 → 回视频播放页
    void prevRequested();
    void nextRequested();
    void seekRequested(int ms);
    /* M5-A2:音量/倍速/EQ/截图 */
    void volumeChanged(int pct);
    void rateChanged(double r);
    void rateMenuRequested(const QPoint& globalPos);   /* 倍速按钮 → 请求弹出菜单 */
    void rotateMenuRequested(const QPoint& globalPos);  /* 旋转按钮 → 弹出菜单 */
    void muteChanged(bool on);           /* 二期同步:静音切换(qskvotube 对齐) */
    void eqRequested();
    void shotRequested();
    void subtitleRequested();   /* M5-A3:字幕轨循环 */
    void subtitleListRequested(); /* 点击底部字幕行 → 打开字幕列表 */
    void repeatClicked();
    void shuffleClicked();
    void autoSkipUnplayableClicked();

protected:
    void paintEvent(QPaintEvent* e);
    void mousePressEvent(QMouseEvent* e);
    void mouseMoveEvent(QMouseEvent* e);
    void mouseReleaseEvent(QMouseEvent* e);
    void leaveEvent(QEvent* e);
    void wheelEvent(QWheelEvent* e);     /* 滚轮 = 音量 ±5 */
    void customEvent(CustomEventBase* e);   // 封面解码完成

private slots:
    void onMuteToggled(bool on);         /* 弹层静音勾选 */
    void onDlHide();

private:
    enum Hit { HitNone, HitPrev, HitPlay, HitNext, HitProgress,
               HitVol, HitRate, HitRotate, HitEq, HitShot, HitSub, HitDownload, HitBuffered,
               HitRepeat, HitShuffle, HitAutoSkipUnplayable, HitSubLine };
    void setProgressFromX(int x);
    QRect coverRect() const;
    QRect titleRect() const;
    QRect playRect() const;
    QRect prevRect() const;
    QRect nextRect() const;
    QRect progressRect() const;
    QRect volRect() const;
    QRect rateRect() const;
    QRect rotateRect() const;
    QRect eqRect() const;
    QRect shotRect() const;
    QRect ccRect() const;
    QRect repeatRect() const;
    QRect shuffleRect() const;
    QRect autoSkipUnplayableRect() const;
    QRect subtitleLineRect() const;
    QRect downloadRect() const;
    QRect bufferedRect() const;

    Track m_track;
    bool m_hasTrack;
    bool m_playing;
    int m_progressMs;
    int m_durationMs;
    Hit m_hover;
    bool m_dragging;
    int m_volume;
    int m_lastVolume;                    /* 静音记忆(取消静音回位) */
    double m_rate;
    bool m_muted;
    int m_repeatMode;
    bool m_shuffle;
    bool m_autoSkipUnplayable;
    int m_dlPct;                         /* -1 未定 / 0-100 / -2 失败 */
    bool m_dlVisible;
    long m_bufferedMs;                   /* 已缓冲总时长(ms),0=未知 */
    long m_bufferedBytes;                /* 已缓冲字节,0=未知 */
    long m_maxBytes;                     /* 后端最大缓冲字节,0=未知 */
    long m_bytesPerSec;                  /* 缓冲速度(bytes/s),0=未知 */
    QString m_subtitleLine;              /* 底部当前字幕行(空=不显示) */
    void* m_dlHideTimer;                 /* QTimer* */
    void* m_volumePopup;                 /* VolumePopupFrame* */
    void* m_volumeSlider;                /* VolumePanelSlider* */
    void* m_muteCheck;                   /* QCheckBox* */
};

#endif // PLAYERBAR_H
