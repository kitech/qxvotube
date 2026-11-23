#ifdef WITH_CX_FF_AO_ALSA
#include "cffao.h"
#include <alsa/asoundlib.h>
#include <stdlib.h>   // getenv/atoi

// ALSA 阻塞模式(网搜核对:alsa-project PCM R/W 接口 + SO 50103858/23092895:
// RW_INTERLEAVED + snd_pcm_set_params(latency≈500ms)、writei 短写循环、
// -EPIPE → snd_pcm_recover、drain 后必须 prepare 否则后续 writei 报 EPIPE)
class CxAoAlsa : public CXAudioOut {
public:
    CxAoAlsa() : m_h(0) { }
    ~CxAoAlsa() { close(); }

    const char* backend() const { return "alsa"; }

    bool open(int ch, int rate)
    {
        /* 默认 200ms(500ms 会给音画同步留下过大的绝对值);
         * QXVOTUBE_AO_LATENCY_MS 可覆盖;失败回退 500ms 兼容老插件。 */
        unsigned int latUs = 200000;
        const char* e = getenv("QXVOTUBE_AO_LATENCY_MS");
        if (e != 0 && *e != 0) {
            int ms = atoi(e);
            if (ms >= 40 && ms <= 1000)
                latUs = (unsigned int)ms * 1000;
        }
        if (openWithLatency(ch, rate, latUs))
            return true;
        if (latUs != 500000 && openWithLatency(ch, rate, 500000))
            return true;
        return false;
    }

    bool openWithLatency(int ch, int rate, unsigned int latUs)
    {
        if (snd_pcm_open(&m_h, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0)
            return false;
        if (snd_pcm_set_params(m_h, SND_PCM_FORMAT_S16_LE,
                               SND_PCM_ACCESS_RW_INTERLEAVED,
                               (unsigned int)ch, (unsigned int)rate,
                               1, latUs) < 0) {
            close();
            return false;
        }
        m_obtCh = ch;
        m_obtRate = rate;
        return true;
    }

    bool write(const uint8_t* data, int bytes)
    {
        if (m_h == 0)
            return false;
        int bpFrame = m_obtCh * 2; // s16 交错
        int frames = bytes / bpFrame;
        const uint8_t* p = data;
        while (frames > 0) {
            snd_pcm_sframes_t w = snd_pcm_writei(m_h, p,
                                                 (unsigned long)frames);
            if (w > 0) {
                p += (long)w * bpFrame;
                frames -= (int)w;
                continue;
            }
            if (w == 0) {
                // 设备暂停/不可写时 writei 返回 0:有界等待可写,超时即退出,
                // 否则 frames 不减会在此无限空转烧满一个核(暂停 CPU 100%)
                int r = snd_pcm_wait(m_h, 100);
                if (r <= 0) {
                    return false; // 超时/出错,交回上层退出本次写入
                }
                continue;
            }
            if (w < 0 && snd_pcm_recover(m_h, (int)w, 0) < 0)
                return false;
        }
        return true;
    }

    void pause()
    {
        if (m_h != 0)
            snd_pcm_pause(m_h, 1); // 插件不支持时忽略(athread 停喂兜底)
    }
    void resume()
    {
        if (m_h != 0)
            snd_pcm_pause(m_h, 0);
    }
    void clear()
    {
        if (m_h == 0)
            return;
        snd_pcm_drop(m_h);
        snd_pcm_prepare(m_h); // drop 后必须 prepare,否则 writei 必 EPIPE
    }
    int latencyMs()
    {
        if (m_h == 0)
            return 0;
        snd_pcm_sframes_t d = 0;
        if (snd_pcm_delay(m_h, &d) < 0)
            return 0;
        if (d < 0)
            d = 0;
        return (int)((long)d * 1000 / m_obtRate);
    }
    void drain()
    {
        if (m_h == 0)
            return;
        snd_pcm_drain(m_h);
        snd_pcm_prepare(m_h);
    }
    void close()
    {
        if (m_h != 0) {
            snd_pcm_close(m_h);
            m_h = 0;
        }
    }

private:
    snd_pcm_t* m_h;
};

CXAudioOut* cffaoNewAlsa()
{
    return new CxAoAlsa();
}

#endif // WITH_CX_FF_AO_ALSA
