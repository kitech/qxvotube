#ifndef CXENGFFMPEG_H
#define CXENGFFMPEG_H

#include "cxengine.h"
#include "cffsub.h"
#include "cxrenderer.h" // M3-B inline setEq/screenshot 需完整型
#include "cxffdec.h"    // M4-A inline hwName() 需完整型
#include "cxframe.h"    // CxLatestFrameSlot 完整型(帧槽成员)
#include <qstringlist.h>
#include <qdatetime.h>

// 自研 FFmpeg 内核(M2;detail.md 第 5 节)
// - M2.0:demux + vthread 双线程;sws→QImage 经事件回投渲染
// - M2.1:athread 音频链(swr→S16→AO;pulse/alsa/sdl/silent)+ CxClock 主时钟
// - M3.1:字幕(内嵌文本流 + 外挂 SRT)+ 轨道选择(切换 = 引擎重启式换轨)
class CxDemuxThread;
class CXFFmpegVideoThread;
class CXFFmpegAudioThread;
class CXFFmpegSubThread;
class CxDecoder;
class CxDecoderAudio;
class CxFFSubDecoder;
class CXAudioOut;
class CxClock;
class CxPacketQueue;
class AVFormatContext;
class CxFFmpegUiEvent;
class QTimer;
struct CxFFmpegOpenCtx;      // 异步打开上下文(eng_ffmpeg.cpp 内定义)
class CxFFmpegOpenThread;    // 异步打开线程(eng_ffmpeg.cpp 内定义)

class CXEngineFfmpeg : public CXEngine {
    Q_OBJECT
private slots:
    void onEosWatchdog();   // AO drain 卡死兜底:到点强制收尾
public:
    const char* name() const { return "ffmpeg"; }
    bool isAvailable() const { return true; }

    void setNetOptions(const CxNetOptions& o) { m_net = o; }
    bool open(const QString& url);
    bool openAsync(const QString& url);   // 非虚:仅本类提供,GUI 不阻塞
    void setRotation(int deg);            // 画面顺时针旋转(0/90/180/270)
    void close();
    bool play();
    bool pause();
    bool stop();
    bool seek(long ms);
    bool setRate(double r);
    bool setVolume(int pct);
    bool setMute(bool on);
    long position() const;
    long duration() const;
    long bufferedMs() const;     // demux 已读包前沿(网络缓冲)
    CxBufferInfo bufferInfo() const; // 缓冲四项(前沿/队列字节/上限/速度)
    const char* aoBackend() const; // M2.1:当前 AO 后端(诊断/自测)
    // M7:宽高/codec/码率/fps(open 时采集);hw/宽高/codec 再实时覆盖,
    // 因为硬解失败会在首帧时回落到软解(cxffdec 会清 m_hwName),不能快照。
    CxVideoInfo videoInfo() const
    {
        CxVideoInfo i = m_info;
        const char* hw = hwName();
        i.hw = hw != 0 ? QString::fromLatin1(hw) : QString();
        if (m_dec != 0) {
            if (m_dec->width() > 0)  i.width  = m_dec->width();
            if (m_dec->height() > 0) i.height = m_dec->height();
            if (!m_dec->codecName().isEmpty()) i.codec = m_dec->codecName();
        }
        return i;
    }

    // M3-A 轨道/字幕
    int audioTrackCount() const { return m_aCount; }
    int subtitleTrackCount() const { return m_sCount; }
    bool setAudioTrack(int idx);      // -1=关音轨(纯视频盲等)
    bool setSubtitleTrack(int idx);   // -1=关内嵌字幕
    bool setSubtitleFile(const QString& path);

    // M3-B:EQ 转渲染器软路;截图取渲染器当前帧(inline,p_renderer 基类项)
    bool setEq(const CXVideoEq& eq)
    {
        if (p_renderer == 0)
            return false;
        p_renderer->setVideoEq(eq);
        return true;
    }
    bool screenshot(const QString& path)
    {
        return p_renderer != 0 && p_renderer->saveScreenshot(path);
    }
    const char* hwName() const
    {
        return m_dec != 0 ? m_dec->hwName() : 0;
    }

    ~CXEngineFfmpeg();

protected:
    void customEvent(CustomEventBase* e);

private:
    friend CxDemuxThread;              // (占位,实际线程不访问引擎)
    friend class CXFFmpegVideoThread;
    friend CXEngine* cxEngineCreateDefault();
    friend CXEngine* cxEngineCreateFor(CxEngineType type);

    CXEngineFfmpeg();
    void stopThreads();
    bool finishOpen(AVFormatContext* fmt);   // 原 open() 后半段 setup
    void cancelOpen();                       // 作废/回收在途异步 open
    void resetOpenState(const QString& url); // 重置本次打开状态
    void dispatchEvent(const CxFFmpegUiEvent* ev);
    int eosPrimaryCount() const;  // 本次会话需等待的主流数(视频/音频)
    void finishEos();             // 全部主流排空后统一收尾(防重入)
    void cxScanSub(long ms); // 字幕轴查询/变化即 emit(仅 UI 线程)
    bool reopenAt(long posMs, bool wasPlaying); // 换轨重启

    CxNetOptions m_net;      // 直链代理/防盗链头
    void* m_fmtCtx;          // AVFormatContext*
    CxPacketQueue* m_vq;
    CxPacketQueue* m_aq;
    CxPacketQueue* m_sq;
    CxDecoder* m_dec;
    CxDecoderAudio* m_adec;
    CxFFSubDecoder* m_sdec;
    CxDemuxThread* m_demux;
    CXFFmpegVideoThread* m_vthread;
    CXFFmpegAudioThread* m_athread;
    CXFFmpegSubThread* m_sthread;
    CXAudioOut* m_ao;
    CxClock* m_clock;
    volatile bool m_paused;
    volatile bool m_running;
    long m_durMs;
    long m_curMs;
    int m_vIdx;
    int m_aIdx;
    int m_sIdx;
    int m_seekPend;
    long m_seekTarget;      /* 本次 seek 目标(丢弃迟到旧帧事件) */
    int m_eosPend; // 待到达的主流 EOS 数(各自 drain 完才减;字幕不参与)
    bool m_fatalErr;        // 本次会话出现过致命读错误(避免 finished 与 skip 双推进)
    QTimer* m_eosTimer;     // EOS 看门狗(3s;防 AO drain 阻塞导致不连播)
    int m_aCount;
    int m_sCount;
    int m_audioPrompt; // 换轨重启时按枚举序挑第 N 个音轨(-1=默认首个;-2=关)
    int m_subPrompt;   // 同上(字幕);-1=默认;-2=关
    double m_rateCur;  // 重启式换轨后重放(setRate/setVolume/setMute 落底)
    int m_volCur;
    bool m_muteCur;
    int m_rotation;            /* 0/90/180/270 */
    QImage m_lastFrameImg;     /* 最近一帧未旋转源(暂停时改角度立即重绘) */
    /* —— 异步 open 状态(集中一处;仅 GUI 线程读写) —— */
    struct OpenState {
        OpenState()
            : ctx(0), thread(0), ioAbort(false), gen(0), ready(false),
              playPending(false), startedOnce(false), pendingSeekMs(-1) {}
        CxFFmpegOpenCtx* ctx;        // 在途上下文;0=无
        CxFFmpegOpenThread* thread;  // 在途线程
        volatile bool ioAbort;       // 中断回调 opaque 恒指向它(生命周期=引擎)
        int gen;                     // 代际:open/cancel/close/换轨 +1
        bool ready;                  // open 完成、可 play/seek/写属性
        bool playPending;            // Opening 期已请求 play
        bool startedOnce;            // 本次 open 是否已发 started
        long pendingSeekMs;          // <0 无;Opening 期 seek 暂存
    };
    OpenState m_openState;
    CxVideoInfo m_info;        // M7:播放信息
    QString m_subFile;         // 外挂 SRT(open 重建后重挂)
    QString m_subCur;          // 当前已 emit 的字幕文本(变化才发)
    CxFFSubTimeline m_subTl;   // 内嵌 cue(open 清零重建)
    CxFFSubTimeline m_subTlExt; // 外挂 cue(独立保留,重启不清)
    CxLatestFrameSlot m_frameSlot;  // 视频帧槽:取代逐帧 QImage 事件,内存恒定 O(1)
};

#endif // CXENGFFMPEG_H
