#ifndef CXENGFFMPEG_H
#define CXENGFFMPEG_H

#include "cxengine.h"

// 自研 FFmpeg 内核(M2;detail.md 第 5 节)
// - M2.0:demux + vthread 双线程;sws→QImage 经事件回投渲染
// - M2.1:athread 音频链(swr→S16→AO;pulse/alsa/sdl/silent)+ CxClock 主时钟
// - isAvailable():被编译进(WITH_CX_ENGINE_FFMPEG pkg-config 探测,缺则关宏,引擎不存在)
class CxDemuxThread;
class CXFFmpegVideoThread;
class CXFFmpegAudioThread;
class CxDecoder;
class CxDecoderAudio;
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

    void* m_fmtCtx;          // AVFormatContext*
    CxPacketQueue* m_vq;
    CxPacketQueue* m_aq;
    CxDecoder* m_dec;
    CxDecoderAudio* m_adec;
    CxDemuxThread* m_demux;
    CXFFmpegVideoThread* m_vthread;
    CXFFmpegAudioThread* m_athread;
    CXAudioOut* m_ao;
    CxClock* m_clock;
    volatile bool m_paused;
    volatile bool m_running;
    long m_durMs;
    long m_curMs;
    int m_vIdx;
    int m_aIdx;
    int m_seekPend;
    int m_eosPend; // 活动线程 EOS 计数(视频+音频都完才 finished)
};

#endif // CXENGFFMPEG_H
