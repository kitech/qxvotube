#ifndef MUSICSTYLE_H
#define MUSICSTYLE_H

#include "StyleParams.h"
#include "ThemeManager.h"

#include <qpainter.h>
#include <qrect.h>

#ifdef QT3_BUILD
template<class T> inline const T& qMax(const T& a, const T& b) { return (a < b) ? b : a; }
template<class T> inline const T& qMin(const T& a, const T& b) { return (a < b) ? a : b; }
template<class T> inline T qBound(const T& lo, const T& v, const T& hi)
{ return qMax(lo, qMin(hi, v)); }
#endif

inline void muiDrawRoundRect(QPainter& p, const QRect& r, int rx, int ry)
{
#ifdef QT3_BUILD
    p.drawRoundRect(r, rx, ry);
#else
    p.drawRoundedRect(r, rx, ry);
#endif
}

inline const StyleParams::Palette& muiPalette()
{
    static StyleParams::Palette s_fallback;
    if (g_activeParams)
        return ThemeManager::isDarkMode() ? g_activeParams->dark : g_activeParams->light;
    return s_fallback;
}

#endif // MUSICSTYLE_H
