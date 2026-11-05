#include "compat34.h"
#include "cxengine.h"
#include "eng_mpv.h"
#include "eng_mpv_cmd.h"
#include "eng_ffmpeg.h"

// 探测顺序:cmdmpv → libmpv(dlopen)→ ffmpeg
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
    (void)0; // ffmpeg 内核 M2 实装;其余未知类型
    return 0;
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
    // 3) ffmpeg 内核 M2 实装;全缺返回 0
    return 0;
}
