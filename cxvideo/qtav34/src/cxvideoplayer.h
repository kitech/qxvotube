#ifndef CXVIDEOPLAYER_H
#define CXVIDEOPLAYER_H

#include "cxvideo_global.h"
#include <qobject.h>
#include <qstring.h>
#include "cxengine.h"

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

    // 直链网络选项(代理/防盗链头):setEngine 之后、open 之前调用;
    // 引擎已存在时立即透传
    void setNetOptions(const CxNetOptions& o);
    const CxNetOptions& netOptions() const { return m_net; }

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
    bool saveScreenshot(const QString& path); // 渲染器当前帧(LUT 后)

    long position() const;
    long duration() const;
    long bufferedMs() const;     // 转发当前引擎缓冲前沿
    // M7:播放信息(转发当前引擎;无引擎=全空)
    CxVideoInfo videoInfo() const;
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
    void videoInfoChanged();     // M7
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
    CxNetOptions m_net;
    CXVideoState m_state;
    double m_rate;
    int m_volume;
    bool m_muted;
};

#endif // CXVIDEOPLAYER_H
