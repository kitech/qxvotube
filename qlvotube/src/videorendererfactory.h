#ifndef VIDEORENDERERFACTORY_H
#define VIDEORENDERERFACTORY_H

#include "compat34.h"
#include <qwidget.h>
#include <qstring.h>
#include <qstringlist.h>

class CXVideoRenderer;

// 视频输出后端工厂(qlvotube 侧,见 docs/cxvideo-vo.md)
//   out ∈ {auto, xv, x11, sdl, raster, d3d11, metal, gl}
//   auto = 平台链,无 GL 优先,gl 永远最后
struct CxVideoOut {
    QWidget*         widget;     // 挂到 VideoPane::surface()
    CXVideoRenderer* renderer;   // 交给 CXVideoPlayer
    QString          name;       // 实际生效的后端
    QString          why;        // 选择/回退原因(写日志)
};

// envOverride:QLVOTUBE_VO_OUT(非空时优先于配置)
CxVideoOut cxCreateVideoOut(QWidget* host, const QString& out,
                            const QString& envOverride);

// 归一化选择值(auto/xv/x11/sdl/raster/d3d11/metal/gl)
QString cxResolveVideoOut(const QString& out, const QString& envOverride);

// 当前构建实际可用的后端(配置页灰显用)
QStringList cxAvailableVideoOuts();

#endif // VIDEORENDERERFACTORY_H
