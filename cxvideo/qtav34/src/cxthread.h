#ifndef CXTHREAD_H
#define CXTHREAD_H

#include "cxvideo_global.h"

// 线程壳:cxvideo 内部唯一接触线程平台件的点(detail.md 第 8 节)
class CXThread {
public:
    static void sleep(int ms);
    static void yield();
};

#endif // CXTHREAD_H
