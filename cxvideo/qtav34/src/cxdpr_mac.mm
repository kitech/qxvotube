#include "cxdpr.h"
#include "cxplatform.h"
#include <qwidget.h>

#if defined(CX_OS_MAC)

#import <AppKit/NSView.h>
#import <AppKit/NSWindow.h>
#import <AppKit/NSScreen.h>
#import <CoreGraphics/CoreGraphics.h>

// 3) 兜底:主屏 backingScaleFactor(10.7+;不依赖 widget 是否 native)
static double cxDprFromScreen()
{
    NSScreen* s = [NSScreen mainScreen];
    if (s == nil) {
        return 1.0;
    }
    double d = [s backingScaleFactor];
    return d > 0.0 ? d : 1.0;
}

double cxDevicePixelRatio(QWidget* w)
{
    if (w == 0) {
        return cxDprFromScreen();
    }

    NSView* v = (NSView*)(w->winId());   // winId() 会强制 native,故此处必有效
    if (v != nil) {
        int lw = w->width();
        int lh = w->height();
        // 1) 首选:convertSizeToBacking —— 反映该 widget 所在屏幕的实时 dpr
        if (lw > 0 && lh > 0) {
            CGSize bs = [v convertSizeToBacking:
                            CGSizeMake((CGFloat)lw, (CGFloat)lh)];
            if (bs.width > 0 && bs.height > 0) {
                double dw = (double)bs.width / (double)lw;
                double dh = (double)bs.height / (double)lh;
                double d = dw > dh ? dw : dh;
                if (d > 0.0) {
                    return d;
                }
            }
        }
        // 2) 次选:window backingScaleFactor(避免 convert 舍入误差)
        NSWindow* nsw = [v window];
        if (nsw != nil) {
            double d = [nsw backingScaleFactor];
            if (d > 0.0) {
                return d;
            }
        }
    }
    return cxDprFromScreen();
}

double cxDevicePixelRatio()
{
    return cxDprFromScreen();
}

#endif // CX_OS_MAC
