#include "cxmpv_def.h"

// unix 优先(mac 亦为 libmpv 动态库);win32 留待 M4 打包阶段
#ifndef _WIN32

#include <dlfcn.h>
#include <locale.h>

//Qt(QApplication/QCoreApplication 构造)会 setlocale(LC_ALL,"") 抢占 locale;
//libmpv check_locale()(player/main.c)硬性要求 LC_NUMERIC ∈ {C,C.UTF-8},
//否则 mpv_create 返回 NULL。必须在 QCoreApplication 构造之后调用一次(与
// mpv-examples/libmpv/qt_opengl/main.cpp 官方示例一致)。
void cxSetLocaleNumericC()
{
    setlocale(LC_NUMERIC, "C");
}

static const char* kMpvSoname[] = {
    "libmpv.so.2",
    "libmpv.so",
    0
};

static void* cxMpvOpenLib()
{
    for (int i = 0; kMpvSoname[i] != 0; i++) {
        void* lib = dlopen(kMpvSoname[i], RTLD_NOW | RTLD_GLOBAL);
        if (lib != 0)
            return lib;
    }
    return 0;
}

static void* cxMpvSym(void* lib, const char* name, bool required, bool* ok)
{
    void* sym = dlsym(lib, name);
    if (sym == 0 && required)
        *ok = false;
    return sym;
}

#else // _WIN32

static void* cxMpvOpenLib()
{
    return (void*)LoadLibraryA("libmpv-2.dll");
}

static void* cxMpvSym(void* lib, const char* name, bool required, bool* ok)
{
    void* sym = (void*)GetProcAddress((HMODULE)lib, name);
    if (sym == 0 && required)
        *ok = false;
    return sym;
}

void cxSetLocaleNumericC()
{
    // win32 无 LC_NUMERIC 语义;空实现保持 ABI 一致
}

#endif

CxMpvApi& CxMpvApi::instance()
{
    static CxMpvApi apiInst;
    return apiInst;
}

bool CxMpvApi::ensure()
{
    if (loaded)
        return true;
    if (lib != 0) // 曾尝试加载但失败
        return false;

#ifndef _WIN32
    // 保底修复(正式路径应在 app 初始化时调 cxSetLocaleNumericC)
    cxSetLocaleNumericC();
#endif
    lib = cxMpvOpenLib();
    if (lib == 0)
        return false;

    bool ok = true;
    mpv_create            = (CxMpvCreateFn)cxMpvSym(lib, "mpv_create", true, &ok);
    mpv_initialize        = (CxMpvInitFn)cxMpvSym(lib, "mpv_initialize", true, &ok);
    mpv_terminate_destroy = (CxMpvTerminateDestroyFn)cxMpvSym(lib, "mpv_terminate_destroy", true, &ok);
    mpv_set_option_string = (CxMpvSetOptionStringFn)cxMpvSym(lib, "mpv_set_option_string", true, &ok);
    mpv_command           = (CxMpvCommandFn)cxMpvSym(lib, "mpv_command", true, &ok);
    mpv_observe_property  = (CxMpvObservePropertyFn)cxMpvSym(lib, "mpv_observe_property", true, &ok);
    mpv_wait_event        = (CxMpvWaitEventFn)cxMpvSym(lib, "mpv_wait_event", true, &ok);
    mpv_set_wakeup_callback = (CxMpvSetWakeupFn)cxMpvSym(lib, "mpv_set_wakeup_callback", true, &ok);
    mpv_client_api_version = (CxMpvClientApiVersionFn)cxMpvSym(lib, "mpv_client_api_version", true, &ok);
    mpv_event_name        = (CxMpvEventNameFn)cxMpvSym(lib, "mpv_event_name", false, &ok);
    mpv_free              = (CxMpvFreeFn)cxMpvSym(lib, "mpv_free", false, &ok);
    mpv_wakeup            = (CxMpvWakeupFn)cxMpvSym(lib, "mpv_wakeup", false, &ok);

    if (!ok) {
        // 不 dlclose:避免与后续线程环境互相残杀,just 永久停用
        loaded = false;
        return false;
    }
    loaded = true;
    return true;
}
