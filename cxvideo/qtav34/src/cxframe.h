#ifndef CXFRAME_H
#define CXFRAME_H

#include "cxvideo_global.h"
#include <qimage.h>
#include <qmutex.h>

// 帧定义(第 5 节;指针传递避免深拷贝)
class CXVideoFrame {
public:
    CXVideoFrame() : ptsMs(0), width(0), height(0), isKey(false) { }

    long ptsMs;          // 毫秒
    int width;
    int height;
    QImage img;          // 首版统一 RGB32(sws 已转好)
    bool isKey;
};

// Qt3 的 QImage(w,h,depth) 与 Qt4+ 的 QImage(w,h,Format) 共用一个构造函数
static inline QImage cxNewImage(int w, int h)
{
#ifdef QT3_BUILD
    // Qt3 的 32bit QImage 带 alpha 通道:解码/转换只写 RGB,alpha 字节为 0,
    // 直接 drawImage 会被当成全透明(表现为纯黑)。关闭 alpha buffer 让 Qt3
    // 忽略该字节(软渲/截图路径都需要;GL/X11/Xv/SDL 不受影响)。
    QImage img(w, h, 32);
    img.setAlphaBuffer(false);
    return img;
#else
    return QImage(w, h, QImage::Format_RGB32);
#endif
}

// 最新帧槽(视频线程写 / UI 线程取):取代"每帧 postEvent 携带 QImage"。
// 只保留最新一帧,内存恒定 O(1);notifyPending 保证同时至多一条 FrameReady
// 通知在途,UI 被卡顿/暂停拖慢时新帧覆盖旧帧,不在事件队列里无限堆积。
class CxLatestFrameSlot {
public:
    CxLatestFrameSlot() : m_frame(0), m_notifyPending(false) { }
    ~CxLatestFrameSlot() { clear(); }

    // 视频线程:存入新帧(覆盖即释放旧帧);返回 true 表示需投一条轻量通知
    bool put(CXVideoFrame* f)
    {
        QMutexLocker lk(&m_mx);
        delete m_frame;
        m_frame = f;
        if (!m_notifyPending) {
            m_notifyPending = true;
            return true;
        }
        return false;
    }

    // UI 线程:取走最新帧(所有权移交调用方)并清通知挂起位
    CXVideoFrame* take()
    {
        QMutexLocker lk(&m_mx);
        CXVideoFrame* f = m_frame;
        m_frame = 0;
        m_notifyPending = false;
        return f;
    }

    // 清空(换源/seek/stop):释放残留帧,避免显示旧画面
    void clear()
    {
        QMutexLocker lk(&m_mx);
        delete m_frame;
        m_frame = 0;
        m_notifyPending = false;
    }

private:
    QMutex m_mx;
    CXVideoFrame* m_frame;
    bool m_notifyPending;
};

#endif // CXFRAME_H
