#include "renderer_xv.h"

#ifdef CX_HAVE_XV

#include <qimage.h>
#include <qevent.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "limelog.h"

extern "C" {
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

// YUYV / UYVY 的 fourcc(Xv 用 little-endian fourcc 作为 format id)
#define CX_FOURCC_YUY2 0x32595559
#define CX_FOURCC_UYVY 0x59565955

bool CXRendererXv::isAvailable()
{
    Display* d = XOpenDisplay(0);
    if (d == 0)
        return false;
    unsigned int n = 0;
    XvAdaptorInfo* ai = 0;
    bool ok = false;
    if (XvQueryAdaptors(d, DefaultRootWindow(d), &n, &ai) == Success) {
        for (unsigned int i = 0; i < n && !ok; ++i) {
            if ((ai[i].type & XvImageMask) == 0)
                continue;
            if (ai[i].num_ports > 0)
                ok = true;
        }
    }
    if (ai != 0)
        XvFreeAdaptorInfo(ai);
    XCloseDisplay(d);
    return ok;
}

void CXRendererXv::setErr(const char* why)
{
    m_err = QString::fromLatin1(why);
    ALOG_WARN("[cxvideo-xv]", why);
}

CXRendererXv::CXRendererXv(QWidget* parent)
#ifdef QT3_BUILD
    : QWidget(parent, 0, WNoAutoErase), m_dpy(0), m_screen(0), m_win(0), m_gc(0),
#else
    : QWidget(parent), m_dpy(0), m_screen(0), m_win(0), m_gc(0),
#endif
      m_port(0), m_formatId(0), m_xvImg(0), m_buf(0), m_vw(0), m_vh(0),
      m_sws(0), m_fmtCount(0), m_visualChecked(false)
{
    ::memset(m_fmtDepth, 0, sizeof(m_fmtDepth));
    ::memset(m_fmtVisual, 0, sizeof(m_fmtVisual));
    setMinimumSize(320, 180);
#ifdef QT3_BUILD
    setBackgroundMode(NoBackground);   /* Qt3:无 setAttribute */
#else
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_NativeWindow, true);
#endif
    setTargetSize(size());
    /* 这里只开 Display + 查 adaptor + XvGrabPort,不碰 winId():
     * 构造期取 winId() 会让 Qt3 子件提前变成顶层窗口(见 cxvideo-vo.md §6)。
     * 历史 bug:此处原本没有调用,导致 ready() 恒 false,xv 在真机上也被丢弃。 */
    initXv();
}

CXRendererXv::~CXRendererXv()
{
    freeXv();
}

bool CXRendererXv::initXv()
{
    if (m_port != 0)
        return true;
    if (m_dpy == 0) {
        m_dpy = XOpenDisplay(0);
        if (m_dpy == 0) {
            setErr("no-display");
            return false;
        }
        m_screen = DefaultScreen(m_dpy);
        m_gc = XCreateGC(m_dpy, DefaultRootWindow(m_dpy), 0, 0);
    }
    m_err = QString();

    unsigned int n = 0;
    XvAdaptorInfo* ai = 0;
    if (XvQueryAdaptors(m_dpy, DefaultRootWindow(m_dpy), &n, &ai) != Success
        || n == 0) {
        if (ai != 0)
            XvFreeAdaptorInfo(ai);
        setErr("no-adaptor");
        return false;
    }

    bool grabbed = false;
    for (unsigned int i = 0; i < n && !grabbed; ++i) {
        if ((ai[i].type & XvImageMask) == 0)
            continue;
        // 记录该 adaptor 支持的 depth/visual(诊断 visual-mismatch 用)
        m_fmtCount = 0;
        for (unsigned long v = 0;
             v < ai[i].num_formats && m_fmtCount < CX_XV_MAX_FMT; ++v) {
            m_fmtDepth[m_fmtCount] = ai[i].formats[v].depth;
            m_fmtVisual[m_fmtCount] = ai[i].formats[v].visual_id;
            m_fmtCount++;
        }
        bool haveYuv = false;
        for (unsigned int p = 0; p < ai[i].num_ports && !grabbed; ++p) {
            XvPortID port = ai[i].base_id + p;
            int cnt = 0;
            XvImageFormatValues* fv = XvListImageFormats(m_dpy, port, &cnt);
            int want = 0;
            for (int k = 0; fv != 0 && k < cnt; ++k) {
                if ((int)fv[k].id == CX_FOURCC_YUY2) {
                    want = (int)fv[k].id;
                    break;
                }
            }
            for (int k = 0; fv != 0 && want == 0 && k < cnt; ++k) {
                if ((int)fv[k].id == CX_FOURCC_UYVY
                    || fv[k].format == XvYUV)
                    want = (int)fv[k].id;
            }
            if (fv != 0)
                XFree(fv);
            if (want == 0)
                continue;
            haveYuv = true;
            if (XvGrabPort(m_dpy, port, CurrentTime) == Success) {
                m_port = port;
                m_formatId = want;
                grabbed = true;
                ALOG_INFO("[cxvideo-xv] adaptor=", ai[i].name,
                          " port=", (unsigned long)port,
                          " base=", (unsigned long)ai[i].base_id,
                          " fmt=0x", QString::number((unsigned)m_formatId, 16),
                          " ports=", (unsigned long)ai[i].num_ports);
            }
        }
        if (!grabbed && haveYuv)
            setErr("port-busy");
    }
    XvFreeAdaptorInfo(ai);
    if (!grabbed && m_err.isEmpty())
        setErr("no-yuv-format");
    return grabbed;
}

void CXRendererXv::freeXv()
{
    if (m_buf != 0) {
        ::free(m_buf);
        m_buf = 0;
    }
    if (m_xvImg != 0) {
        m_xvImg->data = 0;
        XFree(m_xvImg);
        m_xvImg = 0;
    }
    if (m_sws != 0) {
        sws_freeContext((SwsContext*)m_sws);
        m_sws = 0;
    }
    if (m_dpy == 0)
        return;
    if (m_port != 0) {
        XvUngrabPort(m_dpy, m_port, CurrentTime);
        m_port = 0;
    }
    if (m_gc != 0) {
        XFreeGC(m_dpy, m_gc);
        m_gc = 0;
    }
    XCloseDisplay(m_dpy);
    m_dpy = 0;
    m_vw = 0;
    m_vh = 0;
}

bool CXRendererXv::ensureConvert(int w, int h)
{
    if (w <= 0 || h <= 0)
        return false;
    if (m_xvImg != 0 && m_vw == w && m_vh == h)
        return m_sws != 0;
    if (m_xvImg != 0) {
        m_xvImg->data = 0;
        XFree(m_xvImg);
        m_xvImg = 0;
    }
    if (m_buf != 0) {
        ::free(m_buf);
        m_buf = 0;
    }
    m_xvImg = XvCreateImage(m_dpy, m_port, m_formatId, 0, w, h);
    if (m_xvImg == 0)
        return false;
    m_buf = (char*)::calloc(1, (size_t)m_xvImg->data_size);
    if (m_buf == 0) {
        XFree(m_xvImg);
        m_xvImg = 0;
        return false;
    }
    m_xvImg->data = m_buf;
    m_vw = w;
    m_vh = h;
    m_sws = sws_getCachedContext((SwsContext*)m_sws,
                                 w, h, AV_PIX_FMT_RGB32,
                                 w, h, AV_PIX_FMT_YUYV422,
                                 SWS_BILINEAR, 0, 0, 0);
    return m_sws != 0;
}

// 首帧核对窗口 visual:adaptor 只支持特定 depth/visual(例:Intel :0 是
// depth24/visualID 0x20)。不匹配时 XvPutImage 会抛 BadMatch(异步,不易定位),
// 所以提前打一条可读日志;不强行失败,避免误杀其它可用组合。
void CXRendererXv::checkWindowVisual()
{
    if (m_win == 0 || m_dpy == 0)
        return;
    XWindowAttributes wa;
    if (!XGetWindowAttributes(m_dpy, m_win, &wa))
        return;
    unsigned long vid = XVisualIDFromVisual(wa.visual);
    bool match = false;
    for (int i = 0; i < m_fmtCount; ++i) {
        if (m_fmtDepth[i] == (unsigned long)wa.depth
            && (m_fmtVisual[i] == 0 || m_fmtVisual[i] == vid)) {
            match = true;
            break;
        }
    }
    if (!match) {
        m_err = QString::fromLatin1("visual-mismatch");
        ALOG_WARN("[cxvideo-xv] WARN window depth=", wa.depth,
                  " visual=0x", QString::number((unsigned long)vid, 16),
                  " 不在 adaptor支持列表内(XvPutImage 可能 BadMatch,建议改用 x11/gl)");
    } else {
        ALOG_INFO("[cxvideo-xv] window depth=", wa.depth,
                  " visual=0x", QString::number((unsigned long)vid, 16),
                  " compatible");
    }
}

bool CXRendererXv::present(const QImage& img, const QRect& content)
{
    if (m_port == 0 && !initXv())      // 端口曾被占用等:每帧重试一次(廉价)
        return false;
    if (m_win == 0)
        m_win = (Window)winId();
    if (m_port == 0 || m_win == 0)
        return false;
    if (!m_visualChecked) {
        m_visualChecked = true;
        checkWindowVisual();
    }
    if (!ensureConvert(img.width(), img.height()))
        return false;

    QImage src = img;
    if (src.depth() != 32)
        src = cxToRgb32(src);
    const uint8_t* planes[4];
    int strides[4];
    planes[0] = (const uint8_t*)src.bits();
    planes[1] = planes[2] = planes[3] = 0;
    strides[0] = src.bytesPerLine();
    strides[1] = strides[2] = strides[3] = 0;
    uint8_t* dstData[4];
    int dstStride[4];
    dstData[0] = (uint8_t*)m_xvImg->data;
    dstData[1] = dstData[2] = dstData[3] = 0;
    dstStride[0] = m_xvImg->pitches[0];
    dstStride[1] = dstStride[2] = dstStride[3] = 0;
    sws_scale((SwsContext*)m_sws, planes, strides, 0, src.height(),
              dstData, dstStride);
    XvPutImage(m_dpy, m_port, m_win, m_gc, m_xvImg,
               0, 0, m_xvImg->width, m_xvImg->height,
               content.x(), content.y(),
               content.width(), content.height());
    XFlush(m_dpy);
    return true;
}

void CXRendererXv::paintEvent(QPaintEvent*)
{
    if (m_img.isNull())
        return;
    present(m_img, letterbox(size(), m_img.size()));
}

void CXRendererXv::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    setTargetSize(e->size());
    if (!m_img.isNull())
        present(m_img, letterbox(e->size(), m_img.size()));
}

#endif // CX_HAVE_XV
