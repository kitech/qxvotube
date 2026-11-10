#ifndef CXFFHW_H
#define CXFFHW_H

#include "cxff_wrap.h"

// M4-A:ffmpeg 硬解探活 + hw 帧下载(detail.md §7.6)
// 序:vaapi → cuda/nvdec;任一不可用静默落软解(现路径零改动)

enum CxFFHwType {
    CxFFHwNone = 0,
    CxFFHwVaapi,
    CxFFHwCuda
};

// 探活:返回命中的类型(创建即释放;真正在用的 device 由 CxDecoder 建)
int cffHwProbe();

// 创建 hwdevice ctx;失败 false(调用方落软解)
bool cffHwDeviceCtxCreate(int type, AVBufferRef** dev);

// 该 hw 类型在 get_format 里要挑的 pixfmt(VAAPI/CUDA)
int cffHwPixFmt(int type);

// hw 名("vaapi"/"cuda";CxFFHwNone→0)
const char* cffHwTypeName(int type);

// hw 帧 → 系统内存帧(dst 已 alloc 的空帧);成功 true
bool cffHwDownload(AVFrame* src, AVFrame* dst);

#endif // CXFFHW_H
