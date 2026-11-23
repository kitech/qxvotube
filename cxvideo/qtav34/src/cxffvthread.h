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
    // [可清理] 旧逐帧事件(携带 QImage):正式路径已切到 CxFFmpegEvFrameReady
    // + CxLatestFrameSlot。此枚举仅服务 emitFrame 未装配回退分支,可一并移除。
    CxFFmpegEvFrame = 1,      // 一帧到 UI(CXVideoFrame 所有权移交 customEvent)
    CxFFmpegEvFinished = 2,   // EOS
    CxFFmpegEvSeekDone = 3,   // ms = 到位锚点
    CxFFmpegEvError = 4,      // code
    CxFFmpegEvDuration = 5,   // ms
    CxFFmpegEvAudioPos = 6,   // 音频主时钟推进(athread 块级)
    CxFFmpegEvFrameReady = 7  // 有新帧可取(帧在 CxLatestFrameSlot,事件不携带)
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
          renderPts0(-1), rate(1.0), m_clock(0), m_ao(0),
          m_frameSlot(0), m_lastEmitUs(0), m_draining(false),
          m_lastClockMs(-2), m_lastClockUs(0)
    { }

    void requestStop() { stopFlag = true; if (m_clock) m_clock->interrupt(true); wake(); }
    void setPaused(bool on) { paused = on; wake(); }
    void setRenderPts0(long ms) { renderPts0 = ms; wake(); }
    void setRate(double r) { rate = r > 0.0 ? r : 1.0; }
    void setClock(CxClock* c) { m_clock = c; }
    void setAo(CXAudioOut* a) { m_ao = a; }
    void setFrameSlot(CxLatestFrameSlot* s) { m_frameSlot = s; }

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
            {
                if (p.flush) {                    // seek flush 标记
                    if (decoder != 0) decoder->flush();
                    renderPts0 = (p.flushMs > 0) ? p.flushMs : -1;
                } else {
                    break; // 空载体且非 flush:队列已 stopped,退出避免空转
                }
                continue; // flush 标记已处理
            }

            if (p.eos) {
                /* 先 drain 再报 EOS:m_draining 已关闭时钟/节拍等待,
                 * drain 只受解码速度约束;提前投递会让 UI 立即切集,
                 * 丢掉解码器尾部延迟帧(实测约 1~3 帧)。 */
                m_draining = true;
                drain();
                m_draining = false;
                if (!stopFlag)
                    post(CxFFmpegEvFinished, 0, 1);
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

        bool clockStalled = false;
        if (!m_draining && m_clock != 0 && m_ao != 0) {
            // M2.1 主时钟:帧应显示于 pts − ao 延迟(留 AO 缓冲);
            // 每轮重算,≤100ms 打盹(clock 提前唤醒即败絮新值)
            while (true) {
                if (stopFlag)
                    return false;
                if (paused) {
                    m_lastClockUs = 0;   // 恢复后重新观察时钟
                    return false; // 暂停时主时钟冻结,退回 run() 顶部由 waitIfPaused() 阻塞
                }
                const long clk = m_clock->value();
                const long long nowUs = av_gettime_relative();
                if (m_lastClockUs == 0 || clk != m_lastClockMs) {
                    m_lastClockMs = clk;
                    m_lastClockUs = nowUs;
                } else if (nowUs - m_lastClockUs > 500000) {
                    clockStalled = true;   /* 主时钟 500ms 不前进:音频停摆 */
                    break;
                }
                long remain = (pts - m_ao->latencyMs()) - clk;
                if (remain <= 0)
                    break;
                long nap = (remain > 100) ? 100 : remain;
                if (!m_clock->waitAndCheck(nap))
                    return false;
            }
            if (clockStalled) {
                /* 退回按帧时长节拍:让视频继续消费,避免把 demux 卡死 */
                if (!waitPace((long)(dur / rate)))
                    return false;
            }
        } else if (!m_draining) {
            if (!waitPace((long)(dur / rate)))
                return false; // 音频缺席回退 M2.0
        }
        // 第一层防护:自适应出帧下限。HLS 分段回退/主时钟卡高时 remain 恒<=0,
        // 原路径零等待全速出帧 → QImage 事件队列暴涨(2-3G)+UI 冻结无法暂停。
        // 用单调墙钟限制相邻出帧间隔,与时钟值解耦;dur 自适应片源帧率并钳
        // 到 10..100ms(ffplay 同款),倍速时按比例缩短,不牺牲正常/倍速播放。
        if (!m_draining && !clockStalled && m_clock != 0 && m_ao != 0 && m_lastEmitUs > 0) {
            int gap = (int)dur;
            if (gap < 10) gap = 10;
            if (gap > 100) gap = 100;
            // gap 单位 ms,av_gettime_relative() 单位 us,故乘 1000(非 1e6)
            long long gapUs = (long long)(gap * 1000.0 /
                                          (rate > 0.0 ? rate : 1.0));
            QMutexLocker lk(&paceMx);
            while (!stopFlag && !paused) {
                long long leftUs = gapUs - (av_gettime_relative() - m_lastEmitUs);
                if (leftUs <= 0)
                    break;
                long napMs = (long)((leftUs + 999) / 1000);
                if (napMs < 1) napMs = 1;
                paceCond.wait(&paceMx, napMs);
            }
        }
        if (stopFlag || paused)
            return false;
        m_lastEmitUs = av_gettime_relative();

        QImage img = decoder->convert(frame);
        if (img.isNull())
            return false;

        // 第二层防护:帧入最新帧槽,只投一条轻量通知事件(不携带 QImage)。
        // 槽里新帧覆盖旧帧,事件队列恒定 O(1);UI 卡顿/暂停时不再被帧淹没。
        // m_frameSlot 为空(未装配)时回退旧路径,保证兼容。
        CXVideoFrame* f = new CXVideoFrame();
        f->ptsMs = pts;
        f->width = img.width();
        f->height = img.height();
        f->isKey = false;
        f->img = img; // Qt 隐式共享,浅拷贝
        if (m_frameSlot != 0) {
            if (m_frameSlot->put(f))
                post(CxFFmpegEvFrameReady, pts, 0);
        } else {
            // [可清理] m_frameSlot 未装配时的兼容回退;引擎恒会装配,
            // 稳定后可删此分支与 CxFFmpegEvFrame 枚举/事件处理。
            qApp->postEvent(sink, new CxFFmpegUiEvent(CxFFmpegEvFrame, pts, 0, f));
        }
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
    CxLatestFrameSlot* m_frameSlot; // 引擎持有,生命周期长于本线程
    long long m_lastEmitUs;         // 上次出帧单调墙钟(us),出帧限速兜底
    volatile bool m_draining;       // EOS 尾帧排空:跳过 pacing,避免等冻结音频时钟
    long m_lastClockMs;             // 上次观察到的音频主时钟值
    long long m_lastClockUs;        // 该值最近变化的墙钟(0=未初始化)
};

#endif // CXFFVTHREAD_H
