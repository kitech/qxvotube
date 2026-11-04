#include "cxengine.h"
#include "eng_mpv.h"
#include "eng_ffmpeg.h"

// 探测顺序:mpv → ffmpeg(backend-abstraction.md 引擎矩阵,主推荐 CxVideo)
CXEngine* cxEngineCreateDefault()
{
#if !defined(WITH_CX_ENGINE_MPV) && !defined(WITH_CX_ENGINE_FFMPEG)
    // M0:M1/M2 内核宏启用前,保持可编可链、恒为空实现
    return 0;
#else
    CXEngine* e = 0;
#ifdef WITH_CX_ENGINE_MPV
    {
        CXEngineMpv* m = new CXEngineMpv();
        if (m->isAvailable())
            return m;
        delete m;
    }
#endif
#ifdef WITH_CX_ENGINE_FFMPEG
    {
        CXEngineFfmpeg* f = new CXEngineFfmpeg();
        if (f->isAvailable())
            return f;
        delete f;
    }
#endif
    e = 0; // 两内核均不可用
    return e;
#endif
}
