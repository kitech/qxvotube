#ifndef CXENGMPV_H
#define CXENGMPV_H

#include "cxengine.h"

// libmpv 内嵌内核(detail.md 第 7 节)
// - 不硬链:dlopen 运行时探测(M1),缺失自动降级
// - Qt3/任意宿主: wid 原生子窗嵌入(无 GL 依赖)
// - 音频:mpv 自带后端(pulse/alsa 自动)
// - 事件:泵线程 mpv_wait_event(0.2s) → QCustomEvent 回投 UI

class CXMpvPump;
struct CXMpvUiEvent;

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

    ~CXEngineMpv();

protected:
    void customEvent(CustomEventBase* e);

private:
    friend class CXMpvPump;
    friend CXEngine* cxEngineCreateDefault();
    friend CXEngine* cxEngineCreateFor(CxEngineType type);

    CXEngineMpv() : CXEngine(), h_mpv(0), m_pump(0), m_loaded(false),
                    m_seekTarget(-1) { }
    void closeInternal();
    void dispatchEvent(const CXMpvUiEvent* ev);

    void* h_mpv;       // mpv_handle*
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
