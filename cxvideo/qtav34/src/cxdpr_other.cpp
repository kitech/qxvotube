#include "cxdpr.h"

// 非 macOS:无 Retina 逻辑/物理像素换算,恒 1.0。
// Qt5+ 的 devicePixelRatio 由 Qt 绘制栈自行处理,渲染器仍按逻辑像素工作;
// Linux X11 路径行为完全不变。
double cxDevicePixelRatio(QWidget*)
{
    return 1.0;
}

double cxDevicePixelRatio()
{
    return 1.0;
}
