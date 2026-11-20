#ifndef CXPQUEUE_H
#define CXPQUEUE_H

#include "cxff_wrap.h"
#include <qmutex.h>
#include <qwaitcondition.h>
#include <deque>

// AVPacket 线程队列(detail.md §5:videoQ=256 / audioQ=512,push 满则条件等)
struct CxPkt {
    AVPacket* pkt;   // 拥有者交接:消费方 av_packet_free
    int streamIdx;
    bool eos;
    /* seek 后 flush 标记:不携带 pkt;消费线程据此 flush 解码器/时钟 */
    bool flush;
    long flushMs;    // seek 目标(渲染/时钟基准)
    CxPkt() : pkt(0), streamIdx(-1), eos(false), flush(false), flushMs(0) { }
};

class CxPacketQueue {
public:
    explicit CxPacketQueue(int capacity)
        : cap(capacity), stopped(false), flushed(false), bytes(0)
    { }

    void setCapacity(int c)
    {
        QMutexLocker lk(&mx);
        cap = c;
        notFull.wakeAll();
    }

    // 阻塞直至有空位;stop 后静默丢弃
    void push(const CxPkt& p)
    {
        QMutexLocker lk(&mx);
        while (!stopped && (int)items.size() >= cap)
            notFull.wait(&mx);
        if (stopped)
            return;
        if (p.pkt != 0)
            bytes += p.pkt->size;
        items.push_back(p);
        notEmpty.wakeAll();
    }

    // 阻塞取;队列停/空(停止位)返回 invalid CxPkt(调用方判 eos/校验 pkt)
    CxPkt take()
    {
        QMutexLocker lk(&mx);
        while (items.empty() && !stopped)
            notEmpty.wait(&mx);
        if (items.empty()) {
            CxPkt p;
            return p; // 停止唤醒,空载体
        }
        CxPkt p = items.front();
        items.pop_front();
        if (p.pkt != 0)
            bytes -= p.pkt->size;
        if (bytes < 0)
            bytes = 0;
        notFull.wakeAll();
        return p;
    }

    // 线程唤醒;已持有的 pkt 由 clear() 统一释放
    void wake()
    {
        QMutexLocker lk(&mx);
        stopped = true;
        flushed = true;
        notEmpty.wakeAll();
        notFull.wakeAll();
    }

    void clear()
    {
        QMutexLocker lk(&mx);
        for (size_t i = 0; i < items.size(); i++) {
            if (items[i].pkt != 0)
                av_packet_free(&items[i].pkt);
        }
        items.clear();
        bytes = 0;
        notFull.wakeAll();
    }

    // 新轮次(open/seek 后):复位停止位,释放旧积压
    void reset()
    {
        clear();
        QMutexLocker lk(&mx);
        stopped = false;
        flushed = false;
        notFull.wakeAll();
    }

    bool empty() const
    {
        QMutexLocker lk((QMutex*)&mx);
        return items.empty();
    }

    // 当前队列中包字节数(UI 线程读)
    long bufferedBytes() const
    {
        QMutexLocker lk(&mx);
        return bytes;
    }

private:
    mutable QMutex mx;
    QWaitCondition notEmpty;
    QWaitCondition notFull;
    std::deque<CxPkt> items;
    int cap;
    long bytes;                 // 队列中 AVPacket 字节合计(入队加/出队减)
    volatile bool stopped;
    volatile bool flushed;
};

#endif // CXPQUEUE_H
