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
    CxPkt() : pkt(0), streamIdx(-1), eos(false) { }
};

class CxPacketQueue {
public:
    explicit CxPacketQueue(int capacity)
        : cap(capacity), stopped(false), flushed(false)
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

private:
    mutable QMutex mx;
    QWaitCondition notEmpty;
    QWaitCondition notFull;
    std::deque<CxPkt> items;
    int cap;
    volatile bool stopped;
    volatile bool flushed;
};

#endif // CXPQUEUE_H
