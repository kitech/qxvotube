#include "renderer_x11.h"

#ifdef CX_HAVE_X11

#include <qapplication.h>
#include <qimage.h>
#include <qevent.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>

static int cxX11ThreadsInit = 0;

bool CXRendererX11::isAvailable()
{
    Display* d = XOpenDisplay(0);
    if (d == 0)
        return false;
    XCloseDisplay(d);
    return true;
}

CXRendererX11::CXRendererX11(QWidget* parent)
#ifdef QT3_BUILD
    : QWidget(parent, 0, WNoAutoErase), m_dpy(0), m_screen(0), m_gc(0), m_win(0),
#else
    : QWidget(parent), m_dpy(0), m_screen(0), m_gc(0), m_win(0),
#endif
      m_shmOk(false), m_bw(0), m_bh(0), m_cur(0), m_pending(0)
{
    m_xi[0] = 0;
    m_xi[1] = 0;
    ::memset(m_seg, 0, sizeof(m_seg));
    setMinimumSize(320, 180);
#ifdef QT3_BUILD
    setBackgroundMode(NoBackground);   /* Qt3:无 setAttribute;WNoAutoErase 由构造 flags 给 */
#else
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_NativeWindow, true);
#endif
    setTargetSize(size());
    ensureX();
}

CXRendererX11::~CXRendererX11()
{
    destroyBuffer();
    if (m_gc != 0)
        XFreeGC(m_dpy, m_gc);
    if (m_dpy != 0)
        XCloseDisplay(m_dpy);
}

bool CXRendererX11::ensureX()
{
    if (m_dpy != 0)
        return true;
    if (!cxX11ThreadsInit) {
        XInitThreads();     // 渲染线程不直接调 X,但允许宿主线程交叉
        cxX11ThreadsInit = 1;
    }
    m_dpy = XOpenDisplay(0);
    if (m_dpy == 0)
        return false;
    m_screen = DefaultScreen(m_dpy);
    m_gc = XCreateGC(m_dpy, DefaultRootWindow(m_dpy), 0, 0);
    m_shmOk = XShmQueryExtension(m_dpy) ? true : false;
    return m_gc != 0;
}

void CXRendererX11::destroyBuffer()
{
    for (int i = 0; i < 2; ++i) {
        if (m_xi[i] != 0) {
            if (m_seg[i].shmaddr != 0) {
                XShmDetach(m_dpy, &m_seg[i]);
                shmdt(m_seg[i].shmaddr);
                m_seg[i].shmaddr = 0;
                if (m_seg[i].shmid >= 0)
                    shmctl(m_seg[i].shmid, IPC_RMID, 0);
            }
            // SHM 数据段已 detach;普通模式的数据由 XDestroyImage 释放
            if (m_seg[i].shmid < 0 || m_seg[i].shmaddr == 0)
                m_xi[i]->data = 0;
            XDestroyImage(m_xi[i]);
            m_xi[i] = 0;
        }
        m_seg[i].shmid = -1;
    }
    m_bw = 0;
    m_bh = 0;
    m_pending = 0;
}

bool CXRendererX11::shmAlloc(int idx, int w, int h)
{
    Visual* vis = DefaultVisual(m_dpy, m_screen);
    int depth = DefaultDepth(m_dpy, m_screen);
    m_xi[idx] = XShmCreateImage(m_dpy, vis, depth, ZPixmap, 0, &m_seg[idx], w, h);
    if (m_xi[idx] == 0)
        return false;
    m_seg[idx].shmid = shmget(IPC_PRIVATE,
                              m_xi[idx]->bytes_per_line * h,
                              IPC_CREAT | 0600);
    if (m_seg[idx].shmid < 0) {
        XDestroyImage(m_xi[idx]);
        m_xi[idx] = 0;
        return false;
    }
    m_seg[idx].shmaddr = (char*)shmat(m_seg[idx].shmid, 0, 0);
    m_seg[idx].readOnly = False;
    m_xi[idx]->data = m_seg[idx].shmaddr;
    if (!XShmAttach(m_dpy, &m_seg[idx])) {
        shmdt(m_seg[idx].shmaddr);
        m_seg[idx].shmaddr = 0;
        m_xi[idx]->data = 0;
        XDestroyImage(m_xi[idx]);
        m_xi[idx] = 0;
        return false;
    }
    return true;
}

bool CXRendererX11::ensureBuffer(int w, int h)
{
    if (w <= 0 || h <= 0)
        return false;
    if (m_bw == w && m_bh == h && m_xi[0] != 0 && m_xi[1] != 0)
        return true;
    destroyBuffer();

    Visual* vis = DefaultVisual(m_dpy, m_screen);
    int depth = DefaultDepth(m_dpy, m_screen);
    bool ok = true;
    if (m_shmOk) {
        ok = shmAlloc(0, w, h) && shmAlloc(1, w, h);
        if (!ok) {
            // SHM 失败 → 整组回退普通 XImage(保持双缓冲语义一致)
            destroyBuffer();
            m_shmOk = false;
        }
    }
    if (m_xi[0] == 0 || m_xi[1] == 0) {
        for (int i = 0; i < 2; ++i) {
            if (m_xi[i] != 0)
                continue;
            char* buf = (char*)::calloc(1, (size_t)w * 4 * h);
            m_xi[i] = XCreateImage(m_dpy, vis, depth, ZPixmap, 0, buf,
                                    w, h, 32, 0);
            if (m_xi[i] == 0) {
                ok = false;
                break;
            }
        }
    }
    if (!ok || m_xi[0] == 0 || m_xi[1] == 0)
        return false;
    m_bw = w;
    m_bh = h;
    m_cur = 0;
    m_pending = 0;
    return true;
}

void CXRendererX11::drainShm()
{
    while (m_dpy != 0 && XPending(m_dpy) > 0) {
        XEvent ev;
        XNextEvent(m_dpy, &ev);
        if (ev.type == Expose || ev.type == ConfigureNotify)
            continue;
        if (m_pending > 0)
            m_pending--;
    }
}

bool CXRendererX11::present(const QImage& img, const QRect& content)
{
    if (m_dpy == 0 && !ensureX())
        return false;
    if (m_win == 0)
        m_win = (Window)winId();
    QSize ws = size();
    if (ws.width() <= 0 || ws.height() <= 0)
        return true;                    // 还没布局,下一帧再来
    if (!ensureBuffer(ws.width(), ws.height()) || m_win == 0)
        return false;

    QImage src = img;
    src = cxToRgb32(src);
    if (src.width() != content.width() || src.height() != content.height())
        src = cxScaleRgb(src, content.width(), content.height());

    drainShm();
    if (m_pending >= 2) {
        XSync(m_dpy, False);
        m_pending = 0;
    }
    int idx = m_cur;
    m_cur ^= 1;
    XImage* xi = m_xi[idx];
    ::memset(xi->data, 0, (size_t)xi->bytes_per_line * ws.height());
    for (int y = 0; y < src.height(); ++y) {
        char* dst = xi->data + (y + content.y()) * xi->bytes_per_line
                    + content.x() * 4;
        ::memcpy(dst, src.scanLine(y), (size_t)src.width() * 4);
    }
    if (m_shmOk) {
        XShmPutImage(m_dpy, m_win, m_gc, xi, 0, 0, 0, 0,
                     ws.width(), ws.height(), True);
        m_pending++;
    } else {
        XPutImage(m_dpy, m_win, m_gc, xi, 0, 0, 0, 0,
                  ws.width(), ws.height());
    }
    XFlush(m_dpy);
    return true;
}

void CXRendererX11::paintEvent(QPaintEvent*)
{
    if (m_img.isNull())
        return;
    present(m_img, letterbox(size(), m_img.size()));
}

void CXRendererX11::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    setTargetSize(e->size());
    destroyBuffer();
    if (!m_img.isNull())
        present(m_img, letterbox(e->size(), m_img.size()));
}

#endif // CX_HAVE_X11
