#ifdef WITH_CX_FF_AO_SDL
#include "cffao.h"
#include <SDL.h> // pkg-config sdl2 的 cflags 已由 pri 注入

// SDL2 push 模式(网搜核对:SDL2 wiki OpenAudioDevice/QueueAudio/
// ClearQueuedAudio:callback=NULL + SDL_QueueAudio(拷贝语义)+
// SDL_GetQueuedAudioSize;不支持 planar → 链上已统一 s16 交错;
// 参数漂移 allowed_changes=0:前端已按驱动转换,失败即落链下一后端)
class CxAoSdl : public CXAudioOut {
public:
    CxAoSdl() : m_dev(0), m_maxQueue(0) { }
    ~CxAoSdl() { close(); }

    const char* backend() const { return "sdl"; }

    bool open(int ch, int rate)
    {
        SDL_AudioSpec des, obt;
        SDL_zero(des);
        SDL_zero(obt);
        des.freq = rate;
        des.format = AUDIO_S16SYS;
        des.channels = (Uint8)ch;
        des.samples = 256; // 回调为空,该值仅影响缓冲粒度
        des.callback = NULL;
        des.userdata = NULL;
        if (!SDL_WasInit(SDL_INIT_AUDIO) &&
            SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
            return false;
        m_dev = SDL_OpenAudioDevice(NULL, 0, &des, &obt, 0);
        if (m_dev == 0) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            return false;
        }
        m_obtCh = obt.channels;
        m_obtRate = obt.freq;
        long bpm = (long)m_obtRate * m_obtCh * 2 / 1000; // 字节/ms
        if (bpm <= 0)
            bpm = 176;
        m_maxQueue = bpm * 120; // 水位 ≈120ms
        SDL_PauseAudioDevice(m_dev, 0);
        return true;
    }

    bool write(const uint8_t* data, int bytes)
    {
        if (m_dev == 0)
            return false;
        // push 模式无原生阻塞回压:队长超水位轮询退水;等待设上限
        // (device 暂停/拔出时兜底放行,防止 athread 卡死无法响应 stop)
        int spins = 0;
        while (m_dev != 0 &&
               (int)SDL_GetQueuedAudioSize(m_dev) > m_maxQueue &&
               spins < 120) {
            SDL_Delay(10);
            spins++;
        }
        return SDL_QueueAudio(m_dev, data, (Uint32)bytes) == 0;
    }

    void pause() { SDL_PauseAudioDevice(m_dev, 1); }
    void resume() { SDL_PauseAudioDevice(m_dev, 0); }
    void clear() { SDL_ClearQueuedAudio(m_dev); }
    int latencyMs()
    {
        if (m_dev == 0)
            return 0;
        long bpm = (long)m_obtRate * m_obtCh * 2 / 1000;
        if (bpm <= 0)
            bpm = 176;
        return (int)(SDL_GetQueuedAudioSize(m_dev) / bpm);
    }
    void drain()
    {
        while (m_dev != 0 && SDL_GetQueuedAudioSize(m_dev) > 0)
            SDL_Delay(10);
    }
    void close()
    {
        if (m_dev != 0) {
            SDL_CloseAudioDevice(m_dev);
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            m_dev = 0;
        }
    }

private:
    SDL_AudioDeviceID m_dev;
    int m_maxQueue;
};

CXAudioOut* cffaoNewSdl()
{
    return new CxAoSdl();
}

#endif // WITH_CX_FF_AO_SDL
