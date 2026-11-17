#ifdef WITH_CX_FF_AO_SDL
#include "cffao.h"
#include "cxsdl_compat.h"   // SDL2/SDL3 二选一垫片;CX_SDL_MAJOR 由 cxvideo.pri 注入

// push 模式音频后端:
//   SDL2: SDL_OpenAudioDevice + SDL_QueueAudio(拷贝语义) + GetQueuedAudioSize
//   SDL3: SDL_OpenAudioDeviceStream + SDL_PutAudioStreamData + GetAudioStreamQueued
//   两者均不支持 planar → 链上已统一 s16 交错;参数漂移 0(前端已按驱动转换)。
// 版本差异全部收敛在 cxsdl_audio.cpp,本文件不出现 #if。
class CxAoSdl : public CXAudioOut {
public:
    CxAoSdl() : m_h(0), m_ch(0), m_rate(0), m_maxQueue(0) { }
    ~CxAoSdl() { close(); }

    const char* backend() const { return "sdl"; }

    bool open(int ch, int rate)
    {
        m_h = cxSdlAudioOpen(ch, rate, &m_ch, &m_rate);
        if (m_h == 0)
            return false;
        long bpm = (long)m_rate * m_ch * 2 / 1000;   // 字节/ms
        if (bpm <= 0)
            bpm = 176;
        m_maxQueue = bpm * 120;                      // 水位 ≈120ms
        return true;
    }

    bool write(const uint8_t* data, int bytes)
    {
        if (m_h == 0)
            return false;
        // push 模式无原生阻塞回压:队长超水位轮询退水;等待设上限
        // (设备暂停/拔出时兜底放行,防止 athread 卡死无法响应 stop)
        int spins = 0;
        while (m_h != 0 && cxSdlAudioQueued(m_h) > m_maxQueue && spins < 120) {
            SDL_Delay(10);
            spins++;
        }
        return cxSdlAudioWrite(m_h, data, bytes);
    }

    void pause() { cxSdlAudioPause(m_h, true); }
    void resume() { cxSdlAudioPause(m_h, false); }
    void clear() { cxSdlAudioClear(m_h); }

    int latencyMs()
    {
        if (m_h == 0)
            return 0;
        long bpm = (long)m_rate * m_ch * 2 / 1000;
        if (bpm <= 0)
            bpm = 176;
        return cxSdlAudioQueued(m_h) / (int)bpm;
    }

    void drain()
    {
        while (m_h != 0 && cxSdlAudioQueued(m_h) > 0)
            SDL_Delay(10);
    }

    void close()
    {
        if (m_h != 0) {
            cxSdlAudioClose(m_h);
            m_h = 0;
        }
    }

private:
    CxSdlAudio* m_h;
    int m_ch;
    int m_rate;
    int m_maxQueue;
};

CXAudioOut* cffaoNewSdl()
{
#if CX_SDL_MAJOR == 3
    /* SDL3 音频在本机(Qt3 宿主 + dummy/默认设备)实测死锁:
     * 打开后推流阶段整进程卡住(见 docs/cxvideo-vo.md §3)。
     * 先禁用 SDL3 音频 —— 返回 0 让 cffaoCreate 落 silent 兜底;
     * SDL3 只用于视频输出。SDL2 构建下音频行为不变。 */
    return 0;
#else
    return new CxAoSdl();
#endif
}

#endif // WITH_CX_FF_AO_SDL
