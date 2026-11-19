#include "renderer_metal.h"

#if defined(CX_OS_MAC) && defined(WITH_CX_RENDERER_METAL)

#include "cxdpr.h"
#include <qimage.h>
#include <qevent.h>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <AppKit/NSView.h>
#import <AppKit/NSWindow.h>

// 说明(MRC,未开 ARC):
//   MTLCreateSystemDefaultDevice/newCommandQueue 为进程级对象,
//   随 widget 销毁也不释放(泄漏一次,换实现简单与线程安全)。
//   CAMetalLayer 由 NSView 持有,不额外 release。
bool CXRendererMetal::isAvailable()
{
    id<MTLDevice> d = MTLCreateSystemDefaultDevice();
    return d != nil;
}

CXRendererMetal::CXRendererMetal(QWidget* parent)
    : QWidget(parent), m_dev(0), m_queue(0), m_layer(0), m_view(0)
{
    setMinimumSize(320, 180);
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_NativeWindow, true);
    setTargetSize(size());
    // 延迟到首帧 present():此时 widget 已 reparent/show,NSView/window 有效
    // (SDL 后端已验证此做法;构造期取 winId 会得到空 window)。
}

CXRendererMetal::~CXRendererMetal()
{
    freeMetal();
}

void CXRendererMetal::freeMetal()
{
    // device/queue/layer 均为进程级或由 view 持有,不在此释放
    m_layer = 0;
    m_view = 0;
    m_dev = 0;
    m_queue = 0;
}

bool CXRendererMetal::initMetal()
{
    if (m_layer != 0)
        return true;
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    if (dev == nil)
        return false;
    m_dev = dev;
    m_queue = [dev newCommandQueue];
    if (m_queue == 0)
        return false;
    NSView* v = (NSView*)winId();
    if (v == nil)
        return false;
    m_view = v;
    NSWindow* nsw = [v window];
    [v setWantsLayer:YES];
    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.device = dev;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = NO;                 // 允许 blit 写入 drawable
    layer.contentsScale = nsw != nil ? [nsw backingScaleFactor] : cxDevicePixelRatio();
    // 默认 kCAGravityResize 会二次缩放;显式 TopLeft,物理像素 1:1 呈现
    layer.contentsGravity = kCAGravityTopLeft;
    layer.frame = [v bounds];
    // Apple 规范:drawableSize 用 convertSizeToBacking,勿手乘 scale
    layer.drawableSize =
        [v convertSizeToBacking:CGSizeMake((CGFloat)width(), (CGFloat)height())];
    [v setLayer:layer];
    m_layer = layer;
    return true;
}

bool CXRendererMetal::present(const QImage& img, const QRect& content)
{
    if (m_layer == 0 && !initMetal())
        return false;
    CAMetalLayer* layer = (CAMetalLayer*)m_layer;
    double dpr = cxDevicePixelRatio(this);
    if (dpr <= 0.0) {
        dpr = 1.0;
    }

    QImage src = img;
    src = cxToRgb32(src);
    // 统一到物理像素:src 与 blit 目标同坐标系,消除"只占 drawable 一角"
    int cw = (int)(content.width() * dpr + 0.5);
    int ch = (int)(content.height() * dpr + 0.5);
    if (cw < 1) {
        cw = 1;
    }
    if (ch < 1) {
        ch = 1;
    }
    if (src.width() != cw || src.height() != ch) {
        src = cxScaleRgb(src, cw, ch);
    }
    int sw = src.width();
    int sh = src.height();
    if (sw <= 0 || sh <= 0)
        return false;

    MTLTextureDescriptor* td =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:
            MTLPixelFormatBGRA8Unorm width:(NSUInteger)sw
            height:(NSUInteger)sh mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> tex = [(id<MTLDevice>)m_dev newTextureWithDescriptor:td];
    if (tex == nil)
        return false;
    [tex replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger)sw, (NSUInteger)sh)
           mipmapLevel:0
             withBytes:(const void*)src.bits()
           bytesPerRow:(NSUInteger)src.bytesPerLine()];

    id<CAMetalDrawable> dr = [layer nextDrawable];
    if (dr == nil) {
        [tex release];
        return false;
    }
    // 每帧读真实 drawable 尺寸(Apple/sokol 规范),不缓存逻辑宽高
    NSUInteger dtw = dr.texture.width;
    NSUInteger dth = dr.texture.height;
    if (dtw == 0 || dth == 0) {
        [tex release];
        return false;
    }
    int dx = (int)(content.x() * dpr + 0.5);
    int dy = (int)(content.y() * dpr + 0.5);
    if (dx < 0) {
        dx = 0;
    }
    if (dy < 0) {
        dy = 0;
    }
    if (dx + sw > (int)dtw) {
        sw = (int)dtw - dx;
    }
    if (dy + sh > (int)dth) {
        sh = (int)dth - dy;
    }
    if (sw <= 0 || sh <= 0) {
        [tex release];
        return false;
    }
    id<MTLCommandBuffer> cb = [(id<MTLCommandQueue>)m_queue commandBuffer];
    id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
    [bl copyFromTexture:tex
            sourceSlice:0
            sourceLevel:0
           sourceOrigin:MTLOriginMake(0, 0, 0)
             sourceSize:MTLSizeMake((NSUInteger)sw, (NSUInteger)sh, 1)
              toTexture:dr.texture
       destinationSlice:0
       destinationLevel:0
      destinationOrigin:MTLOriginMake((NSUInteger)dx, (NSUInteger)dy, 0)];
    [bl endEncoding];
    [cb presentDrawable:dr];
    [cb commit];
    [tex release];
    return true;
}

void CXRendererMetal::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    setTargetSize(e->size());
    if (m_layer != 0) {
        CAMetalLayer* l = (CAMetalLayer*)m_layer;
        NSView* v = (NSView*)m_view;
        NSWindow* nsw = v != nil ? [v window] : nil;
        if (nsw != nil) {
            l.contentsScale = [nsw backingScaleFactor];
        }
        CGSize bs = v != nil
            ? [v convertSizeToBacking:
                CGSizeMake((CGFloat)e->size().width(), (CGFloat)e->size().height())]
            : CGSizeMake((CGFloat)e->size().width(), (CGFloat)e->size().height());
        l.drawableSize = bs;
        // 诊断:仅尺寸变化时打,便于日后核对 dpr 是否生效
        qWarning("[dpr] metal logical=%dx%d physical=%dx%d dpr=%.2f",
                 e->size().width(), e->size().height(),
                 (int)bs.width, (int)bs.height, cxDevicePixelRatio(this));
    }
    if (!m_img.isNull())
        present(m_img, letterbox(e->size(), m_img.size()));
}

#endif
