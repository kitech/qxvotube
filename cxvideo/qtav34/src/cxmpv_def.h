#ifndef CXMPV_DEF_H
#define CXMPV_DEF_H

// libmpv(client.h) 的稳定 ABI 镜像(dlopen 不带头文件,数值为 mpv 官方冻结值)
// 依据:cxvideo-detail.md 第 7 节;许可注记见 cxvideo/README.md

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <string.h>

typedef void CxMpvHandle; // mpv_handle*

// mpv_format(client.h 冻结值)
enum CxMpvFormat {
    CxMpvFmtNone = 0,
    CxMpvFmtString = 1,      // 官方冻结序(旧版错位已修:Double 曾误配 8)
    CxMpvFmtOsdString = 2,
    CxMpvFmtFlag = 3,
    CxMpvFmtInt64 = 4,
    CxMpvFmtDouble = 5,
    CxMpvFmtNode = 6,
    CxMpvFmtNodeArray = 7,
    CxMpvFmtNodeMap = 8,
    CxMpvFmtByteArray = 9
};

// mpv_event_id(client.h 冻结值)
enum CxMpvEventId {
    CxMpvEventNone = 0,
    CxMpvEventShutdown = 1,
    CxMpvEventLogMessage = 2,
    CxMpvEventGetPropertyReply = 3,
    CxMpvEventSetPropertyReply = 4,
    CxMpvEventCommandReply = 5,
    CxMpvEventStartFile = 6,
    CxMpvEventEndFile = 7,
    CxMpvEventFileLoaded = 8,     // MPV_EVENT_FILE_LOADED
    CxMpvEventTracksChanged = 9,
    CxMpvEventTrackSwitched = 10,
    CxMpvEventIdle = 11,
    CxMpvEventTick = 14,          // 已废弃,不消费
    CxMpvEventClientMessage = 16,
    CxMpvEventVideoReconfig = 17,
    CxMpvEventAudioReconfig = 18,
    CxMpvEventSeek = 20,
    CxMpvEventPlaybackRestart = 21,
    CxMpvEventPropertyChange = 22,
    CxMpvEventQueueOverflow = 24
};

enum CxMpvEndFileReason {
    CxMpvEndFileEof = 0,
    CxMpvEndFileMarked = 1,
    CxMpvEndFileStop = 2,
    CxMpvEndFileQuit = 3,
    CxMpvEndFileError = 4
};

// mpv_event 镜像(client.h: int event_id; int error; uint64 reply_userdata; void* data)
struct CxMpvEvent {
    int event_id;
    int error;
    unsigned long long reply_userdata;
    void* data;
};

// mpv_event_property 镜像
struct CxMpvEventProperty {
    const char* name;
    int format;
    void* data;
};

// mpv_event_end_file 镜像(reason 恒为首字段)
struct CxMpvEventEndFileData {
    long long reason;
    long long error;
};

typedef void* (*CxMpvCreateFn)(void);
typedef int (*CxMpvInitFn)(void*);
typedef void (*CxMpvTerminateDestroyFn)(void*);
typedef int (*CxMpvSetOptionStringFn)(void*, const char*, const char*);
typedef int (*CxMpvCommandFn)(void*, const char**);
typedef int (*CxMpvObservePropertyFn)(void*, unsigned long long, const char*, int);
typedef void* (*CxMpvWaitEventFn)(void*, double);
typedef void (*CxMpvSetWakeupFn)(void*, void (*)(void*), void*);
typedef unsigned long long (*CxMpvClientApiVersionFn)(void);
typedef const char* (*CxMpvEventNameFn)(int);
typedef void (*CxMpvFreeFn)(void*);
typedef void (*CxMpvWakeupFn)(void*);
typedef int (*CxMpvGetPropertyFn)(void*, const char*, int, void*); // M3-A 轨道枚举
typedef int (*CxMpvSetPropertyFn)(void*, const char*, int, void*); // M4-B EQ 属性直写

// M4-B:render.h/render_gl.h 官方冻结值(dlopen 不带头文件)
typedef void CxMpvRenderCtx; // mpv_render_context*(opaque)
typedef void* (*CxMpvRenderGetProc)(void* ctx, const char* name);

// mpv_render_param(render.h 镜像)
typedef struct CxMpvRenderParam {
    int type;
    void* data;
} CxMpvRenderParam;

// mpv_render_param_type(render.h 冻结值)
enum CxMpvRenderParamType {
    CxMpvRenderParamInvalid          = 0,
    CxMpvRenderParamApiType          = 1, // "opengl"
    CxMpvRenderParamOpenGLInitParams = 2,
    CxMpvRenderParamOpenGLFbo        = 3,
    CxMpvRenderParamFlipY            = 4,
    CxMpvRenderParamDepth            = 5,
    CxMpvRenderParamIccProfile       = 6,
    CxMpvRenderParamAlphaMode        = 7
};

// mpv_opengl_init_params(render_gl.h 冻结布局前两字段)
typedef struct CxMpvOpenglInitParams {
    CxMpvRenderGetProc get_proc_address;
    void*              get_proc_address_ctx;
} CxMpvOpenglInitParams;

// mpv_opengl_fbo(render_gl.h)
typedef struct CxMpvOpenglFbo {
    int fbo;
    int w;
    int h;
    int internal_format; // 0=默认 RGBA8
} CxMpvOpenglFbo;

typedef int  (*CxMpvRenderCreateFn)(CxMpvRenderCtx** ctx, void* mpv,
                                     CxMpvRenderParam* params);
typedef void (*CxMpvRenderFreeFn)(CxMpvRenderCtx* ctx);
typedef int  (*CxMpvRenderRenderFn)(CxMpvRenderCtx* ctx, CxMpvRenderParam* params);
typedef void (*CxMpvRenderSetUpdateCbFn)(CxMpvRenderCtx* ctx,
                                         void (*cb)(void*), void* cb_ctx);
typedef void (*CxMpvRenderReportSwapFn)(CxMpvRenderCtx* ctx);
typedef int  (*CxMpvRenderUpdateFn)(CxMpvRenderCtx* ctx); // 旧 API 兼容

class CxMpvApi {
public:
    static CxMpvApi& instance();

    // 幂等:加载一次;任一必需符号缺失视为不可用
    bool ensure();

    bool loaded;
    void* lib;
    CxMpvCreateFn mpv_create;
    CxMpvInitFn mpv_initialize;
    CxMpvTerminateDestroyFn mpv_terminate_destroy;
    CxMpvSetOptionStringFn mpv_set_option_string;
    CxMpvCommandFn mpv_command;
    CxMpvObservePropertyFn mpv_observe_property;
    CxMpvWaitEventFn mpv_wait_event;
    CxMpvSetWakeupFn mpv_set_wakeup_callback;
    CxMpvClientApiVersionFn mpv_client_api_version;
    CxMpvEventNameFn mpv_event_name;
    CxMpvFreeFn mpv_free;
    CxMpvWakeupFn mpv_wakeup;
    CxMpvGetPropertyFn mpv_get_property; // M3-A(可选;缺则轨道 API 返 0/false)
    // M4-B(全可选;render 四件套齐才开 gl 路,缺则回退 wid)
    CxMpvSetPropertyFn mpv_set_property;
    CxMpvRenderCreateFn      mpv_render_context_create;
    CxMpvRenderFreeFn        mpv_render_context_free;
    CxMpvRenderRenderFn      mpv_render_context_render;
    CxMpvRenderSetUpdateCbFn mpv_render_context_set_update_callback;
    CxMpvRenderReportSwapFn  mpv_render_context_report_swap;
    CxMpvRenderUpdateFn      mpv_render_context_update;

private:
    CxMpvApi() : loaded(false), lib(0), mpv_create(0), mpv_initialize(0),
                 mpv_terminate_destroy(0), mpv_set_option_string(0),
                 mpv_command(0), mpv_observe_property(0), mpv_wait_event(0),
                 mpv_set_wakeup_callback(0), mpv_client_api_version(0),
                 mpv_event_name(0), mpv_free(0), mpv_wakeup(0),
                 mpv_get_property(0),
                 mpv_set_property(0),
                 mpv_render_context_create(0), mpv_render_context_free(0),
                 mpv_render_context_render(0),
                 mpv_render_context_set_update_callback(0),
                 mpv_render_context_report_swap(0),
                 mpv_render_context_update(0) { }
};

#endif // CXMPV_DEF_H
