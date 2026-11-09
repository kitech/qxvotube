#include "cffao.h"
#include "cxthread.h"
#include <sys/time.h>
#include <string.h>

// ── silent(恒编:headless/CI;真实时间节流,虚拟延迟)──
class CxAoSilent : public CXAudioOut {
public:
    CxAoSilent() : m_written(0), m_baseUs(0) { }

    const char* backend() const { return "silent"; }

    bool open(int ch, int rate)
    {
        m_obtCh = ch;
        m_obtRate = rate;
        m_written = 0;
        m_baseUs = nowUs();
        return true;
    }

    bool write(const uint8_t* data, int bytes)
    {
        (void)data;
        long bpm = bytesPerMs(); // 字节/ms
        long elapsedUs = nowUs() - m_baseUs;
        long targetMs = (m_written + (long)bytes) / bpm;
        long elapsedMs = elapsedUs / 1000;
        if (targetMs > elapsedMs) { // 实时节流(块 ≤40ms,粒度可接受)
            CXThread::sleep(targetMs - elapsedMs);
            m_baseUs = nowUs() - targetMs * 1000; // 消漂移
        }
        m_written += bytes;
        return true;
    }

    void pause() { }
    void resume() { }
    void clear()
    {
        m_written = 0;
        m_baseUs = nowUs();
    }
    int latencyMs()
    {
        long bpm = bytesPerMs();
        long pend = m_written - (nowUs() - m_baseUs) / 1000 * bpm;
        if (pend < 0)
            pend = 0;
        return (int)(pend / (bpm > 0 ? bpm : 1));
    }
    void drain() { }
    void close() { }

private:
    static long long nowUs()
    {
        struct timeval tv;
        gettimeofday(&tv, 0);
        return (long long)tv.tv_sec * 1000000 + tv.tv_usec;
    }
    long bytesPerMs() const
    {
        long b = (long)m_obtRate * m_obtCh * 2 / 1000;
        return b > 0 ? b : 176;
    }
    long m_written;
    long long m_baseUs;
};

CXAudioOut* cffaoNewSilent()
{
    return new CxAoSilent();
}

// ── 工厂 ──
static CXAudioOut* cffaoCreateNamed(const char* want)
{
    if (strcmp(want, "pulse") == 0) {
#ifdef WITH_CX_FF_AO_PULSE
        return cffaoNewPulse();
#endif
        return 0;
    }
    if (strcmp(want, "alsa") == 0) {
#ifdef WITH_CX_FF_AO_ALSA
        return cffaoNewAlsa();
#endif
        return 0;
    }
    if (strcmp(want, "sdl") == 0) {
#ifdef WITH_CX_FF_AO_SDL
        return cffaoNewSdl();
#endif
        return 0;
    }
    if (strcmp(want, "silent") == 0)
        return cffaoNewSilent();
    return 0;
}

CXAudioOut* cffaoCreate(const char* want)
{
    if (want != 0 && want[0] != '\0' && strcmp(want, "auto") != 0) {
        CXAudioOut* ao = cffaoCreateNamed(want);
        if (ao == 0)
            return cffaoCreateNamed("silent");
        return ao;
    }
    // auto:alsa → pulse → sdl → silent
    // Linux 上 alsa 直通最稳(pipewire/pulse 栈也暴露 ALSA);pulse-simple
    // 的阻塞写不可中断,仅作显式/次级后端;SDL 跨平台兜底;CI 无设备时
    // 各 open 失败 → silent 实时节流照走
    CXAudioOut* ao = cffaoCreateNamed("alsa");
    if (ao == 0)
        ao = cffaoCreateNamed("pulse");
    if (ao == 0)
        ao = cffaoCreateNamed("sdl");
    if (ao == 0)
        ao = cffaoCreateNamed("silent");
    return ao;
}
