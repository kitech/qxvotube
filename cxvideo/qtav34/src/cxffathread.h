#ifndef CXFFATHREAD_H
#define CXFFATHREAD_H

#include "compat34.h"
#include "cxff_wrap.h"
#include "cxpqueue.h"
#include "cxffdeca.h"
#include "cffao.h"
#include "cxffclock.h"
#include "cxffvthread.h" // CxFFmpegUiEvent + CxFFmpegEvAudioPos/CxFFmpegEvFinished/CxFFmpegEvSeekDone
#include <qthread.h>
#include <qmutex.h>
#include <qwaitcondition.h>
#include <qstring.h> // QString 依存 via cxffvthread.h;此处仅编译 sane

// 音频线程(detail.md §5 CXAudioThread;QtAV 主时钟五要点):
// take → decode → swr(s16 obtained) → 倍速块丢/复补偿 → 软音量 →
// ao->write(阻塞) → clock->updateValue;EOF 先 ao->drain() 再 Finished;
// seek 由 setRenderPts0 丢旧包帧。
class CXFFmpegAudioThread : public QThread {
public:
    CXFFmpegAudioThread(CxPacketQueue* q, CxDecoderAudio* d, CXAudioOut* ao,
                        CxClock* clock, QObject* sink)
        : aQueue(q), dec(d), out(ao), m_clock(clock), sink(sink),
          stopFlag(false), paused(false), renderPts0(-1),
          volumePermil(1000), muted(false), rate(1.0),
          m_skipAcc(0), m_dupAcc(0), m_lastMs(-1),
          m_blkBuf(0), m_blkCap(0)
    {
    }

    void requestStop()
    {
        stopFlag = true;
        waking();
        if (m_clock != 0)
            m_clock->interrupt(true);
    }
    void setPaused(bool on)
    {
        paused = on;
        waking();
    }
    void setRenderPts0(long ms) { renderPts0 = ms; }
    void setRate(double r) { rate = (r > 0.0) ? r : 1.0; }
    void setVolume(int pct) // 0..100 → 0..1000(软缩放)
    {
        volumePermil = (pct <= 0) ? 0 : ((pct > 100) ? 1000 : pct * 10);
    }
    void setMute(bool on) { muted = on; }

    void waking()
    {
        QMutexLocker lk(&paceMx);
        paceCond.wakeAll();
    }

protected:
    void run();

private:
    void waitIfPaused();
    void deliver(AVFrame* frame);
    bool writePcmChunks(const uint8_t* src, int bytes, long baseMs,
                        int aCh, int aRate);
    bool writeScaled(const uint8_t* src, int n);
    bool cxFadeAndWrite(const uint8_t* src, int n, int bpf,
                        bool head, bool tail); // M3-B splice crossfade
    void ensureBuf(int bytes);
    void post(int ev, long ms);
    void tailFlush();

    CxPacketQueue* aQueue;
    CxDecoderAudio* dec;
    CXAudioOut* out;
    CxClock* m_clock;
    QObject* sink;
    volatile bool stopFlag;
    volatile bool paused;
    volatile long renderPts0;
    int volumePermil;
    bool muted;
    double rate;
    double m_skipAcc; // 快进:累计应丢样本数(块粒度丢)
    double m_dupAcc;  // 慢放:累计应补样本数(块粒度复写)
    long m_lastMs;    // 尾块结束时刻(tailFlush 时钟起点)
    QMutex paceMx;
    QWaitCondition paceCond;
    uint8_t* m_blkBuf;
    int m_blkCap;
};

#endif // CXFFATHREAD_H
