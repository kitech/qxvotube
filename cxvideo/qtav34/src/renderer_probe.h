#ifndef RENDERER_PROBE_H
#define RENDERER_PROBE_H

#include "cxrenderer.h"
#include <qobject.h>

class QTimer;

// 无窗口帧回调:qskvotube(Quick/RHI)与自测入口(detail.md 第 6/11 节,M5)
class CXRendererProbe : public QObject, public CXVideoRenderer {
    Q_OBJECT
public:
    explicit CXRendererProbe(QObject* parent = 0);
    ~CXRendererProbe();

    void showFrame(const CXVideoFrame* f);
    void setViewport(const QRect& contentRect);

    // 30ms 节流是否开启(默认开;单帧测试可关)
    void setPolling(bool on);

signals:
    // 观察方只读,勿持有越过下一帧
    void frameReady(const CXVideoFrame* frame);

private slots:
    void pollTick();

private:
    QTimer* m_timer;
    CXVideoFrame m_last;
    bool m_hasLast;
};

#endif // RENDERER_PROBE_H
