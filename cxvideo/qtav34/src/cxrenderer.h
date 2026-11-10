#ifndef CXRENDERER_H
#define CXRENDERER_H

#include "cxframe.h"
#include <qrect.h>

struct CXVideoEq; // cxvideo_global.h 中的软路 EQ(M3-B)

// 渲染器接口(第 2/3 节;独立于 QWidget 层次)
class CXVideoRenderer {
public:
    virtual ~CXVideoRenderer() { }

    // 仅 UI 线程被调用;帧由内部事件投递方出借
    virtual void showFrame(const CXVideoFrame* f) = 0;

    // letterbox 内容区定位(引擎计算后下发)
    virtual void setViewport(const QRect& contentRect) = 0;

    // M3-B:软路 EQ(默认 no-op,仅软渲落地;GL 侧 M4)
    virtual void setVideoEq(const CXVideoEq& /*eq*/) { }

    // M3-B:截图(默认 false;软渲保存当前帧 LUT 后画面)
    virtual bool saveScreenshot(const QString& /*path*/) { return false; }
};

#endif // CXRENDERER_H
