#include "renderer_probe.h"
#include <qtimer.h>
#include "cxthread.h"

#define CXPROBE_POLL_MS 30

CXRendererProbe::CXRendererProbe(QObject* parent)
    : QObject(parent)
    , m_timer(0)
    , m_hasLast(false)
{
#ifdef QT3_BUILD
    m_timer = new QTimer(this, "cxprobe");
#else
    m_timer = new QTimer(this);
    m_timer->setObjectName("cxprobe");
#endif
    connect(m_timer, SIGNAL(timeout()), this, SLOT(pollTick()));
    m_timer->start(CXPROBE_POLL_MS);
}

CXRendererProbe::~CXRendererProbe()
{
}

void CXRendererProbe::setPolling(bool on)
{
    if (on)
        m_timer->start(CXPROBE_POLL_MS);
    else
        m_timer->stop();
}

void CXRendererProbe::showFrame(const CXVideoFrame* f)
{
    if (f == 0)
        return;
    m_last = *f;
    m_hasLast = true;
    emit frameReady(&m_last);
}

void CXRendererProbe::setViewport(const QRect& contentRect)
{
    Q_UNUSED(contentRect);
}

void CXRendererProbe::pollTick()
{
    // 占位:M1 接引擎后用于线程→UI 帧搬运的节流出口
    Q_UNUSED(m_hasLast);
}
