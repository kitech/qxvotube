#include "compat34.h"
#include "cxengine.h"
#include "eng_mpv.h"
#include "eng_mpv_cmd.h"
#include "eng_ffmpeg.h"

// 探测顺序:cmdmpv → libmpv(dlopen)→ ffmpeg(自研,M2)
// (libmpv 与 cmdmpv 分引擎实现;探测顺序按用户要求 cmdmpv 优先)
CXEngine* cxEngineCreateFor(CxEngineType type)
{
#ifdef WITH_CX_ENGINE_MPV
    if (type == CxEngineMpv) {
        CXEngineMpv* m = new CXEngineMpv();
        if (m->isAvailable())
            return m;
        delete m;
        return 0;
    }
#endif
    if (type == CxEngineMpvCmd) {
        CXEngineMpvCmd* c = new CXEngineMpvCmd();
        if (c->isAvailable())
            return c;
        delete c;
        return 0;
    }
#ifdef WITH_CX_ENGINE_FFMPEG
    if (type == CxEngineFfmpeg) {
        CXEngineFfmpeg* f = new CXEngineFfmpeg();
        if (f->isAvailable())
            return f;
        delete f;
        return 0;
    }
#endif
    return 0; // 其余未知类型 / 未编入内核
}

CXEngine* cxEngineCreateDefault()
{
    // 1) cmdmpv:mpv 命令行子进程(仅进程级控制,M2 接 IPC)
    {
        CXEngineMpvCmd* c = new CXEngineMpvCmd();
        if (c->isAvailable())
            return c;
        delete c;
    }
    // 2) libmpv:dlopen 内嵌
#ifdef WITH_CX_ENGINE_MPV
    {
        CXEngineMpv* m = new CXEngineMpv();
        if (m->isAvailable())
            return m;
        delete m;
    }
#endif
    // 3) ffmpeg 软解自研内核(M2,M2.0:解码+渲染,无音频主时钟)
#ifdef WITH_CX_ENGINE_FFMPEG
    {
        CXEngineFfmpeg* f = new CXEngineFfmpeg();
        if (f->isAvailable())
            return f;
        delete f;
    }
#endif
    return 0;
}
