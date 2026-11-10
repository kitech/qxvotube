#include "cffhw.h"
#include <stdio.h>

const char* cffHwTypeName(int type)
{
    switch (type) {
    case CxFFHwVaapi: return "vaapi";
    case CxFFHwCuda:  return "cuda";
    default: return 0;
    }
}

static int cffHwAvType(int type)
{
    switch (type) {
    case CxFFHwVaapi: return AV_HWDEVICE_TYPE_VAAPI;
    case CxFFHwCuda:  return AV_HWDEVICE_TYPE_CUDA;
    default: return AV_HWDEVICE_TYPE_NONE;
    }
}

bool cffHwDeviceCtxCreate(int type, AVBufferRef** dev)
{
    *dev = 0;
    if (cffHwAvType(type) == AV_HWDEVICE_TYPE_NONE)
        return false;
    if (av_hwdevice_ctx_create(dev, (AVHWDeviceType)cffHwAvType(type),
                               0, 0, 0) < 0)
        return false;
    return *dev != 0;
}

int cffHwProbe()
{
    static const int kOrder[] = { CxFFHwVaapi, CxFFHwCuda };
    for (int i = 0; i < 2; i++) {
        AVBufferRef* dev = 0;
        if (cffHwDeviceCtxCreate(kOrder[i], &dev) && dev != 0) {
            av_buffer_unref(&dev);
            return kOrder[i];
        }
    }
    return CxFFHwNone;
}

int cffHwPixFmt(int type)
{
    switch (type) {
    case CxFFHwVaapi: return AV_PIX_FMT_VAAPI;
    case CxFFHwCuda:  return AV_PIX_FMT_CUDA;
    default: return AV_PIX_FMT_NONE;
    }
}

bool cffHwDownload(AVFrame* src, AVFrame* dst)
{
    if (src == 0 || dst == 0 || (src->format != AV_PIX_FMT_VAAPI &&
                                 src->format != AV_PIX_FMT_CUDA))
        return false;
    dst->format = AV_PIX_FMT_NV12; // hw surface 侧的共同输出格式
    if (av_hwframe_transfer_data(dst, src, 0) < 0)
        return false;
    dst->pts = src->pts;
    dst->best_effort_timestamp = src->best_effort_timestamp;
    dst->duration = src->duration;
    return true;
}
