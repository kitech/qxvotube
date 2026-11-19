#ifndef CXFFDEMUX_H
#define CXFFDEMUX_H

#include "cxff_wrap.h"
#include "cxpqueue.h"
#include "cxffvthread.h"   // CxFFmpegUiEvent / CxFFmpegEvError
#include <qthread.h>
#include <qmutex.h>
#include <qwaitcondition.h>
#include <qapplication.h>

// 解包线程(detail.md §5:av_read_frame 循环;EOS 双投;停位唤醒)
// M3-A:第三队列(字幕流,文本型)——sq/sIdx 传 0/-1 即旧行为
class CxDemuxThread : public QThread {
public:
    CxDemuxThread(AVFormatContext* fmt, CxPacketQueue* vq, int vIdx,
                  CxPacketQueue* aq, int aIdx, CxPacketQueue* sq = 0,
                  int sIdx = -1, QObject* sink = 0)
        : fmtCtx(fmt), vQueue(vq), aQueue(aq), sQueue(sq),
          vStream(vIdx), aStream(aIdx), sStream(sIdx),
          sink(sink), stopFlag(false), m_bufferedMs(0),
          seekMs(0), seekStream(-1), seekReq(false)
    { }

    void requestStop() { stopFlag = true; }

    /* 缓冲前沿(ms):demux 已从网络读到的最后包时间戳;UI 线程读 */
    long bufferedMs() const
    {
        QMutexLocker lk(&m_bufMx);
        return m_bufferedMs;
    }

    /* seek 由本线程执行:AVFormatContext 非线程安全,绝不能与
     * av_read_frame 并发(2026-11-18 修复:并发导致读错误→EOS→冻结) */
    void requestSeek(long ms, int streamIdx)
    {
        QMutexLocker lk(&seekMx);
        seekMs = ms;
        seekStream = streamIdx;
        seekReq = true;
    }

protected:
    void run()
    {
        AVPacket* pkt = av_packet_alloc();
        if (pkt == 0) {
            postEos();
            return;
        }
        while (!stopFlag) {
            if (takeSeek())          // 先处理 seek(单线程独占 fmtCtx)
                continue;            // 重新取包(marker 已入队)
            int r = av_read_frame(fmtCtx, pkt);
            if (r < 0) {
                // EOF = 正常收尾;其它错误上报(不再静默当 EOF 吞掉)
                if (r != AVERROR_EOF && sink != 0)
                    qApp->postEvent(sink, new CxFFmpegUiEvent(
                        CxFFmpegEvError, 0, (int)CxErrEngine));
                postEos();
                break;
            }
            notePkt(pkt);   // 记录缓冲前沿,先于 pushPkt(所有权移交)
            if (pkt->stream_index == vStream && vQueue != 0)
                pushPkt(vQueue, pkt, vStream);
            else if (pkt->stream_index == aStream && aQueue != 0)
                pushPkt(aQueue, pkt, aStream);
            else if (pkt->stream_index == sStream && sQueue != 0)
                pushPkt(sQueue, pkt, sStream);
            else
                av_packet_free(&pkt); // 非目标流,直接弃
            if (pkt == 0) {
                pkt = av_packet_alloc(); // push 后必须重取
                if (pkt == 0) {
                    postEos();
                    return;
                }
            }
        }
        if (pkt != 0)
            av_packet_free(&pkt);
    }

private:
    bool takeSeek()
    {
        long ms = 0;
        int idx = -1;
        {
            QMutexLocker lk(&seekMx);
            if (!seekReq) return false;
            ms = seekMs; idx = seekStream; seekReq = false;
        }
        const int sIdx = (idx >= 0) ? idx
                        : (vStream >= 0 ? vStream : aStream);
        if (fmtCtx == 0 || sIdx < 0) return true;
        AVRational tb = fmtCtx->streams[sIdx]->time_base;
        /* 注意:ms 是毫秒,AV_TIME_BASE_Q 是微秒基 → 必须先 ×1000;
         * 旧实现漏乘,seek 实际落到 0.18s 处再逐帧快进(表现为"停播") */
        const long long target = av_rescale_q(ms * 1000LL, AV_TIME_BASE_Q, tb);
        const int rc = av_seek_frame(fmtCtx, sIdx, target, AVSEEK_FLAG_BACKWARD);
        if (rc < 0) {
            if (sink != 0)
                qApp->postEvent(sink, new CxFFmpegUiEvent(
                    CxFFmpegEvError, ms, (int)CxErrSeek));
            return true;   // 保留原位置继续(不退出线程)
        }
        flushAndMark(ms);  // 清积压 + 投 flush 标记
        return true;
    }

    /* 清队列积压并由消费线程自 flush(解码器归消费线程所有,避免竞态) */
    void flushAndMark(long ms)
    {
        {   // seek 后旧缓冲前沿失效,归零重新累计
            QMutexLocker lk(&m_bufMx);
            m_bufferedMs = 0;
        }
        flushOne(vQueue, ms);
        flushOne(aQueue, ms);
        flushOne(sQueue, ms);
    }

    /* 把已读包的 PTS 折算成 ms 取最大值当缓冲前沿;无时间戳跳过 */
    void notePkt(const AVPacket* pkt)
    {
        if (pkt == 0 || fmtCtx == 0)
            return;
        const int si = pkt->stream_index;
        if (si < 0 || si >= fmtCtx->nb_streams)
            return;
        const long long ts = (pkt->pts != AV_NOPTS_VALUE)
                             ? pkt->pts : pkt->dts;
        if (ts == AV_NOPTS_VALUE)
            return;
        const long ms = (long)(av_rescale_q(
            ts, fmtCtx->streams[si]->time_base, AV_TIME_BASE_Q) / 1000);
        if (ms <= 0)
            return;
        QMutexLocker lk(&m_bufMx);
        if (ms > m_bufferedMs)
            m_bufferedMs = ms;
    }

    void flushOne(CxPacketQueue* q, long ms)
    {
        if (q == 0) return;
        q->clear();
        q->reset();
        CxPkt m;
        m.flush = true;
        m.flushMs = ms;
        q->push(m);
    }

    void pushPkt(CxPacketQueue* q, AVPacket*& pkt, int streamIdx)
    {
        CxPkt p;
        p.pkt = pkt;
        p.streamIdx = streamIdx;
        p.eos = false;
        q->push(p);
        pkt = 0; // 所有权已交队列
    }

    void postEos()
    {
        CxPkt eos;
        eos.eos = true;
        if (vQueue != 0)
            vQueue->push(eos);
        if (aQueue != 0)
            aQueue->push(eos);
        if (sQueue != 0)
            sQueue->push(eos);
    }

    AVFormatContext* fmtCtx;
    CxPacketQueue* vQueue;
    CxPacketQueue* aQueue;
    CxPacketQueue* sQueue;
    int vStream;
    int aStream;
    int sStream;
    QObject* sink;
    volatile bool stopFlag;
    mutable QMutex m_bufMx;      // 保护 m_bufferedMs(demux 写/UI 读)
    long m_bufferedMs;
    QMutex seekMx;
    long seekMs;
    int seekStream;
    bool seekReq;
};

#endif // CXFFDEMUX_H
