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
#include <stdlib.h>      // getenv

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
          m_lastClockMs(-2), m_lastClockUs(0),
          m_dropTotal(0), m_dropWindow(0), m_lastAvLogUs(0),
          m_avWarnUs(0), m_avLateRun(0), m_avAheadRun(0),
          m_clockWaitLogged(false), m_clockStallLogUs(0),
          m_frameCount(0), m_absDiffSum(0), m_absDiffN(0)
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
        /* 线程退出时输出同步总结(仅 debug) */
        if (getenv("QLVOTUBE_DEBUG") != 0)
            qDebug("avsync summary: frames=%ld drops=%ld avg|diff|=%ldms",
                   m_frameCount, m_dropTotal,
                   m_absDiffN > 0 ? (long)(m_absDiffSum / m_absDiffN) : 0L);
    }

private:
    /* 丢帧阈值按帧间隔自适应:1.5×frameDur,钳到 40..100ms */
    enum { kAvDropLateMinMs = 40, kAvDropLateMaxMs = 100 };

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
        long avDiff = 0;   // pts - played:>0 超前,<=0 已到/落后
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
                const long played = m_clock->playedNow();
                const long long nowUs = av_gettime_relative();
                if (played < 0) {
                    /* 音频尚未起播(open/seek 后):纯等待,不判"停摆";
                     * 3s 仍无时钟才退回帧节拍,避免无声源永久卡住 */
                    if (m_lastClockUs == 0)
                        m_lastClockUs = nowUs;
                    else if (nowUs - m_lastClockUs > 3000000) {
                        clockStalled = true;
                        if (!m_clockWaitLogged) {
                            m_clockWaitLogged = true;
                            if (getenv("QLVOTUBE_DEBUG") != 0)
                                qDebug("avsync clock wait timeout: 3000ms");
                        }
                        break;
                    }
                    if (!m_clock->waitAndCheck(100))
                        return false;
                    continue;
                }
                if (m_lastClockUs == 0 || played != m_lastClockMs) {
                    m_lastClockMs = played;
                    m_lastClockUs = nowUs;
                } else if (nowUs - m_lastClockUs > 500000) {
                    clockStalled = true;   /* 主时钟 500ms 不前进:音频停摆 */
                    if (getenv("QLVOTUBE_DEBUG") != 0
                        && nowUs - m_clockStallLogUs > 5000000) {
                        m_clockStallLogUs = nowUs;
                        qDebug("avsync clock stalled: played=%ld frozen=%ldms",
                               played,
                               (long)((nowUs - m_lastClockUs) / 1000));
                    }
                    break;
                }
                const long diff = pts - played;
                avDiff = diff;
                m_absDiffSum += (diff < 0 ? -diff : diff);
                ++m_absDiffN;
                /* 丢帧阈值随帧间隔:25fps≈60ms,低帧率封顶 100ms;
                 * 晚到阈值才丢帧,继续解码追赶。日志 1s 一条。 */
                long dropLate = (long)(dur * 1.5);
                if (dropLate < kAvDropLateMinMs) dropLate = kAvDropLateMinMs;
                if (dropLate > kAvDropLateMaxMs) dropLate = kAvDropLateMaxMs;
                if (diff < -dropLate) {
                    ++m_dropTotal;
                    ++m_dropWindow;
                    if (getenv("QLVOTUBE_DEBUG") != 0
                        && nowUs - m_lastAvLogUs > 1000000) {
                        m_lastAvLogUs = nowUs;
                        qDebug("avsync drop: pts=%ld played=%ld late=%ldms"
                               " dropLate=%ld window=%ld total=%ld",
                               pts, played, -diff, dropLate,
                               m_dropWindow, m_dropTotal);
                        m_dropWindow = 0;
                    }
                    return false;
                }
                if (getenv("QLVOTUBE_DEBUG") != 0
                    && nowUs - m_lastAvLogUs > 1000000) {
                    m_lastAvLogUs = nowUs;
                    qDebug("avsync: pts=%ld written=%ld played=%ld"
                           " diff=%ld dropLate=%ld drop/s=%ld total=%ld",
                           pts, clk, played, diff, dropLate,
                           m_dropWindow, m_dropTotal);
                    m_dropWindow = 0;
                }
                /* 持续不同步告警:跳过启动前 ~2s;连续 2 次采样;2s 限流 */
                if (m_frameCount > 50) {
                    if (diff < -100) ++m_avLateRun; else m_avLateRun = 0;
                    if (diff > 100) ++m_avAheadRun; else m_avAheadRun = 0;
                    if ((m_avLateRun >= 2 || m_avAheadRun >= 2)
                        && getenv("QLVOTUBE_DEBUG") != 0
                        && nowUs - m_avWarnUs > 2000000) {
                        m_avWarnUs = nowUs;
                        qDebug("avsync warn %s: pts=%ld played=%ld"
                               " diff=%+ldms dropLate=%ld",
                               m_avAheadRun >= 2 ? "ahead" : "late",
                               pts, played, diff, dropLate);
                        m_avLateRun = 0;
                        m_avAheadRun = 0;
                    }
                }
                long remain = diff;
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
        /* 视频已落后但未达丢帧阈值:跳过 emit-gap 限速,让画面自然追平。
         * 落后过多(>250ms)时仍保留限速,防止“主时钟异常偏高”导致全速出帧
         * 把 QImage 事件队列打爆(该限速原本就是为此加的)。 */
        const bool avCatchup = (avDiff < -5 && avDiff > -250);
        // 第一层防护:自适应出帧下限。HLS 分段回退/主时钟卡高时 remain 恒<=0,
        // 原路径零等待全速出帧 → QImage 事件队列暴涨(2-3G)+UI 冻结无法暂停。
        // 用单调墙钟限制相邻出帧间隔,与时钟值解耦;dur 自适应片源帧率并钳
        // 到 10..100ms(ffplay 同款),倍速时按比例缩短,不牺牲正常/倍速播放。
        if (!m_draining && !clockStalled && !avCatchup
            && m_clock != 0 && m_ao != 0 && m_lastEmitUs > 0) {
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
        ++m_frameCount;

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
    long m_lastClockMs;             // 上次观察到的已播放位置
    long long m_lastClockUs;        // 该值最近变化的墙钟(0=未初始化)
    long m_dropTotal;               // 累计丢帧数
    long m_dropWindow;              // 本秒窗口丢帧数
    long long m_lastAvLogUs;        // 上一条 avsync 日志墙钟(1s 限流)
    long long m_avWarnUs;           // 持续不同步告警限流(2s)
    long m_avLateRun;               // 连续落后采样数
    long m_avAheadRun;              // 连续超前采样数
    bool m_clockWaitLogged;         // 启动 3s 无时钟已记录
    long long m_clockStallLogUs;    // 时钟停摆日志限流(5s)
    long m_frameCount;              // 实际送显帧数
    long long m_absDiffSum;         // |diff| 累计(结束总结)
    long m_absDiffN;                // |diff| 采样数
};

#endif // CXFFVTHREAD_H
