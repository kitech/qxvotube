#include "cxthread.h"

#ifndef _WIN32
#include <unistd.h> // usleep 与 sched_yield(在 Qt3(unix) 与 Qt4/5/6(linux) 通用)
#endif

void CXThread::sleep(int ms)
{
    if (ms <= 0)
        return;
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

void CXThread::yield()
{
#ifdef _WIN32
    Sleep(0);
#else
    sched_yield();
#endif
}
