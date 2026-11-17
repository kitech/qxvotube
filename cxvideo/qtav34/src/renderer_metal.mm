#include "renderer_metal.h"

#if defined(CX_OS_MAC) && defined(WITH_CX_RENDERER_METAL)

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
    initMetal();
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
    layer.contentsScale = nsw != nil ? [nsw backingScaleFactor] : 1.0;
    layer.drawableSize = CGSizeMake(width() * layer.contentsScale,
                                    height() * layer.contentsScale);
    [v setLayer:layer];
    m_layer = layer;
    return true;
}

bool CXRendererMetal::present(const QImage& img, const QRect& content)
{
    if (m_layer == 0 && !initMetal())
        return false;
    CAMetalLayer* layer = (CAMetalLayer*)m_layer;
    double scale = layer.contentsScale > 0 ? layer.contentsScale : 1.0;

    QImage src = img;
    src = cxToRgb32(src);
    if (src.width() != content.width() || src.height() != content.height())
        src = cxScaleRgb(src, content.width(), content.height());
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
      destinationOrigin:MTLOriginMake((NSUInteger)(content.x() * scale),
                                      (NSUInteger)(content.y() * scale), 0)];
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
        if (nsw != nil)
            l.contentsScale = [nsw backingScaleFactor];
        l.drawableSize = CGSizeMake(e->size().width() * l.contentsScale,
                                    e->size().height() * l.contentsScale);
    }
    if (!m_img.isNull())
        present(m_img, letterbox(e->size(), m_img.size()));
}

#endif
