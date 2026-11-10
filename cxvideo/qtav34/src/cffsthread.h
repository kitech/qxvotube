#ifndef CXFFSTHREAD_H
#define CXFFSTHREAD_H

#include "compat34.h"
#include "cxff_wrap.h"
#include "cxpqueue.h"
#include "cffsub.h"
#include "cxffvthread.h" // CxFFmpegUiEvent / CxFFmpegEvFinished
#include <qthread.h>
#include <qapplication.h>

// 字幕线程(M3-A;detail.md §7.4):take → decode(追加时间轴)→ EOS 投
// Finished(引擎 m_eosPend 计入);独立线程免阻塞音视频主路;
// 时间轴按 pts 写,UI 线程 dispatch 侧按 position 查询。

// pts(st) → ms;负值返回 -1(调用判)
inline long cffPtsMsSub(long long pts, AVRational tb)
{
    if (pts <= 0 || (tb.num <= 0 || tb.den <= 0) ||
        tb.num == 0)
        return -1;
    AVRational msQ = { 1, 1000 };
    long long ms = av_rescale_q(pts, tb, msQ);
    if (ms < 0)
        ms = 0;
    return (long)ms;
}

class CXFFmpegSubThread : public QThread {
public:
    CXFFmpegSubThread(CxPacketQueue* q, CxFFSubDecoder* d,
                      CxFFSubTimeline* tl, QObject* sink)
        : sQueue(q), dec(d), tl(tl), sink(sink), stopFlag(false)
    { }

    void requestStop()
    {
        stopFlag = true;
        sQueue->wake();
    }

protected:
    void run()
    {
        while (!stopFlag) {
            CxPkt p = sQueue->take();
            if (p.pkt == 0 && !p.eos)
                continue;
            if (p.eos) {
                if (!stopFlag)
                    QApplication::postEvent(sink,
                        new CxFFmpegUiEvent(CxFFmpegEvFinished, 0, 0));
                break;
            }
            long ptsMs = cffPtsMsSub(p.pkt->pts, dec->timeBase());
            if (ptsMs >= 0)
                dec->decode(p.pkt, ptsMs, tl);
            av_packet_free(&p.pkt);
        }
    }

private:
    CxPacketQueue* sQueue;
    CxFFSubDecoder* dec;
    CxFFSubTimeline* tl;
    QObject* sink;
    volatile bool stopFlag;
};

#endif // CXFFSTHREAD_H
