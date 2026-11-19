#ifndef CXFFCLOCK_H
#define CXFFCLOCK_H

#include <qmutex.h>
#include <qwaitcondition.h>
#include "limelog.h"

// 主时钟(detail.md §5 CXClock;音频驱动:athread 写块后 updateValue 推进)
class CxClock {
public:
    CxClock() : m_value(-1), m_speed(1.0), m_stopped(false), m_clampLogged(false) { }

    long value()
    {
        QMutexLocker lk(&mx);
        return m_value;
    }

    void updateValue(long ms)
    {
        QMutexLocker lk(&mx);
        if (ms >= m_value)
            m_value = ms;
        cond.wakeAll(); // vthread 打盹更新重算
    }

    void setSpeed(double r)
    {
        QMutexLocker lk(&mx);
        m_speed = r > 0.0 ? r : 1.0;
    }

    // 至多睡 delay/speed ms;interrupt(true)/updateValue 提前唤醒
    bool waitAndCheck(long delay)
    {
        QMutexLocker lk(&mx);
        if (m_stopped)
            return false;
        long ms = (long)(delay / m_speed);
        // 倍速>1 且 delay 很小时整除会得 0 → 完全不睡;此时 emitFrame 主时钟
        // 循环会因主时钟冻结(暂停)而无限空转,占满一个核。钳到至少 1ms。
        if (delay > 0 && ms < 1) {
            ms = 1;
            if (!m_clampLogged) {
                m_clampLogged = true; // 进程级一次性,避免暂停期间刷屏
                ALOG_WARN("cx clock: nap clamped speed=", m_speed,
                          " delay=", delay);
            }
        }
        if (ms > 0) {
            cond.wait(&mx, ms);
        }
        return !m_stopped;
    }

    void interrupt(bool stop)
    {
        QMutexLocker lk(&mx);
        if (stop)
            m_stopped = true;
        cond.wakeAll();
    }

    // open/seek 新一轮:复位停止位与 value
    void reset()
    {
        QMutexLocker lk(&mx);
        m_stopped = false;
        m_value = -1;
    }

    bool isStopped() const
    {
        QMutexLocker lk((QMutex*)&mx);
        return m_stopped;
    }

private:
    mutable QMutex mx;
    QWaitCondition cond;
    long m_value;
    double m_speed;
    bool m_stopped;
    bool m_clampLogged; // waitAndCheck 钳位是否已记日志(一次性)
};

#endif // CXFFCLOCK_H
