#ifndef CXENGFFMPEG_H
#define CXENGFFMPEG_H

#include "cxengine.h"

// 自研 FFmpeg 内核(M2;detail.md 第 5 节)
// - M2.0:demux + vthread 双线程;sws→QImage 经事件回投渲染;无音频主时钟
// - isAvailable():被编译进(WITH_CX_ENGINE_FFMPEG pkg-config 探测,缺则关宏,引擎不存在)
class CxDemuxThread;
class CXFFmpegVideoThread;
class CxDecoder;
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
    CxDemuxThread* m_demux;
    CXFFmpegVideoThread* m_vthread;
    volatile bool m_paused;
    volatile bool m_running;
    long m_durMs;
    long m_curMs;
    int m_vIdx;
    int m_aIdx;
};

#endif // CXENGFFMPEG_H
