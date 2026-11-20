#ifndef CXENGINE_H
#define CXENGINE_H

#include "cxvideo_global.h"
#include <qobject.h>
#include <qstring.h>

class CXVideoRenderer;
class QWidget;

struct CXVideoEq;

// 网络选项(播放直链需要的代理/防盗链头);空 = 直连、不加头
struct CxNetOptions {
    QString proxy;      // "http://host:port"
    QString referer;
    QString userAgent;
    bool isEmpty() const {
        return proxy.isEmpty() && referer.isEmpty() && userAgent.isEmpty();
    }
};

// 播放信息(M7):播放器详情弹窗 + 顶栏分辨率共用。
// 取不到的字段一律留空/0,UI 显示 N/A —— 不猜、不造假。
struct CxVideoInfo {
    QString url;                // 当前播放直链(可能带签名)
    int     width;              // 解码器输出宽
    int     height;             // 解码器输出高
    QString codec;              // h264/hevc/av1...
    QString hw;                 // vaapi/cuda/...;空=软解
    qint64  bitrateKbps;        // 容器/流码率;0=未知
    double  fps;                // avg_frame_rate;0=未知
    QString quality;            // 实际档位(Stream.quality)
    QString requestedQuality;   // 用户请求档位
    bool    degraded;           // 请求档位被降级
    double  dpr;                // 设备像素比(显示侧采集;1.0=标准/未知)
    CxVideoInfo()
        : width(0), height(0), bitrateKbps(0), fps(0), degraded(false), dpr(1.0) {}
};

// 缓冲信息:四项均由后端实测;取不到的字段为 0(不猜、不造假)。
// maxBytes = 后端允许的最大缓冲字节上限;mpv 取 options/demuxer-max-bytes,
// FFmpeg 无对应选项,统一用 CX_BUFFER_MAX_BYTES_DEFAULT(与 mpv 默认 150MiB 对齐)。
struct CxBufferInfo {
    long bufferedMs;      // 已缓冲总时长(ms)
    long bufferedBytes;   // 当前已缓冲字节
    long maxBytes;        // 最大缓冲字节上限
    long bytesPerSec;     // 缓冲速度(bytes/s,1 秒窗口)
    CxBufferInfo() : bufferedMs(0), bufferedBytes(0), maxBytes(0), bytesPerSec(0) {}
};
#define CX_BUFFER_MAX_BYTES_DEFAULT (150L * 1024L * 1024L)

// 引擎抽象(detail.md 第 3 节;UI 线程 API,线程→UI 经事件回投)
class CXEngine : public QObject {
    Q_OBJECT
public:
    virtual ~CXEngine() { }

    virtual const char* name() const = 0;
    virtual bool isAvailable() const = 0;

    // 直链网络选项(默认空实现;引擎在 open() 时使用)
    virtual void setNetOptions(const CxNetOptions&) { }

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
    // 缓冲前沿(ms):后端已从网络读到、可供播放的最后时间点;
    // 0=未知/不支持/本地完整(不伪造)。各后端:
    //   ffmpeg = demux 已读包 PTS 最大值(见 cxffdemux.h);
    //   libmpv = demuxer-cache-time,回退 demuxer-cache-duration;
    //   cmdmpv = 进程型无 IPC/属性通道,恒 0。
    virtual long bufferedMs() const { return 0; }
    // 缓冲四项快照(默认仅回带 bufferedMs;其余能力由各后端自行实现)
    virtual CxBufferInfo bufferInfo() const {
        CxBufferInfo b;
        b.bufferedMs = bufferedMs();
        return b;
    }

    // M3-A:轨道选择 + 外挂字幕(不支持的能力各自返回 false/0;cmdmpv 即此形)
    virtual int audioTrackCount() const { return 0; }
    virtual int subtitleTrackCount() const { return 0; }
    virtual bool setAudioTrack(int /*idx*/) { return false; }      // -1=关闭
    virtual bool setSubtitleTrack(int /*idx*/) { return false; }   // -1=关闭
    virtual bool setSubtitleFile(const QString& /*path*/) { return false; }

    // M3-B:EQ 先锁名(默认 false)
    virtual bool setEq(const CXVideoEq& /*eq*/) { return false; }
    // M3-B:截图(默认 false;cmdmpv/libmpv 不承诺)
    virtual bool screenshot(const QString& /*path*/) { return false; }
    // M4-A:硬解名("vaapi"/"cuda";0=软解;cmdmpv 恒 0)
    virtual const char* hwName() const { return 0; }

    // M7:播放信息(默认全空;ffmpeg/mpv 各自填)
    virtual CxVideoInfo videoInfo() const { return CxVideoInfo(); }

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
    void videoInfoChanged();     // M7:打开完成/换轨后触发(UI 刷新顶栏/详情)
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
