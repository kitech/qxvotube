#ifndef CXENGMPV_H
#define CXENGMPV_H

#include "cxengine.h"
#include "cxvideo_global.h" // CXVideoEq(M4-B setEq)

// libmpv 内嵌内核(detail.md 第 7 节)
// - 不硬链:dlopen 运行时探测(M1),缺失自动降级
// - Qt3/任意宿主: wid 原生子窗嵌入(无 GL 依赖)
// - 音频:mpv 自带后端(pulse/alsa 自动)
// - 事件:泵线程 mpv_wait_event(0.2s) → QCustomEvent 回投 UI

class CXMpvPump;
struct CXMpvUiEvent;
class CXVideoEq;

class CXEngineMpv : public CXEngine {
    Q_OBJECT
public:
    const char* name() const { return "mpv"; }
    bool isAvailable() const;

    bool open(const QString& url);
    void close();
    bool play();
    bool pause();
    bool stop();
    bool seek(long ms);
    bool setRate(double r);
    bool setVolume(int pct);
    bool setMute(bool on);
    long position() const;
    long duration() const;

    // M3-A:轨道 + 外挂字幕(track-list 枚举/set aid·sid/sub-add)
    int audioTrackCount() const;
    int subtitleTrackCount() const;
    bool setAudioTrack(int idx);      // -1=关闭(暗播)
    bool setSubtitleTrack(int idx);   // -1=关闭
    bool setSubtitleFile(const QString& path);

    // M4-B:mpv 自带 EQ(brightness/contrast/saturation/hue 属性直写)
    bool setEq(const CXVideoEq& eq);
    bool screenshot(const QString& path); // 仅 mpv-gl 路;wid 路恒 false

    // M4-B:渲染路名("mpv-gl"/"mpv-wid"/未 open=0;selftest 探针用)
    const char* renderPath() const { return m_renderPath; }

    ~CXEngineMpv();

protected:
    void customEvent(CustomEventBase* e);

private:
    friend class CXMpvPump;
    friend CXEngine* cxEngineCreateDefault();
    friend CXEngine* cxEngineCreateFor(CxEngineType type);

    CXEngineMpv() : CXEngine(), h_mpv(0), m_pump(0), m_loaded(false),
                    h_rc(0), m_renderPath(0), m_glv(0)
                    , m_seekTarget(-1) { }
    void closeInternal();
    void dispatchEvent(const CXMpvUiEvent* ev);
    bool cxOpenCore(const QString& routeUrl, bool glRoute);

    void* h_mpv;       // mpv_handle*
    void* h_rc;        // mpv_render_context*(M4-B;0=wid 路)
    const char* m_renderPath;
    void* m_glv;       // CXRendererMpvGl*(仅 gl 路)
    CXMpvPump* m_pump;
    bool m_loaded;
    long m_seekTarget;
};

// libmpv 探测(无头;不需要窗口):
// 0 = libmpv 缺包未加载 / 1 = 已加载但 init 失败 / 2 = 全绿
int cxEngineMpvProbeLib();
// 便捷布尔版(== probeLib()==2)
bool cxEngineMpvSmokeHeadless();

#endif // CXENGMPV_H
