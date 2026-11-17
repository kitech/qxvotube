#include "cxsdl_compat.h"

#if defined(WITH_CX_FF_AO_SDL) || defined(WITH_CX_RENDERER_SDL)

// SDL2/SDL3 音频后端垫片实现。
// SDL2:OpenAudioDevice + QueueAudio(push 拷贝语义)
// SDL3:OpenAudioDeviceStream + PutAudioStreamData
// 两者对外语义一致:write=入队、queued=云端未播字节、pause=暂停设备。
struct CxSdlAudio {
#if CX_SDL_MAJOR == 3
    SDL_AudioStream* st;
#else
    SDL_AudioDeviceID dev;
#endif
    int ch;
    int rate;
#if CX_SDL_MAJOR == 3
    CxSdlAudio() : st(0), ch(0), rate(0) { }
#else
    CxSdlAudio() : dev(0), ch(0), rate(0) { }
#endif
};

CxSdlAudio* cxSdlAudioOpen(int ch, int rate, int* gotCh, int* gotRate)
{
    if (!SDL_WasInit(SDL_INIT_AUDIO) &&
        SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
        return 0;
    CxSdlAudio* h = new CxSdlAudio();
    h->ch = ch;
    h->rate = rate;
#if CX_SDL_MAJOR == 3
    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.format = SDL_AUDIO_S16;
    spec.channels = ch;
    spec.freq = rate;
    h->st = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                      &spec, 0, 0);
    if (h->st == 0) {
        delete h;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return 0;
    }
    SDL_ResumeAudioStreamDevice(h->st);
#else
    SDL_AudioSpec des, obt;
    SDL_zero(des);
    SDL_zero(obt);
    des.freq = rate;
    des.format = AUDIO_S16SYS;
    des.channels = (Uint8)ch;
    des.samples = 256;      // 回调为空,该值仅影响缓冲粒度
    des.callback = 0;
    des.userdata = 0;
    h->dev = SDL_OpenAudioDevice(0, 0, &des, &obt, 0);
    if (h->dev == 0) {
        delete h;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return 0;
    }
    h->ch = obt.channels;
    h->rate = obt.freq;
    SDL_PauseAudioDevice(h->dev, 0);
#endif
    if (gotCh) *gotCh = h->ch;
    if (gotRate) *gotRate = h->rate;
    return h;
}

bool cxSdlAudioWrite(CxSdlAudio* h, const void* data, int bytes)
{
    if (h == 0)
        return false;
#if CX_SDL_MAJOR == 3
    return SDL_PutAudioStreamData(h->st, data, bytes) == 0;
#else
    return SDL_QueueAudio(h->dev, data, (Uint32)bytes) == 0;
#endif
}

int cxSdlAudioQueued(CxSdlAudio* h)
{
    if (h == 0)
        return 0;
#if CX_SDL_MAJOR == 3
    return (int)SDL_GetAudioStreamQueued(h->st);
#else
    return (int)SDL_GetQueuedAudioSize(h->dev);
#endif
}

void cxSdlAudioPause(CxSdlAudio* h, bool on)
{
    if (h == 0)
        return;
#if CX_SDL_MAJOR == 3
    if (on)
        SDL_PauseAudioStreamDevice(h->st);
    else
        SDL_ResumeAudioStreamDevice(h->st);
#else
    SDL_PauseAudioDevice(h->dev, on ? 1 : 0);
#endif
}

void cxSdlAudioClear(CxSdlAudio* h)
{
    if (h == 0)
        return;
#if CX_SDL_MAJOR == 3
    SDL_ClearAudioStream(h->st);
#else
    SDL_ClearQueuedAudio(h->dev);
#endif
}

void cxSdlAudioClose(CxSdlAudio* h)
{
    if (h == 0)
        return;
#if CX_SDL_MAJOR == 3
    if (h->st != 0)
        SDL_DestroyAudioStream(h->st);
#else
    if (h->dev != 0)
        SDL_CloseAudioDevice(h->dev);
#endif
    delete h;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

#endif // WITH_CX_FF_AO_SDL || WITH_CX_RENDERER_SDL
