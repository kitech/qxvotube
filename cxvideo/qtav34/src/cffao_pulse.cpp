#ifdef WITH_CX_FF_AO_PULSE
#include "cffao.h"
extern "C" {
#include <pulse/simple.h>
#include <pulse/error.h>
}

// pulse-simple(网搜核对:freedesktop simple.html / pacat-simple.c:
// write 阻塞,get_latency 单位 us,drain/flush;无 pause 原语 → 停喂即静音)
class CxAoPulse : public CXAudioOut {
public:
    CxAoPulse() : m_s(0), m_err(0) { }
    ~CxAoPulse() { close(); }

    const char* backend() const { return "pulse"; }

    bool open(int ch, int rate)
    {
        pa_sample_spec ss;
        ss.format = PA_SAMPLE_S16LE;
        ss.channels = (uint8_t)ch;
        ss.rate = (uint32_t)rate;
        m_s = pa_simple_new(NULL, "cxvideo", PA_STREAM_PLAYBACK, NULL,
                            "playback", &ss, NULL, NULL, &m_err);
        if (m_s == 0)
            return false;
        m_obtCh = ch;
        m_obtRate = rate;
        return true;
    }

    bool write(const uint8_t* data, int bytes)
    {
        if (m_s == 0)
            return false;
        return pa_simple_write(m_s, data, (size_t)bytes, &m_err) >= 0;
    }

    void pause() { }  // pulse-simple 无原生 pause:athread 停喂 + athread sleep
    void resume() { }
    void clear() { if (m_s != 0) pa_simple_flush(m_s, &m_err); }

    int latencyMs()
    {
        if (m_s == 0)
            return 0;
        pa_usec_t l = pa_simple_get_latency(m_s, &m_err);
        if (l == (pa_usec_t)-1)
            return 0;
        return (int)(l / 1000);
    }

    void drain() { if (m_s != 0) pa_simple_drain(m_s, &m_err); }

    void close()
    {
        if (m_s != 0) {
            pa_simple_free(m_s);
            m_s = 0;
        }
    }

private:
    pa_simple* m_s;
    int m_err;
};

CXAudioOut* cffaoNewPulse()
{
    return new CxAoPulse();
}

#endif // WITH_CX_FF_AO_PULSE
