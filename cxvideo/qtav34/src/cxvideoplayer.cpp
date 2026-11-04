#include "cxvideoplayer.h"
#include "cxengine.h"

CXVideoPlayer::CXVideoPlayer(QObject* parent)
    : QObject(parent)
    , m_engine(0)
    , m_state(CxStopped)
    , m_rate(1.0)
    , m_volume(100)
    , m_muted(false)
{
}

CXVideoPlayer::~CXVideoPlayer()
{
    unwireEngine();
    close();
}

bool CXVideoPlayer::setEngine(CxEngineType type)
{
    unwireEngine();
    m_engine = 0;
    if (type == CxEngineAuto || type == CxEngineMpv || type == CxEngineFfmpeg) {
        m_engine = cxEngineCreateDefault();
    }
    wireEngine();
    return m_engine != 0;
}

void CXVideoPlayer::setRenderer(CXVideoRenderer* r)
{
    if (m_engine != 0)
        m_engine->setRenderer(r);
}

bool CXVideoPlayer::open(const QString& url)
{
    setState(CxStopped);
    m_rate = 1.0;
    m_volume = 100;
    m_muted = false;
    if (m_engine == 0) {
        emit error(CxErrEngine, "no engine available");
        return false;
    }
    bool ok = m_engine->open(url);
    if (!ok) {
        emit error(CxErrOpenFail, url);
        return false;
    }
    return true;
}

void CXVideoPlayer::close()
{
    if (m_engine != 0)
        m_engine->close();
    setState(CxStopped);
}

void CXVideoPlayer::play()
{
    if (m_engine != 0)
        m_engine->play();
}

void CXVideoPlayer::pause()
{
    if (m_engine != 0)
        m_engine->pause();
}

void CXVideoPlayer::stop()
{
    if (m_engine != 0)
        m_engine->stop();
}

void CXVideoPlayer::seek(long ms)
{
    if (m_engine != 0)
        m_engine->seek(ms);
}

void CXVideoPlayer::setRate(double r)
{
    m_rate = r;
    if (m_engine != 0)
        m_engine->setRate(r);
}

void CXVideoPlayer::setVolume(int pct)
{
    if (pct < 0)
        pct = 0;
    if (pct > 100)
        pct = 100;
    m_volume = pct;
    if (m_engine != 0)
        m_engine->setVolume(pct);
}

void CXVideoPlayer::setMute(bool on)
{
    m_muted = on;
    if (m_engine != 0)
        m_engine->setMute(on);
}

long CXVideoPlayer::position() const
{
    if (m_engine != 0)
        return m_engine->position();
    return 0;
}

long CXVideoPlayer::duration() const
{
    if (m_engine != 0)
        return m_engine->duration();
    return 0;
}

void CXVideoPlayer::setState(CXVideoState s)
{
    m_state = s;
    emit mediaStatusChanged(int(s));
}

// 引擎信号 → 公共信号(Qt3 字符串签名转发)
void CXVideoPlayer::wireEngine()
{
    if (m_engine == 0)
        return;
    connect(m_engine, SIGNAL(started()), this, SIGNAL(started()));
    connect(m_engine, SIGNAL(paused()), this, SIGNAL(paused()));
    connect(m_engine, SIGNAL(stopped()), this, SIGNAL(stopped()));
    connect(m_engine, SIGNAL(finished()), this, SIGNAL(finished()));
    connect(m_engine, SIGNAL(positionChanged(long)), this, SIGNAL(positionChanged(long)));
    connect(m_engine, SIGNAL(durationChanged(long)), this, SIGNAL(durationChanged(long)));
    connect(m_engine, SIGNAL(seekFinished(long)), this, SIGNAL(seekFinished(long)));
    connect(m_engine, SIGNAL(started()), this, SLOT(engineStarted()));
    connect(m_engine, SIGNAL(paused()), this, SLOT(enginePaused()));
    connect(m_engine, SIGNAL(stopped()), this, SLOT(engineStopped()));
    connect(m_engine, SIGNAL(finished()), this, SLOT(engineFinished()));
    connect(m_engine, SIGNAL(positionChanged(long)), this, SLOT(enginePositionChanged(long)));
    connect(m_engine, SIGNAL(durationChanged(long)), this, SLOT(engineDurationChanged(long)));
    connect(m_engine, SIGNAL(seekFinished(long)), this, SLOT(engineSeekFinished(long)));
    connect(m_engine, SIGNAL(noticedError(int,QString)), this, SLOT(engineError(int,QString)));
}

void CXVideoPlayer::unwireEngine()
{
    // 该引擎对象由工厂交给 player 完全持有
}

void CXVideoPlayer::engineStarted() { setState(CxPlaying); }
void CXVideoPlayer::enginePaused() { setState(CxPaused); }
void CXVideoPlayer::engineStopped() { setState(CxStopped); }
void CXVideoPlayer::engineFinished() { setState(CxStopped); }
void CXVideoPlayer::enginePositionChanged(long ms) { Q_UNUSED(ms); }
void CXVideoPlayer::engineDurationChanged(long ms) { Q_UNUSED(ms); }
void CXVideoPlayer::engineSeekFinished(long ms) { Q_UNUSED(ms); }
void CXVideoPlayer::engineError(int code, const QString& msg) { emit error(code, msg); }
