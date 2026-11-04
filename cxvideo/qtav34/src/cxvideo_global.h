#ifndef CXVIDEO_GLOBAL_H
#define CXVIDEO_GLOBAL_H

#include "compat34.h"

// cxvideo/qtav34 : Qt3 优先的跨 Qt 播放骨架(接口层,M0 里程碑)
// 机制/命名依据 docs/qtav-evaluation.md + docs/cxvideo-detail.md

#define CXVIDEO_VERSION "0.1.0"

// 引擎选择(auto 由工厂探测顺序决定,见 detail.md 引擎矩阵)
enum CxEngineType {
    CxEngineAuto = 0,
    CxEngineMpv = 1,      // libmpv C-API 内嵌
    CxEngineFfmpeg = 2    // 自研 FFmpeg 三线程管线
};

// 状态机(对应 QtAV State Playing/Paused/Stopped)
enum CXVideoState {
    CxStopped = 0,
    CxLoading = 1,
    CxPaused = 2,
    CxPlaying = 3
};

// 错误码(对齐 cxvideo-detail.md 第 9 节)
enum CxErrorCode {
    CXERR_OK = 0,
    CxErrOpenFail = -10,     // 文件/网络流打开失败
    CxErrNoStream = -11,     // 无可播的音视频流
    CxErrDecode = -12,       // 解码持续失败
    CxErrAudio = -13,        // 音频设备打开失败
    CxErrSeek = -14,         // seek 目标超界
    CxErrEngine = -15,       // 引擎底层错误
    CxErrWindowLost = -16    // 内嵌宿主窗口被销毁
};

#endif // CXVIDEO_GLOBAL_H
