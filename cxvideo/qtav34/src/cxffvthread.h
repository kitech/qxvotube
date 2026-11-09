#ifndef CXFFVTHREAD_H
#define CXFFVTHREAD_H

#include "compat34.h"
#include "cxff_wrap.h"
#include "cxpqueue.h"
#include "cxffdec.h"
#include "cxffclock.h"
#include "cffao.h"
#include "cxframe.h"
#include <qthread.h>
#include <qmutex.h>
#include <qwaitcondition.h>
#include <qapplication.h>
#include <qobject.h>
#include <qstring.h>
#include <qimage.h>

// ffmpeg 自定事件(复用 M1 pump 的 CustomEventBase 模式;避开 4002 mpv)
const int kcxFFmpegEventRaw = 4004;

enum CxFFmpegEv {
    CxFFmpegEvFrame = 1,      // 一帧到 UI(CXVideoFrame 所有权移交 customEvent)
    CxFFmpegEvFinished = 2,   // EOS
    CxFFmpegEvSeekDone = 3,   // ms = 到位锚点
    CxFFmpegEvError = 4,      // code
    CxFFmpegEvDuration = 5,   // ms
    CxFFmpegEvAudioPos = 6    // 音频主时钟推进(athread 块级)
};

class CxFFmpegUiEvent : public CustomEventBase {
public:
    CxFFmpegUiEvent(int ev, long ms, int code, CXVideoFrame* f = 0)
        : CustomEventBase(toEventType34(kcxFFmpegEventRaw)),
          ev(ev), ms(ms), code(code), frame(f) { }
    ~CxFFmpegUiEvent() { delete frame; }

    int ev;
    long ms;
    int code;
    CXVideoFrame* frame;
};

// 解码线程(detail.md §5 CXVideoThread;M2.1:音轨在时以主时钟校准,
// 音频缺席回退 M2.0 帧时长自走节拍)
class CXFFmpegVideoThread : public QThread {
public:
    CXFFmpegVideoThread(CxPacketQueue* vq, CxDecoder* dec, QObject* sink)
        : vQueue(vq), decoder(dec), sink(sink), stopFlag(false), paused(false),
          renderPts0(-1), rate(1.0), m_clock(0), m_ao(0)
    { }

    void requestStop() { stopFlag = true; if (m_clock) m_clock->interrupt(true); wake(); }
    void setPaused(bool on) { paused = on; wake(); }
    void setRenderPts0(long ms) { renderPts0 = ms; wake(); }
    void setRate(double r) { rate = r > 0.0 ? r : 1.0; }
    void setClock(CxClock* c) { m_clock = c; }
    void setAo(CXAudioOut* a) { m_ao = a; }

    void wake()
    {
        paceMx.lock();
        paceCond.wakeAll();
        paceMx.unlock();
    }

protected:
    void run()
    {
        while (!stopFlag) {
            waitIfPaused();
            if (stopFlag)
                break;

            CxPkt p = vQueue->take();
            if (p.pkt == 0 && !p.eos)
                continue; // 停止唤醒空载体

            if (p.eos) {
                drain();
                post(CxFFmpegEvFinished, 0, 0);
                break;
            }

            AVFrame* frame = decoder->decode(p.pkt);
            if (p.pkt != 0)
                av_packet_free(&p.pkt);
            if (frame != 0)
                emitFrame(frame); // decode 契约:仅本次有效
        }
    }

private:
    void drain()
    {
        while (!stopFlag) {
            AVFrame* frame = decoder->decode(0);
            if (frame == 0)
                break;
            emitFrame(frame);
        }
    }

    void waitIfPaused()
    {
        QMutexLocker lk(&paceMx);
        while (paused && !stopFlag)
            paceCond.wait(&paceMx, 120);
    }

    bool waitPace(long ms)
    {
        QMutexLocker lk(&paceMx);
        paceCond.wait(&paceMx, ms);
        return !stopFlag;
    }

    bool emitFrame(AVFrame* frame)
    {
        long pts = decoder->framePtsMs(frame);
        long dur = decoder->frameDurationMs(frame);
        if (pts < 0)
            pts = 0;
        if (renderPts0 > 0 && pts < renderPts0)
            return false; // seek 前旧帧弃

        // seek 首帧到位:告知 UI 锚点
        if (renderPts0 > 0 && pts >= renderPts0) {
            renderPts0 = -1;
            post(CxFFmpegEvSeekDone, pts, 0);
        }

        if (m_clock != 0 && m_ao != 0) {
            // M2.1 主时钟:帧应显示于 pts − ao 延迟(留 AO 缓冲);
            // 每轮重算,≤100ms 打盹(clock 提前唤醒即败絮新值)
            while (true) {
                if (stopFlag)
                    return false;
                long remain = (pts - m_ao->latencyMs()) - m_clock->value();
                if (remain <= 0)
                    break;
                long nap = (remain > 100) ? 100 : remain;
                if (!m_clock->waitAndCheck(nap))
                    return false;
            }
        } else {
            if (!waitPace((long)(dur / rate)))
                return false; // 音频缺席回退 M2.0
        }
        if (stopFlag)
            return false;

        QImage img = decoder->convert(frame);
        if (img.isNull())
            return false;

        // 帧 → QImage → 事件投 UI(渲在 UI 线程)
        CXVideoFrame* f = new CXVideoFrame();
        f->ptsMs = pts;
        f->width = img.width();
        f->height = img.height();
        f->isKey = false;
        f->img = img; // Qt 隐式共享,浅拷贝
        qApp->postEvent(sink, new CxFFmpegUiEvent(CxFFmpegEvFrame, pts, 0, f));
        return true;
    }

    void post(int ev, long ms, int code)
    {
        qApp->postEvent(sink, new CxFFmpegUiEvent(ev, ms, code));
    }

    CxPacketQueue* vQueue;
    CxDecoder* decoder;
    QObject* sink;
    volatile bool stopFlag;
    volatile bool paused;
    volatile long renderPts0;
    double rate;
    QMutex paceMx;
    QWaitCondition paceCond;
    CxClock* m_clock;
    CXAudioOut* m_ao;
};

#endif // CXFFVTHREAD_H
