#include "compat34.h"
#include "eng_mpv.h"
#include "cxmpv_def.h"
#include "cxthread.h"
#include "cxframe.h"
#include <qapplication.h>
#include <qmutex.h>
#include <qwaitcondition.h>
#include <qthread.h>
#include <stdio.h>
#include <string.h>

// ── 事件泵 ──

// 自定事件类型(Qt3: 自定段 >=1000;避开 qlappcom User+100~113)
const int kCxMpvEventRaw = 4002;

enum CxMpvUiEv {
    CxMpvUiEvNone = 0,
    CxMpvUiEvPos = 1,       // ms
    CxMpvUiEvDur = 2,       // ms
    CxMpvUiEvStarted = 3,
    CxMpvUiEvStopped = 4,
    CxMpvUiEvFinished = 5,
    CxMpvUiEvError = 6,     // code
    CxMpvUiEvSeekDone = 7   // ms
};

class CXMpvUiEvent : public CustomEventBase {
public:
    CXMpvUiEvent(int ev, long v, int c)
        : CustomEventBase(toEventType34(kCxMpvEventRaw)), uiev(ev), ms(v), code(c) { }

    int uiev;
    long ms;
    int code;
};

class CXMpvPump : public QThread {
public:
    CXMpvPump(CXEngineMpv* eng, void* handle)
        : eng(eng), h(handle), stopFlag(false) { }

    void requestStop()
    {
        stopFlag = true;
    }

protected:
    void run()
    {
        CxMpvApi& api = CxMpvApi::instance();
        while (!stopFlag) {
            void* evh = api.mpv_wait_event(h, 0.2);
            if (evh == 0)
                break;
            CxMpvEvent* ev = (CxMpvEvent*)evh;
            if (ev->event_id == CxMpvEventNone)
                continue; // 0.2s 超时,继续轮询
            dispatch(ev->event_id, ev->data);
        }
    }

private:
    void dispatch(int id, void* data)
    {
        switch (id) {
        case CxMpvEventFileLoaded:
            post(CxMpvUiEvStarted, 0, 0);
            break;
        case CxMpvEventPlaybackRestart:
            post(CxMpvUiEvSeekDone, eng->m_seekTarget, 0);
            break;
        case CxMpvEventEndFile: {
            CxMpvEventEndFileData* ef = (CxMpvEventEndFileData*)data;
            if (ef != 0 && ef->reason == CxMpvEndFileError)
                post(CxMpvUiEvError, 0, (int)CxErrEngine);
            else
                post(CxMpvUiEvFinished, 0, 0);
            break;
        }
        case CxMpvEventIdle:
        case CxMpvEventShutdown:
            post(CxMpvUiEvStopped, 0, 0);
            break;
        case CxMpvEventPropertyChange: {
            CxMpvEventProperty* pr = (CxMpvEventProperty*)data;
            if (pr == 0 || pr->name == 0 || pr->data == 0)
                break;
            if (pr->format == CxMpvFmtDouble) {
                double secs = *(double*)pr->data;
                long ms = (long)(secs * 1000.0 + 0.5);
                if (strcmp(pr->name, "time-pos") == 0)
                    post(CxMpvUiEvPos, ms, 0);
                else if (strcmp(pr->name, "duration") == 0)
                    post(CxMpvUiEvDur, ms, 0);
            }
            break;
        }
        default:
            break; // StartFile/Seek/Reconfig 等暂不消费
        }
    }

    void post(int uiev, long ms, int code)
    {
        qApp->postEvent(eng, new CXMpvUiEvent(uiev, ms, code));
    }

    CXEngineMpv* eng;
    void* h;
    volatile bool stopFlag;
};

// ── 引擎 ──

CXEngineMpv::~CXEngineMpv()
{
    closeInternal();
}

bool CXEngineMpv::isAvailable() const
{
    return CxMpvApi::instance().ensure();
}

static bool cxMpvRunCmd1(CxMpvApi& api, void* h, const char* a)
{
    const char* args[2] = { a, 0 };
    return api.mpv_command(h, args) == 0;
}

static bool cxMpvRunCmd2(CxMpvApi& api, void* h, const char* a, const char* b)
{
    const char* args[3] = { a, b, 0 };
    return api.mpv_command(h, args) == 0;
}

static bool cxMpvRunCmd3(CxMpvApi& api, void* h,
                         const char* a, const char* b, const char* c)
{
    const char* args[4] = { a, b, c, 0 };
    return api.mpv_command(h, args) == 0;
}

bool CXEngineMpv::open(const QString& url)
{
    CxMpvApi& api = CxMpvApi::instance();
    if (!api.ensure()) {
        emit noticedError((int)CxErrEngine, QString("libmpv load failed"));
        return false;
    }
    if (h_mpv != 0)
        closeInternal(); // 换源即重建

    m_url = url;
    m_loaded = false;
    m_seekTarget = -1;

    void* mpv = api.mpv_create();
    if (mpv == 0) {
        emit noticedError((int)CxErrEngine, QString("mpv_create failed"));
        return false;
    }

    api.mpv_set_option_string(mpv, "osc", "no");
    api.mpv_set_option_string(mpv, "input-default-bindings", "no");
    api.mpv_set_option_string(mpv, "input-vo-keyboard", "no");
    api.mpv_set_option_string(mpv, "cache", "yes");
    api.mpv_set_option_string(mpv, "keep-open", "no");

    if (p_host != 0) {
        // wid 原生嵌入(Qt3 唯一可行渲染路;无 GL 依赖)
        char buf[32];
        snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)p_host->winId());
        api.mpv_set_option_string(mpv, "wid", buf);
    } else {
        // 无宿主:mpv 自开独立窗口(等同 ProcessMpv 独立窗)
        api.mpv_set_option_string(mpv, "force-window", "yes");
    }

    // 遥测进度/时长(detail.md §7)
    api.mpv_observe_property(mpv, 1, "time-pos", (int)CxMpvFmtDouble);
    api.mpv_observe_property(mpv, 1, "duration", (int)CxMpvFmtDouble);

    int r = api.mpv_initialize(mpv);
    if (r < 0) {
        char errtxt[64];
        snprintf(errtxt, sizeof(errtxt), "mpv_initialize ret=%d", r);
        api.mpv_terminate_destroy(mpv);
        emit noticedError((int)CxErrEngine, QString(errtxt));
        return false;
    }

    h_mpv = mpv;
    m_pump = new CXMpvPump(this, mpv);
    m_pump->start();

    QByteArray ba = qToUtf8(m_url);
    if (!cxMpvRunCmd2(api, mpv, "loadfile", ba.data())) {
        emit noticedError((int)CxErrOpenFail, m_url);
        closeInternal();
        return false;
    }
    return true;
}

void CXEngineMpv::closeInternal()
{
    if (m_pump != 0) {
        m_pump->requestStop();
        CxMpvApi& api = CxMpvApi::instance();
        if (h_mpv != 0 && api.mpv_wakeup != 0)
            api.mpv_wakeup(h_mpv); // 加速 wait_event 返回
        m_pump->wait(2000);
        delete m_pump;
        m_pump = 0;
    }
    if (h_mpv != 0) {
        CxMpvApi& api = CxMpvApi::instance();
        api.mpv_terminate_destroy(h_mpv);
        h_mpv = 0;
    }
    m_loaded = false;
    m_seekTarget = -1;
}

void CXEngineMpv::close()
{
    closeInternal();
    m_url = QString();
}

bool CXEngineMpv::play()
{
    if (h_mpv == 0)
        return false;
    CxMpvApi& api = CxMpvApi::instance();
    // mpv 里"播放"=解除暂停(loadfile 已由 open 发出)
    return cxMpvRunCmd3(api, h_mpv, "set", "pause", "no");
}

bool CXEngineMpv::pause()
{
    if (h_mpv == 0)
        return false;
    return cxMpvRunCmd3(CxMpvApi::instance(), h_mpv, "set", "pause", "yes");
}

bool CXEngineMpv::stop()
{
    if (h_mpv == 0)
        return false;
    return cxMpvRunCmd1(CxMpvApi::instance(), h_mpv, "stop");
}

bool CXEngineMpv::seek(long ms)
{
    if (h_mpv == 0)
        return false;
    if (ms < 0)
        ms = 0;
    m_seekTarget = ms;
    char buf[24];
    snprintf(buf, sizeof(buf), "%ld", ms / 1000); // mpv seek 单位=秒
    return cxMpvRunCmd3(CxMpvApi::instance(), h_mpv, "seek", buf, "absolute");
}

bool CXEngineMpv::setRate(double r)
{
    if (h_mpv == 0)
        return false;
    char buf[24];
    snprintf(buf, sizeof(buf), "%.3f", r > 0.0 ? r : 1.0);
    return cxMpvRunCmd3(CxMpvApi::instance(), h_mpv, "set", "speed", buf);
}

bool CXEngineMpv::setVolume(int pct)
{
    if (h_mpv == 0)
        return false;
    if (pct < 0)
        pct = 0;
    if (pct > 100)
        pct = 100;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", pct);
    return cxMpvRunCmd3(CxMpvApi::instance(), h_mpv, "set", "volume", buf);
}

bool CXEngineMpv::setMute(bool on)
{
    if (h_mpv == 0)
        return false;
    return cxMpvRunCmd3(CxMpvApi::instance(), h_mpv, "set", "mute",
                        on ? "yes" : "no");
}

long CXEngineMpv::position() const
{
    // 进度走遥测(positionChanged 信号);同步接口仅返回缓存,首版不跨线程 get_property
    return 0;
}

long CXEngineMpv::duration() const
{
    return 0;
}

void CXEngineMpv::customEvent(CustomEventBase* e)
{
    if (e->type() != toEventType34(kCxMpvEventRaw))
        return;
    CXMpvUiEvent* ev = (CXMpvUiEvent*)e;
    dispatchEvent(ev);
}

void CXEngineMpv::dispatchEvent(const CXMpvUiEvent* ev)
{
    switch (ev->uiev) {
    case CxMpvUiEvPos:
        emit positionChanged(ev->ms);
        break;
    case CxMpvUiEvDur:
        emit durationChanged(ev->ms);
        break;
    case CxMpvUiEvStarted:
        m_loaded = true;
        emit started();
        break;
    case CxMpvUiEvStopped:
        m_loaded = false;
        emit stopped();
        break;
    case CxMpvUiEvFinished:
        m_loaded = false;
        emit finished();
        break;
    case CxMpvUiEvError:
        m_loaded = false;
        emit noticedError(ev->code, QString("mpv end file error"));
        break;
    case CxMpvUiEvSeekDone:
        if (ev->ms >= 0)
            emit seekFinished(ev->ms);
        break;
    default:
        break;
    }
}

// ── selftest 专用无头探测 ──

int cxEngineMpvProbeLib()
{
    CxMpvApi& api = CxMpvApi::instance();
    if (!api.ensure()) {
        fprintf(stderr, "probelib: ensure failed\n");
        return 0;
    }
    void* mpv = api.mpv_create();
    if (mpv == 0) {
        fprintf(stderr, "probelib: create failed\n");
        return 1;
    }
    api.mpv_set_option_string(mpv, "ao", "null");
    api.mpv_set_option_string(mpv, "vo", "null");
    api.mpv_set_option_string(mpv, "force-window", "no");
    int r = api.mpv_initialize(mpv);
    if (r < 0) {
        fprintf(stderr, "probelib: initialize ret=%d\n", r);
        api.mpv_terminate_destroy(mpv);
        return 1;
    }
    api.mpv_terminate_destroy(mpv);
    return 2;
}

bool cxEngineMpvSmokeHeadless()
{
    return cxEngineMpvProbeLib() == 2;
}
