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
    CxMpvFmtString = 1,
    CxMpvFmtNode = 2,
    CxMpvFmtNodeArray = 3,
    CxMpvFmtNodeMap = 4,
    CxMpvFmtOsdString = 5,
    CxMpvFmtFlag = 6,
    CxMpvFmtInt64 = 7,
    CxMpvFmtDouble = 8
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

private:
    CxMpvApi() : loaded(false), lib(0), mpv_create(0), mpv_initialize(0),
                 mpv_terminate_destroy(0), mpv_set_option_string(0),
                 mpv_command(0), mpv_observe_property(0), mpv_wait_event(0),
                 mpv_set_wakeup_callback(0), mpv_client_api_version(0),
                 mpv_event_name(0), mpv_free(0), mpv_wakeup(0) { }
};

#endif // CXMPV_DEF_H
