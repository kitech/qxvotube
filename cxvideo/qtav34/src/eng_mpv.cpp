#include "compat34.h"
#include "eng_mpv.h"
#include "cxmpv_def.h"
#include "renderer_mpvgl.h" // M4-B GL 路
#include "cxthread.h"
#include "cxframe.h"
#include <qapplication.h>
#include <qmutex.h>
#include <qwaitcondition.h>
#include <qthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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
    CxMpvUiEvSeekDone = 7,  // ms
    CxMpvUiEvSubtitle = 8   // text(UTF-8 堆串,消费方接管释放)
};

class CXMpvUiEvent : public CustomEventBase {
public:
    CXMpvUiEvent(int ev, long v, int c, char* t = 0)
        : CustomEventBase(toEventType34(kCxMpvEventRaw)), uiev(ev), ms(v), code(c), txt(t) { }
    ~CXMpvUiEvent() { if (txt != 0) free(txt); } // strdup 来源,free 释放

    int uiev;
    long ms;
    int code;
    char* txt; // sub-text(M3-A;UTF-8 strdup;其它事件为空)
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
                if (strcmp(pr->name, "time-pos") == 0) {
                    if (getenv("CX_MPV_DEBUG") != 0)
                        fprintf(stderr, "pos-ev %ld\n", ms);
                    post(CxMpvUiEvPos, ms, 0);
                }
                else if (strcmp(pr->name, "duration") == 0)
                    post(CxMpvUiEvDur, ms, 0);
            } else if (pr->format == CxMpvFmtString &&
                       strcmp(pr->name, "sub-text") == 0) {
                // M3-A 字幕文本(可能为空串 = 清屏)
                char* t = strdup((const char*)pr->data);
                post(CxMpvUiEvSubtitle, 0, 0, t);
            }
            break;
        }
        default:
            break; // StartFile/Seek/Reconfig 等暂不消费
        }
    }

    void post(int uiev, long ms, int code, char* txt = 0)
    {
        qApp->postEvent(eng, new CXMpvUiEvent(uiev, ms, code, txt));
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

    // M4-B:gl 路(host + render 符号齐)优先;失败逐级回退 wid 路
    // CX_MPV_ROUTE=wid 可强制回退路(selftest --mpv-wid-probe 用)
    const char* forced = getenv("CX_MPV_ROUTE");
    bool gl = p_host != 0 &&
              api.mpv_render_context_create != 0 &&
              api.mpv_render_context_render != 0 &&
              api.mpv_render_context_free != 0 &&
              !(forced != 0 && strcmp(forced, "wid") == 0);
    if (gl) {
        if (cxOpenCore(url, true))
            return true;
        closeInternal(); // 半吊子清干净,重建走 wid
    }
    return cxOpenCore(url, false);
}

// M4-B:open 主体(route=true 用 vo=libmpv + render_context;
// false 走 wid 原生嵌入,与 M1/M2/M3 行为一致)
bool CXEngineMpv::cxOpenCore(const QString& url, bool glRoute)
{
    CxMpvApi& api = CxMpvApi::instance();

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
    if (getenv("CX_MPV_DEBUG") != 0) {
        api.mpv_set_option_string(mpv, "terminal", "yes");
        api.mpv_set_option_string(mpv, "msg-level", "all=v");
    }

    if (glRoute) {
        // vo=libmpv 不吃 wid/force-window;窗口由 CXRendererMpvGl 提
        api.mpv_set_option_string(mpv, "vo", "libmpv");
        api.mpv_set_option_string(mpv, "force-window", "no");
    } else if (p_host != 0) {
        // wid 原生嵌入(无 GL 依赖回退路)
        char buf[32];
        snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)p_host->winId());
        api.mpv_set_option_string(mpv, "wid", buf);
    } else {
        // 无宿主:mpv 自开独立窗口(等同 ProcessMpv 独立窗)
        api.mpv_set_option_string(mpv, "force-window", "yes");
    }

    // 遥测进度/时长(detail.md §7)+ M3-A 字幕文本
    api.mpv_observe_property(mpv, 1, "time-pos", (int)CxMpvFmtDouble);
    api.mpv_observe_property(mpv, 1, "duration", (int)CxMpvFmtDouble);
    api.mpv_observe_property(mpv, 1, "sub-text", (int)CxMpvFmtString);

    int r = api.mpv_initialize(mpv);
    if (r < 0) {
        char errtxt[64];
        snprintf(errtxt, sizeof(errtxt), "mpv_initialize ret=%d", r);
        api.mpv_terminate_destroy(mpv);
        if (!glRoute) {
            emit noticedError((int)CxErrEngine, QString(errtxt));
            return false;
        }
        return false; // gl 路:外层静默回退 wid
    }

    h_mpv = mpv;
    m_pump = new CXMpvPump(this, mpv);
    m_pump->start();

    // gl 路二阶段:host 挂视图 → GL context 现场 → render_context
    if (glRoute) {
        CXRendererMpvGl* v = (CXRendererMpvGl*)cxMpvGlViewCreate(p_host);
        cxMpvGlViewGeometry(v, p_host->width(), p_host->height());
        v->show();
        cxMpvGlStartNotify(v);
        v->makeCurrent();
        CxMpvOpenglInitParams ip;
        cxMpvGlViewInitParams(&ip);
        CxMpvRenderParam rp[3];
        rp[0].type = CxMpvRenderParamApiType;
        rp[0].data = (void*)"opengl";
        rp[1].type = CxMpvRenderParamOpenGLInitParams;
        rp[1].data = &ip;
        rp[2].type = CxMpvRenderParamInvalid;
        rp[2].data = 0;
        bool ok = api.mpv_render_context_create(
                      (CxMpvRenderCtx**)&h_rc, mpv, rp) == 0 && h_rc != 0;
        v->doneCurrent();
        if (!ok) {
            h_rc = 0;
            cxMpvGlStopNotify();
            cxMpvGlViewDelete(v);
            closeInternal();
            return false; // 外层回退 wid
        }
        cxMpvGlViewSet(v, h_rc);
        if (api.mpv_render_context_set_update_callback != 0)
            api.mpv_render_context_set_update_callback(
                (CxMpvRenderCtx*)h_rc, cxMpvGlUpdateHook, v);
        m_renderPath = "mpv-gl";
        m_glv = v;
    } else {
        m_renderPath = "mpv-wid";
    }

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
    // M4-B:render 对象须先于 mpv_terminate_destroy 收
    CxMpvApi& apiPrec = CxMpvApi::instance();
    if (h_rc != 0 && apiPrec.mpv_render_context_free != 0) {
        if (apiPrec.mpv_render_context_set_update_callback != 0)
            apiPrec.mpv_render_context_set_update_callback(
                (CxMpvRenderCtx*)h_rc, 0, 0); // 解注册渲染线程回调
        apiPrec.mpv_render_context_free((CxMpvRenderCtx*)h_rc);
    }
    h_rc = 0;
    m_renderPath = 0;
    if (m_glv != 0) {
        cxMpvGlStopNotify();
        cxMpvGlViewDelete(m_glv);
        m_glv = 0;
    }
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
    case CxMpvUiEvSubtitle: { // M3-A:文本到即发(空串=清屏)
        QString t;
        if (ev->txt != 0)
            t = qFromUtf8(ev->txt);
        emit subtitleChanged(t);
        break;
    }
    default:
        break;
    }
}

// ── M3-A:轨道(mpv track-list;i 为枚举序 → mpv 内 id)──

static int cxMpvTrackCountByType(CxMpvApi& api, void* h, const char* type)
{
    if (api.mpv_get_property == 0)
        return 0;
    long long n = 0;
    if (api.mpv_get_property(h, "track-list/count", (int)CxMpvFmtInt64, &n) < 0)
        return 0;
    int count = 0;
    for (long long i = 0; i < n; i++) {
        char* t = 0;
        char buf[64];
        snprintf(buf, sizeof(buf), "track-list/%lld/type", i);
        if (api.mpv_get_property(h, buf, (int)CxMpvFmtString, &t) < 0 || t == 0)
            continue;
        bool hit = (strcmp(t, type) == 0);
        if (api.mpv_free != 0)
            api.mpv_free(t);
        if (hit)
            count++;
    }
    return count;
}

// 第 idx(0 起)个 type 型轨的 mpv id;无则 -1
static int cxMpvTrackIdByOrdinal(CxMpvApi& api, void* h,
                                 const char* type, int idx)
{
    if (api.mpv_get_property == 0)
        return -1;
    long long n = 0;
    if (api.mpv_get_property(h, "track-list/count", (int)CxMpvFmtInt64, &n) < 0)
        return -1;
    int seen = 0;
    for (long long i = 0; i < n; i++) {
        char* t = 0;
        char buf[64];
        snprintf(buf, sizeof(buf), "track-list/%lld/type", i);
        if (api.mpv_get_property(h, buf, (int)CxMpvFmtString, &t) < 0 || t == 0)
            continue;
        bool hit = (strcmp(t, type) == 0);
        if (api.mpv_free != 0)
            api.mpv_free(t);
        if (!hit)
            continue;
        if (seen == idx) {
            long long id = -1;
            snprintf(buf, sizeof(buf), "track-list/%lld/id", i);
            if (api.mpv_get_property(h, buf, (int)CxMpvFmtInt64, &id) < 0)
                return -1;
            return (int)id;
        }
        seen++;
    }
    return -1;
}

int CXEngineMpv::audioTrackCount() const
{
    if (h_mpv == 0)
        return 0;
    return cxMpvTrackCountByType(CxMpvApi::instance(), h_mpv, "audio");
}

int CXEngineMpv::subtitleTrackCount() const
{
    if (h_mpv == 0)
        return 0;
    return cxMpvTrackCountByType(CxMpvApi::instance(), h_mpv, "sub");
}

bool CXEngineMpv::setAudioTrack(int idx)
{
    if (h_mpv == 0)
        return false;
    CxMpvApi& api = CxMpvApi::instance();
    if (idx < 0)
        return cxMpvRunCmd3(api, h_mpv, "set", "aid", "no");
    int id = cxMpvTrackIdByOrdinal(api, h_mpv, "audio", idx);
    if (id < 0)
        return false;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", id);
    return cxMpvRunCmd3(api, h_mpv, "set", "aid", buf);
}

bool CXEngineMpv::setSubtitleTrack(int idx)
{
    if (h_mpv == 0)
        return false;
    CxMpvApi& api = CxMpvApi::instance();
    if (idx < 0)
        return cxMpvRunCmd3(api, h_mpv, "set", "sid", "no");
    int id = cxMpvTrackIdByOrdinal(api, h_mpv, "sub", idx);
    if (id < 0)
        return false;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", id);
    return cxMpvRunCmd3(api, h_mpv, "set", "sid", buf);
}

bool CXEngineMpv::setSubtitleFile(const QString& path)
{
    if (h_mpv == 0 || path.isEmpty())
        return false;
    QByteArray pa = qToUtf8(path);
    const char* args[3] = { "sub-add", pa.data(), 0 }; // 默认 select
    return CxMpvApi::instance().mpv_command(h_mpv, args) == 0;
}

// ── M4-B:EQ 属性直写 + 截图 ──

static bool cxMpvSetPropInt(CxMpvApi& api, void* h, const char* prop, int v)
{
    if (api.mpv_set_property == 0)
        return false;
    long long val = v;
    int r = api.mpv_set_property(h, prop, (int)CxMpvFmtInt64, &val);
    if (r < 0 && getenv("CX_MPV_DEBUG") != 0)
        fprintf(stderr, "setEq: %s=%d ret=%d\n", prop, v, r);
    return r == 0;
}

// 直通映射(域一致 §7.7):四项与 mpv -100..100 / hue ±180 同域
bool CXEngineMpv::setEq(const CXVideoEq& eq)
{
    if (h_mpv == 0)
        return false;
    CxMpvApi& api = CxMpvApi::instance();
    if (api.mpv_set_property == 0)
        return false;
    bool ok = true;
    ok &= cxMpvSetPropInt(api, h_mpv, "brightness", eq.brightness);
    ok &= cxMpvSetPropInt(api, h_mpv, "contrast", eq.contrast);
    ok &= cxMpvSetPropInt(api, h_mpv, "saturation", eq.saturation);
    ok &= cxMpvSetPropInt(api, h_mpv, "hue", eq.hue);
    return ok;
}

// 仅 mpv-gl 路可(screenshot-to-file;wid 无控);探针规格与 M3-B 对齐
bool CXEngineMpv::screenshot(const QString& path)
{
    if (h_mpv == 0 || m_renderPath == 0 ||
        strcmp(m_renderPath, "mpv-gl") != 0)
        return false;
    QByteArray pa = qToUtf8(path);
    const char* args[3] = { "screenshot-to-file", pa.data(), 0 };
    return CxMpvApi::instance().mpv_command(h_mpv, args) == 0;
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
