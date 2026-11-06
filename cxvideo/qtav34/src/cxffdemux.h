#ifndef CXFFDEMUX_H
#define CXFFDEMUX_H

#include "cxff_wrap.h"
#include "cxpqueue.h"
#include <qthread.h>

// 解包线程(detail.md §5:av_read_frame 循环;EOS 双投;停位唤醒)
class CxDemuxThread : public QThread {
public:
    CxDemuxThread(AVFormatContext* fmt, CxPacketQueue* vq, int vIdx,
                  CxPacketQueue* aq, int aIdx)
        : fmtCtx(fmt), vQueue(vq), aQueue(aq), vStream(vIdx), aStream(aIdx),
          stopFlag(false)
    { }

    void requestStop() { stopFlag = true; }

protected:
    void run()
    {
        AVPacket* pkt = av_packet_alloc();
        if (pkt == 0) {
            postEos();
            return;
        }
        while (!stopFlag) {
            int r = av_read_frame(fmtCtx, pkt);
            if (r < 0) {
                postEos(); // EOF/出错都按帧流走完处理
                break;
            }
            if (pkt->stream_index == vStream && vQueue != 0)
                pushPkt(vQueue, pkt, vStream);
            else if (pkt->stream_index == aStream && aQueue != 0)
                pushPkt(aQueue, pkt, aStream);
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
    }

    AVFormatContext* fmtCtx;
    CxPacketQueue* vQueue;
    CxPacketQueue* aQueue;
    int vStream;
    int aStream;
    volatile bool stopFlag;
};

#endif // CXFFDEMUX_H
