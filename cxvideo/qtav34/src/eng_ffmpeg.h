#ifndef CXENGFFMPEG_H
#define CXENGFFMPEG_H

#include "cxengine.h"
#include "cffsub.h"
#include "cxrenderer.h" // M3-B inline setEq/screenshot 需完整型
#include "cxffdec.h"    // M4-A inline hwName() 需完整型
#include <qstringlist.h>

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

class CXEngineFfmpeg : public CXEngine {
    Q_OBJECT
public:
    const char* name() const { return "ffmpeg"; }
    bool isAvailable() const { return true; }

    bool open(const QString& url);
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
    const char* aoBackend() const; // M2.1:当前 AO 后端(诊断/自测)

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
    void dispatchEvent(const CxFFmpegUiEvent* ev);
    void cxScanSub(long ms); // 字幕轴查询/变化即 emit(仅 UI 线程)
    bool reopenAt(long posMs, bool wasPlaying); // 换轨重启

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
    int m_eosPend; // 活动线程 EOS 计数(视频+音频+字幕都完才 finished)
    int m_aCount;
    int m_sCount;
    int m_audioPrompt; // 换轨重启时按枚举序挑第 N 个音轨(-1=默认首个;-2=关)
    int m_subPrompt;   // 同上(字幕);-1=默认;-2=关
    double m_rateCur;  // 重启式换轨后重放(setRate/setVolume/setMute 落底)
    int m_volCur;
    bool m_muteCur;
    QString m_subFile;         // 外挂 SRT(open 重建后重挂)
    QString m_subCur;          // 当前已 emit 的字幕文本(变化才发)
    CxFFSubTimeline m_subTl;   // 内嵌 cue(open 清零重建)
    CxFFSubTimeline m_subTlExt; // 外挂 cue(独立保留,重启不清)
};

#endif // CXENGFFMPEG_H
