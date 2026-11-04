#ifndef CXRENDERER_H
#define CXRENDERER_H

#include "cxframe.h"
#include <qrect.h>

// 渲染器接口(第 2/3 节;独立于 QWidget 层次)
class CXVideoRenderer {
public:
    virtual ~CXVideoRenderer() { }

    // 仅 UI 线程被调用;帧由内部事件投递方出借
    virtual void showFrame(const CXVideoFrame* f) = 0;

    // letterbox 内容区定位(引擎计算后下发)
    virtual void setViewport(const QRect& contentRect) = 0;
};

#endif // CXRENDERER_H
