#ifndef CXVIDEO_GLOBAL_H
#define CXVIDEO_GLOBAL_H

#include "compat34.h"

// cxvideo/qtav34 : Qt3 优先的跨 Qt 播放骨架(接口层,M0 里程碑)
// 机制/命名依据 docs/qtav-evaluation.md + docs/cxvideo-detail.md

#define CXVIDEO_VERSION "0.1.0"

// 引擎选择(auto 由工厂探测顺序决定,见 detail.md 引擎矩阵;
// libmpv 与 cmdmpv 分引擎实现,探测顺序按用户要求 cmdmpv 优先)
enum CxEngineType {
    CxEngineNone = 0,
    CxEngineAuto,
    CxEngineMpv,          // libmpv C-API 内嵌(dlopen)
    CxEngineMpvCmd,       // mpv 命令行子进程(M1)
    CxEngineFfmpeg        // 自研 FFmpeg 三线程管线
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

// Locale 保底修复(mpv 硬性需求;详见 cxmpv_api.cpp 注释):
// 在 QCoreApplication/QApplication 构造之后、第一次触碰播放器之前调用一次
void cxSetLocaleNumericC();

// 视频 EQ(QtAV 同域;M3-A 占位结构,M3-B 实装;-100..100)
struct CXVideoEq {
    int brightness;
    int contrast;
    int saturation;
    int hue;
    CXVideoEq() : brightness(0), contrast(0), saturation(0), hue(0) { }
};

#endif // CXVIDEO_GLOBAL_H
