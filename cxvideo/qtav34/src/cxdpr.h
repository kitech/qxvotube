#ifndef CXDPR_H
#define CXDPR_H

class QWidget;

// 设备像素比(device pixel ratio)平台桥接。
// macOS:NSView convertSizeToBacking → window backingScaleFactor →
//        NSScreen mainScreen backingScaleFactor → 1.0 逐级回退;
// 其他平台:恒返回 1.0(Linux X11 行为不变)。
// 返回 >= 1.0;取不到任何信息时 1.0,不抛异常、不依赖用户配置。
double cxDevicePixelRatio(QWidget* w);
double cxDevicePixelRatio();

#endif // CXDPR_H
