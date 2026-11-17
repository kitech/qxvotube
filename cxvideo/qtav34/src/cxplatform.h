#ifndef CXPLATFORM_H
#define CXPLATFORM_H

// 平台宏收敛(Qt3 没有 Q_OS_*,统一在这里判断)
#if defined(_WIN32) || defined(Q_OS_WIN) || defined(Q_WS_WIN)
#  define CX_OS_WIN 1
#endif
#if defined(__APPLE__) || defined(Q_OS_MACX) || defined(Q_WS_MAC)
#  define CX_OS_MAC 1
#endif
#if defined(__unix__) && !defined(CX_OS_MAC)
#  define CX_OS_LINUX 1
#endif

// X11 / Xv 渲染器可用性(需要 cxvideo.pri 的 DEFINES 配合)
#if defined(CX_OS_LINUX) && defined(WITH_CX_RENDERER_X11)
#  define CX_HAVE_X11 1
#endif
#if defined(CX_HAVE_X11) && defined(WITH_CX_RENDERER_XV)
#  define CX_HAVE_XV 1
#endif

#endif // CXPLATFORM_H
