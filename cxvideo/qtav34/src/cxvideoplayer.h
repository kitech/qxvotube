#ifndef CXVIDEOPLAYER_H
#define CXVIDEOPLAYER_H

#include "cxvideo_global.h"
#include <qobject.h>
#include <qstring.h>

class CXEngine;
class CXVideoRenderer;
class QWidget;
struct CXVideoEq;

// 公共编排层(QtAV 风格照搬,detail.md 第 2/3 节)
class CXVideoPlayer : public QObject {
    Q_OBJECT
public:
    explicit CXVideoPlayer(QObject* parent = 0);
    ~CXVideoPlayer();

    // 引擎选择;auto = 工厂探测(mpv→ffmpeg)
    bool setEngine(CxEngineType type);
    CXEngine* engine() const { return m_engine; }

    void setRenderer(CXVideoRenderer* r);
    void setHostWidget(QWidget* w);

    bool open(const QString& url);
    void close();
    void play();
    void pause();
    void stop();
    void seek(long ms);
    void setRate(double r);
    void setVolume(int pct);   // 0~100
    void setMute(bool on);

    // M3-A:轨道 + 外挂字幕(passthrough;能力按内核各有支持/不支持)
    int audioTrackCount() const;
    int subtitleTrackCount() const;
    bool setAudioTrack(int idx);
    bool setSubtitleTrack(int idx);
    bool setSubtitleFile(const QString& path);
    // M3-B:EQ(passthrough,先锁名)
    bool setEq(const CXVideoEq& eq);

    long position() const;
    long duration() const;
    double rate() const { return m_rate; }
    int volume() const { return m_volume; }
    bool isMuted() const { return m_muted; }
    CXVideoState state() const { return m_state; }

signals:
    void started();
    void paused();
    void stopped();
    void finished();
    void mediaStatusChanged(int state);
    void positionChanged(long ms);
    void durationChanged(long ms);
    void seekFinished(long ms);
    void noticedError(int code, const QString& msg);
    void subtitleChanged(const QString& text); // 空串 = 当前无

private slots:
    void engineStarted();
    void enginePaused();
    void engineStopped();
    void engineFinished();
    void enginePositionChanged(long ms);
    void engineDurationChanged(long ms);
    void engineSeekFinished(long ms);
    void engineError(int code, const QString& msg);

private:
    void setState(CXVideoState s);
    void wireEngine();
    void unwireEngine();

    CXEngine* m_engine;
    CXVideoState m_state;
    double m_rate;
    int m_volume;
    bool m_muted;
};

#endif // CXVIDEOPLAYER_H
