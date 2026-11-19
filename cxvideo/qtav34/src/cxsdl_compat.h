#ifndef CXSDL_COMPAT_H
#define CXSDL_COMPAT_H

// SDL2 / SDL3 二选一垫片。
// 两者导出**同名符号**(ABI 不兼容),绝不能同时链接;版本由 cxvideo.pri 的
// CX_SDL_MAJOR 决定:优先 sdl3,缺失回 sdl2;QXVOTUBE_SDL=2|3 可强制。
//
// 差异点(2026-11 核对本机头文件 SDL2 2.32 / SDL3 3.4):
//   - 嵌入已有原生窗口:SDL2 = SDL_CreateWindowFrom;
//     SDL3 删除了该函数,改 SDL_CreateWindowWithProperties + 原生句柄属性。
//   - 渲染器:SDL2 CreateRenderer(-1, ACCELERATED|PRESENTVSYNC) + RenderCopy;
//     SDL3 CreateRenderer(win, name) + SetRenderVSync + RenderTexture。
//   - 音频:SDL2 OpenAudioDevice + QueueAudio;SDL3 OpenAudioDeviceStream +
//     PutAudioStreamData(cffao_sdl 经下面的 cxSdlAudio* 保持版本无关)。
#include "cxplatform.h"

#if defined(WITH_CX_FF_AO_SDL) || defined(WITH_CX_RENDERER_SDL)

#ifndef CX_SDL_MAJOR
#  error "CX_SDL_MAJOR 未定义:cxvideo.pri 的 sdl3/sdl2 探测失败"
#endif

#if CX_SDL_MAJOR == 3
#  include <SDL3/SDL.h>
#else
#  include <SDL2/SDL.h>
#endif

#include <stdint.h>

// ── 窗口:把宿主原生窗口包装成 SDL 窗口 ──
inline SDL_Window* cxSdlWrapNative(void* winId)
{
#if CX_SDL_MAJOR == 3
    SDL_PropertiesID p = SDL_CreateProperties();
    if (p == 0)
        return 0;
#  if defined(CX_OS_WIN)
    SDL_SetPointerProperty(p, SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER, winId);
#  elif defined(CX_OS_MAC)
    SDL_SetPointerProperty(p, SDL_PROP_WINDOW_CREATE_COCOA_VIEW_POINTER, winId);
#  else
    SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_X11_WINDOW_NUMBER,
                          (Sint64)(intptr_t)winId);
#  endif
    SDL_SetBooleanProperty(p, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, false);
    SDL_Window* w = SDL_CreateWindowWithProperties(p);
    SDL_DestroyProperties(p);
    return w;
#else
    return SDL_CreateWindowFrom(winId);
#endif
}

// ── 渲染器 ──
inline bool cxSdlMakeRenderer(SDL_Window* w, SDL_Renderer** out)
{
    if (w == 0 || out == 0)
        return false;
    *out = 0;
#if CX_SDL_MAJOR == 3
    *out = SDL_CreateRenderer(w, 0);
    if (*out == 0)
        return false;
    SDL_SetRenderVSync(*out, 1);
#else
    *out = SDL_CreateRenderer(w, -1,
               SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (*out == 0)
        *out = SDL_CreateRenderer(w, -1, SDL_RENDERER_SOFTWARE);
    if (*out == 0)
        return false;
#endif
    SDL_SetRenderDrawColor(*out, 0, 0, 0, 255);
    return true;
}

// 逻辑尺寸:SDL2=SDL_RenderSetLogicalSize;SDL3 重命名为
// SDL_SetRenderLogicalPresentation(4 参数)。设后所有绘制坐标为 widget 逻辑像素,
// SDL 自动映射到物理输出(修 macOS Retina 下"画面只占 1/4"类症状)。
// 两侧均为等比装入(letterbox),语义一致。
inline bool cxSdlSetLogicalSize(SDL_Renderer* r, int w, int h)
{
    if (r == 0 || w <= 0 || h <= 0) {
        return false;
    }
#if CX_SDL_MAJOR == 3
    return SDL_SetRenderLogicalPresentation(
        r, w, h, SDL_LOGICAL_PRESENTATION_LETTERBOX);
#else
    return SDL_RenderSetLogicalSize(r, w, h) == 0;
#endif
}

// 后端名(opengl/direct3d11/metal/software…):SDL 不保证"无 GL",日志要打出来。
// SDL3 去掉了 SDL_RendererInfo,改用 SDL_GetRendererName。
inline const char* cxSdlRendererName(SDL_Renderer* r)
{
    if (r == 0)
        return "";
#if CX_SDL_MAJOR == 3
    const char* nm = SDL_GetRendererName(r);
    return nm != 0 ? nm : "";
#else
    static SDL_RendererInfo info;
    info.name = 0;
    if (SDL_GetRendererInfo(r, &info) != 0)
        return "";
    return info.name != 0 ? info.name : "";
#endif
}

// 用 int 矩形做中立接口:SDL2=SDL_Rect(int),SDL3=SDL_FRect(float)。
inline void cxSdlBlit(SDL_Renderer* r, SDL_Texture* t,
                      int x, int y, int w, int h)
{
    SDL_RenderClear(r);
#if CX_SDL_MAJOR == 3
    SDL_FRect dst;
    dst.x = (float)x;
    dst.y = (float)y;
    dst.w = (float)w;
    dst.h = (float)h;
    SDL_RenderTexture(r, t, 0, &dst);
#else
    SDL_Rect dst;
    dst.x = (Sint16)x;
    dst.y = (Sint16)y;
    dst.w = (Uint16)w;
    dst.h = (Uint16)h;
    SDL_RenderCopy(r, t, 0, &dst);
#endif
    SDL_RenderPresent(r);
}

// ── 音频(cffao_sdl 使用;实现见 cxsdl_audio.cpp) ──
struct CxSdlAudio;
CxSdlAudio* cxSdlAudioOpen(int ch, int rate, int* gotCh, int* gotRate);
bool cxSdlAudioWrite(CxSdlAudio* h, const void* data, int bytes);
int  cxSdlAudioQueued(CxSdlAudio* h);
void cxSdlAudioPause(CxSdlAudio* h, bool on);
void cxSdlAudioClear(CxSdlAudio* h);
void cxSdlAudioClose(CxSdlAudio* h);

#endif // WITH_CX_FF_AO_SDL || WITH_CX_RENDERER_SDL
#endif // CXSDL_COMPAT_H
