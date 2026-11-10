#ifndef CXENGINE_H
#define CXENGINE_H

#include "cxvideo_global.h"
#include <qobject.h>
#include <qstring.h>

class CXVideoRenderer;
class QWidget;

struct CXVideoEq;

// 引擎抽象(detail.md 第 3 节;UI 线程 API,线程→UI 经事件回投)
class CXEngine : public QObject {
    Q_OBJECT
public:
    virtual ~CXEngine() { }

    virtual const char* name() const = 0;
    virtual bool isAvailable() const = 0;

    virtual bool open(const QString& url) { m_url = url; return true; }
    virtual void close() { m_url = QString(); }

    virtual bool play() = 0;
    virtual bool pause() = 0;
    virtual bool stop() = 0;
    virtual bool seek(long /*ms*/) { return false; }
    virtual bool setRate(double /*r*/) { return false; }
    virtual bool setVolume(int /*pct*/) { return false; }
    virtual bool setMute(bool /*on*/) { return false; }

    virtual long position() const { return 0; }
    virtual long duration() const { return 0; }

    // M3-A:轨道选择 + 外挂字幕(不支持的能力各自返回 false/0;cmdmpv 即此形)
    virtual int audioTrackCount() const { return 0; }
    virtual int subtitleTrackCount() const { return 0; }
    virtual bool setAudioTrack(int /*idx*/) { return false; }      // -1=关闭
    virtual bool setSubtitleTrack(int /*idx*/) { return false; }   // -1=关闭
    virtual bool setSubtitleFile(const QString& /*path*/) { return false; }

    // M3-B:EQ 先锁名(默认 false)
    virtual bool setEq(const CXVideoEq& /*eq*/) { return false; }

    // M1 起挂渲染器(renderer 由引擎侧驱动 showFrame)
    virtual void setRenderer(CXVideoRenderer* r) { p_renderer = r; }
    CXVideoRenderer* renderer() const { return p_renderer; }

    // 宿主窗口(mpv wid 内嵌用;Qt3 渲染主路)
    virtual void setHostWidget(QWidget* w) { p_host = w; }
    QWidget* hostWidget() const { return p_host; }

    const QString& url() const { return m_url; }

signals:
    void started();
    void paused();
    void stopped();
    void finished();
    void noticedError(int code, const QString& msg); // 避免与 QObject::error 撞名(Qt4)
    void positionChanged(long ms);
    void durationChanged(long ms);
    void seekFinished(long ms);
    void subtitleChanged(const QString& text); // 空串 = 当前无;connect 规格用 const QString&

protected:
    CXEngine() : QObject(), p_renderer(0), p_host(0) { }

    QString m_url;
    CXVideoRenderer* p_renderer;
    QWidget* p_host;
};

// 工厂:按类型创建(未实现内核返回 0);Auto→cxEngineCreateDefault
CXEngine* cxEngineCreateFor(CxEngineType type);
// auto 链:cmdmpv → libmpv(dlopen)→ ffmpeg(M2);全缺返回 0
CXEngine* cxEngineCreateDefault();

#endif // CXENGINE_H
