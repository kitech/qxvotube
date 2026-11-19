#include "playerbar.h"
#include "musicstyle.h"
#include "translator.h"
#include "identicon.h"

#include <qglobal.h>
#include <stdio.h>
#include <stdlib.h>
#include <qpainter.h>
#include <qevent.h>
#include <qfontmetrics.h>
#include <qframe.h>
#include <qcheckbox.h>
#include <qtimer.h>
#include <qapplication.h>
#ifdef QT3_BUILD
#include <qpointarray.h>
typedef QPointArray MuiPolygon;
#else
typedef QPolygon MuiPolygon;
#endif

namespace {
QString fmtMs(int ms)
{
    int total = ms / 1000;
    int m = total / 60;
    int s = total % 60;
    QString ss = QString::number(s);
    if (s < 10) ss = QString("0") + ss;
    return QString::number(m) + ":" + ss;
}

/* 音量弹层:自绘滑条 + 静音勾选(qlmusic 同款,Qt3/Qt4 双分支) */
class VolumePanelSlider : public QWidget {
public:
    VolumePanelSlider(PlayerBar* bar, QWidget* parent)
        : QWidget(parent), m_bar(bar), m_v(100), m_drag(false), m_hover(false)
    {
        setFixedSize(126, 26);
        setMouseTracking(true);
    }
    void setValue(int v) { if (v != m_v) { m_v = v; update(); } }
    int getValue() const { return m_v; }
    void endDrag() { if (m_drag) { m_drag = false; update(); } }

protected:
    void paintEvent(QPaintEvent*)
    {
        const StyleParams::Palette& pal = muiPalette();
        QPainter p(this);
        QRect tr(0, 10, width(), 6);
        p.setPen(Qt::NoPen);
        p.setBrush(pal.border);
        muiDrawRoundRect(p, tr, 3, 3);
        int vw = tr.width() * m_v / 100;
        if (vw > 0) {
            p.setBrush(pal.accent);
            muiDrawRoundRect(p, QRect(tr.x(), tr.y(), vw, tr.height()), 3, 3);
        }
        int hx = tr.x() + vw - 6;
        QColor hc = (m_drag || m_hover) ? pal.accent : pal.textPrimary;
        p.setPen(QPen(pal.border, 1));
        p.setBrush(hc);
        p.drawEllipse(hx, tr.y() + tr.height() / 2 - 6, 12, 12);
    }
    void mousePressEvent(QMouseEvent* e)
    {
        if (e->button() != Qt::LeftButton) { e->ignore(); return; }
        m_drag = true;
        jumpTo(e->pos().x());
    }
    void mouseMoveEvent(QMouseEvent* e) { if (m_drag) jumpTo(e->pos().x()); }
    void mouseReleaseEvent(QMouseEvent*) { if (m_drag) { m_drag = false; update(); } }
    void enterEvent(QEvent*) { m_hover = true; update(); }
    void leaveEvent(QEvent*) { m_hover = false; update(); }

private:
    void jumpTo(int x)
    {
        int v = qBound(0, x, width() - 1) * 100 / (width() - 1);
        if (v < 2) v = 2;            /* click-jump 不落 0(静音走勾选) */
        if (v == m_v) return;
        m_v = v;
        update();
        m_bar->onPopupVolume(v);
    }
    PlayerBar* m_bar;
    int m_v;
    bool m_drag;
    bool m_hover;
};

class VolumePopupFrame : public QFrame {
public:
    VolumePopupFrame(PlayerBar* bar)
#ifdef QT3_BUILD
        : QFrame(bar, 0, Qt::WType_Popup)
#else
        : QFrame(bar, Qt::Popup)
#endif
        , m_bar(bar), m_panel(0)
    {
        setFrameStyle(QFrame::WinPanel | QFrame::Raised);
    }
    void setPanel(VolumePanelSlider* s) { m_panel = s; }

protected:
    void closeEvent(QCloseEvent* e)
    {
        e->accept();
        if (m_panel) m_panel->endDrag();
        QMouseEvent me(QEvent::MouseButtonRelease, QPoint(0, 0), QPoint(0, 0),
#ifdef QT3_BUILD
                       QMouseEvent::LeftButton, QMouseEvent::NoButton);
#else
                       Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
#endif
        QApplication::sendEvent(m_bar, &me);
    }

private:
    PlayerBar* m_bar;
    VolumePanelSlider* m_panel;
};

void drawTriangle(QPainter& p, const QRect& r, bool left, const QColor& c)
{
    MuiPolygon tri(3);
    if (left) {
        tri.setPoint(0, r.right(), r.top());
        tri.setPoint(1, r.right(), r.bottom());
        tri.setPoint(2, r.left(), r.center().y());
    } else {
        tri.setPoint(0, r.left(), r.top());
        tri.setPoint(1, r.left(), r.bottom());
        tri.setPoint(2, r.right(), r.center().y());
    }
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawPolygon(tri);
}
}

PlayerBar::PlayerBar(QWidget* parent)
    : QWidget(parent), m_hasTrack(false), m_playing(false),
      m_progressMs(0), m_durationMs(0), m_hover(HitNone), m_dragging(false),
      m_volume(100), m_lastVolume(100), m_rate(1.0), m_muted(false),
      m_repeatMode(0), m_shuffle(false), m_autoSkip(true),
      m_dlPct(0), m_dlVisible(false), m_bufferedMs(0),
      m_dlHideTimer(0), m_volumePopup(0), m_volumeSlider(0), m_muteCheck(0),
      m_subtitleLine(qFromUtf8("无字幕资源"))
{
    setMouseTracking(true);
    setFixedHeight(80);
    retranslateUi();
}

void PlayerBar::retranslateUi() { update(); }

void PlayerBar::setTrack(const Track& t)
{
    m_track = t;
    m_hasTrack = true;
    m_progressMs = 0;
    m_durationMs = t.durationMs;
    update();
}

void PlayerBar::setProgress(int ms)
{
    if (m_dragging) return;
    m_progressMs = ms;
    update();
}

void PlayerBar::setDuration(int ms)
{
    if (ms > 0) m_durationMs = ms;
    update();
}

void PlayerBar::setVolume(int pct)
{
    m_volume = qBound(0, pct, 100);
    if (m_volume > 0) m_lastVolume = m_volume;
    update();
}

void PlayerBar::setRate(double r)
{
    m_rate = r > 0.0 ? r : 1.0;
    update();
}

void PlayerBar::setMuted(bool on)
{
    m_muted = on;
    update();
}

void PlayerBar::setPlaying(bool on)
{
    if (m_playing == on) return;
    m_playing = on;
    emit playStateChanged(on);
    update();
}

QRect PlayerBar::playRect() const
{
    int cx = width() / 2;                 /* qlmusic 中央群:prev|play|next|循环|随机|喇叭|下载 */
    return QRect(cx - 75, 14, 30, 30);
}

QRect PlayerBar::prevRect() const
{
    int cx = width() / 2;
    return QRect(cx - 120, 19, 20, 20);
}

QRect PlayerBar::nextRect() const
{
    int cx = width() / 2;
    return QRect(cx - 20, 19, 20, 20);
}

QRect PlayerBar::progressRect() const
{
    int cx = width() / 2;
    return QRect(cx - 130, 58, 260, 6);
}

/* 喇叭(弹层):next 右侧第三位(qlmusic 口径) */
QRect PlayerBar::volRect() const   { int cx = width() / 2; return QRect(cx + 71, 19, 20, 20); }
QRect PlayerBar::rateRect() const  { return QRect(width() - 172, 22, 40, 20); }
QRect PlayerBar::ccRect() const    { return QRect(width() - 126, 22, 30, 20); }
QRect PlayerBar::eqRect() const    { return QRect(width() - 90, 22, 34, 20); }
QRect PlayerBar::shotRect() const  { return QRect(width() - 52, 22, 34, 20); }
QRect PlayerBar::repeatRect() const  { int cx = width() / 2; return QRect(cx + 15, 19, 20, 20); }
QRect PlayerBar::shuffleRect() const { int cx = width() / 2; return QRect(cx + 43, 19, 20, 20); }
QRect PlayerBar::downloadRect() const{ int cx = width() / 2; return QRect(cx + 99, 19, 20, 20); }
QRect PlayerBar::bufferedRect() const{ int cx = width() / 2; return QRect(cx + 121, 19, 20, 20); }
QRect PlayerBar::autoSkipRect() const{ int cx = width() / 2; return QRect(cx - 155, 19, 20, 20); }
/* 单行字幕:右侧 EQ 按钮下方一行(夹在按钮行 y42 与进度条 y58 之间),
 * 右对齐到 width-14;不再占底部整行。绘制/命中均走此矩形。 */
QRect PlayerBar::subtitleLineRect() const { return QRect(width() - 174, 44, 160, 13); }

void PlayerBar::setSubtitleLine(const QString& text)
{
    /* 空文本归一为占位提示:字幕行始终可见,点击仍可打开列表 */
    const QString s = text.isEmpty() ? qFromUtf8("无字幕资源") : text;
    if (m_subtitleLine == s) return;
    m_subtitleLine = s;
    update();
}

/* 下载圈 tooltip:给出可读的进度/状态文字,与饼图视觉互补 */
static QString pbDownloadTip(bool visible, int pct)
{
    if (!visible)
        return qFromUtf8("点击下载");
    if (pct == 100)
        return qFromUtf8("下载完成 100%");
    if (pct == -2)
        return qFromUtf8("下载失败,点击重试");
    if (pct < 0)
        return qFromUtf8("下载中…(进度未知)");
    return qFromUtf8("下载中 ") + QString::number(pct) + qFromUtf8("%");
}

/* 缓冲圈 tooltip:显示后端网络缓冲到的时间点与百分比 */
static QString pbBufferedTip(long bufferedMs, int durationMs)
{
    if (bufferedMs <= 0 || durationMs <= 0)
        return qFromUtf8("缓冲进度:未知");
    const long v = qMin(bufferedMs, (long)durationMs);
    const int pct = qBound(0, (int)(v * 100 / durationMs), 100);
    return qFromUtf8("缓冲 ") + QString::number(pct) + qFromUtf8("% · 已缓存到 ")
           + fmtMs((int)v);
}

/* 单行截断(Qt3 无 QFontMetrics::elidedText,按宽度逐字裁) */
static QString pbElideOneLine(QPainter& p, const QString& text, int maxW)
{
    if (text.isEmpty()) return text;
    const QFontMetrics fm = p.fontMetrics();
    if (qFontWidth(fm, text) <= maxW) return text;
    const QString dots = qFromUtf8("…");
    QString t = text;
    while (!t.isEmpty() && qFontWidth(fm, t + dots) > maxW)
        t.remove(t.length() - 1, 1);
    return t + dots;
}

void PlayerBar::setProgressFromX(int x)
{
    QRect pr = progressRect();
    if (!m_hasTrack || pr.width() <= 0) return;
    int rel = qBound(0, x - pr.x(), pr.width());
    m_progressMs = (int)((double)rel / pr.width() * m_durationMs);
    update();
}

void PlayerBar::paintEvent(QPaintEvent*)
{
    const StyleParams::Palette& pal = muiPalette();
    QPainter p(this);
    p.fillRect(rect(), pal.surfaceBg);
    p.setPen(pal.border);
    p.drawLine(0, 0, width(), 0);

    QFont bold = font();
    bold.setBold(true);
    QFont small = font();
    small.setPointSize(qMax(7, small.pointSize() - 2));

    if (m_hasTrack) {
        QPixmap cover = generateIdenticon(m_track.coverSeed, 52);
        p.drawPixmap(14, 14, cover);
        p.setFont(bold);
        p.setPen(pal.textPrimary);
        p.drawText(QRect(78, 18, width() / 2 - 100, 22), Qt::AlignVCenter | Qt::AlignLeft, m_track.title);
        p.setFont(small);
        p.setPen(pal.textMuted);
        p.drawText(QRect(78, 42, width() / 2 - 100, 20), Qt::AlignVCenter | Qt::AlignLeft, m_track.artist);
    }

    QColor ctrl = (m_hover == HitPlay || m_hover == HitPrev || m_hover == HitNext)
                      ? pal.accent : pal.textPrimary;

    QRect pv = prevRect();
    p.setPen(Qt::NoPen);
    p.setBrush(ctrl);
    p.drawRect(QRect(pv.left(), pv.top(), 3, pv.height()));
    drawTriangle(p, QRect(pv.left() + 5, pv.top(), pv.width() - 5, pv.height()), true, ctrl);

    QRect pl = playRect();
    if (m_playing) {
        p.setBrush(ctrl);
        p.drawRect(QRect(pl.left() + 5, pl.top(), 7, pl.height()));
        p.drawRect(QRect(pl.right() - 12, pl.top(), 7, pl.height()));
    } else {
        drawTriangle(p, QRect(pl.left() + 4, pl.top(), pl.width() - 4, pl.height()), false, ctrl);
    }

    QRect nx = nextRect();
    drawTriangle(p, QRect(nx.left(), nx.top(), nx.width() - 5, nx.height()), false, ctrl);
    p.setBrush(ctrl);
    p.drawRect(QRect(nx.right() - 3, nx.top(), 3, nx.height()));

    QRect pr = progressRect();
    p.setPen(Qt::NoPen);
    p.setBrush(pal.border);
    muiDrawRoundRect(p, pr, 3, 3);
    /* 缓冲段(accent 与背景对半混色):下载常快于播放,浅色段领先亮色播放头 */
    if (m_hasTrack && m_durationMs > 0 && m_bufferedMs > 0) {
        int br = (int)((double)qMin(m_bufferedMs, (long)m_durationMs)
                       / m_durationMs * pr.width());
        br = qBound(0, br, pr.width());
        if (br > 0) {
            const QColor& a = pal.accent;
            const QColor& b = pal.surfaceBg;
            QColor bc((a.red() + b.red()) / 2, (a.green() + b.green()) / 2,
                      (a.blue() + b.blue()) / 2);
            p.setBrush(bc);
            muiDrawRoundRect(p, QRect(pr.x(), pr.y(), br, pr.height()), 3, 3);
        }
    }
    int ratio = m_hasTrack && m_durationMs > 0
                    ? (int)((double)m_progressMs / m_durationMs * pr.width()) : 0;
    ratio = qBound(0, ratio, pr.width());
    if (ratio > 0) {
        p.setBrush(pal.accent);
        muiDrawRoundRect(p, QRect(pr.x(), pr.y(), ratio, pr.height()), 3, 3);
    }

    p.setFont(small);
    p.setPen(pal.textMuted);
    p.drawText(QRect(pr.x() - 44, pr.y() - 8, 40, 20), Qt::AlignVCenter | Qt::AlignRight,
               fmtMs(m_progressMs));
    /* M5-A2:时长由引擎 durationChanged 权威(不再依赖 setTrack 的库值) */
    p.drawText(QRect(pr.right() + 4, pr.y() - 8, 44, 20), Qt::AlignVCenter | Qt::AlignLeft,
               fmtMs(m_durationMs));

    /* 喇叭(点击弹音量面板;静音/0 音量画斜杠;0 以上画声波) */
    {
        QRect vr = volRect();
        const QColor avCol = (m_hover == HitVol) ? pal.accent : pal.textMuted;
        p.save();
        p.setPen(QPen(avCol, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(vr.left() + 3, vr.top() + 9, 4, 6);
        p.drawLine(vr.left() + 7, vr.top() + 9, vr.left() + 11, vr.top() + 5);
        p.drawLine(vr.left() + 7, vr.top() + 14, vr.left() + 11, vr.top() + 18);
        if (!m_muted && m_volume > 0) {
            p.drawArc(QRect(vr.left() + 11, vr.top() + 8, 8, 8), -45 * 16, 100 * 16);
            if (m_volume >= 50)
                p.drawArc(QRect(vr.left() + 10, vr.top() + 5, 10, 14), -45 * 16, 100 * 16);
        } else {
            p.setPen(QPen(QColor(0xCC, 0x44, 0x44), 2));
            p.drawLine(vr.left() + 3, vr.top() + 18, vr.left() + 18, vr.top() + 4);
        }
        p.restore();
    }

    /* 下载指示器(常驻):空闲=下载箭头;进行中=扇形/转圈;失败=斜杠;完成=满圈 */
    {
        QRect dr = downloadRect();
        const QColor dlCol = (m_hover == HitDownload) ? pal.accent : pal.textMuted;
        p.save();
        p.setPen(QPen(dlCol, 1));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(dr.left() + 1, dr.top() + 1, dr.width() - 2, dr.height() - 2);
        if (!m_dlVisible) {
            /* 空闲:向下箭头(下载图标),常驻可见 */
            int mx = dr.center().x();
            p.setPen(QPen(dlCol, 2));
            p.drawLine(mx, dr.top() + 4, mx, dr.bottom() - 5);
            p.drawLine(mx - 4, dr.bottom() - 9, mx, dr.bottom() - 4);
            p.drawLine(mx + 4, dr.bottom() - 9, mx, dr.bottom() - 4);
        } else if (m_dlPct == -2) {
            p.setPen(QPen(QColor(0xCC, 0x44, 0x44), 2));
            p.drawLine(dr.left() + 5, dr.top() + 16, dr.left() + 16, dr.top() + 5);
        } else if (m_dlPct < 0) {
            p.setPen(QPen(pal.accent, 2));
            p.drawArc(dr.left() + 3, dr.top() + 3, dr.width() - 6,
                      dr.height() - 6, 45 * 16, -120 * 16);
        } else {
            p.setBrush(pal.accent);
            p.setPen(Qt::NoPen);
            p.drawPie(dr.left() + 2, dr.top() + 2, dr.width() - 4,
                      dr.height() - 4, 90 * 16, -m_dlPct * 3600 / 100);
            if (dr.right() + 34 <= rateRect().left() - 4) {
                p.setPen(pal.textMuted);
                p.drawText(QRect(dr.right() + 2, dr.top(), 30, dr.height()),
                           Qt::AlignVCenter, QString::number(m_dlPct)
                                             + qFromUtf8("%"));
            }
        }
        p.restore();
    }

    /* 缓冲进度圈(常驻):与下载圈区分——读播放后端网络缓冲前沿 */
    {
        QRect br = bufferedRect();
        const QColor rc = (m_hover == HitBuffered) ? pal.accent : pal.textMuted;
        int bp = (m_durationMs > 0 && m_bufferedMs > 0)
                 ? qBound(0, (int)(m_bufferedMs * 100 / m_durationMs), 100) : 0;
        p.save();
        p.setPen(QPen(rc, 1));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(br.left() + 1, br.top() + 1, br.width() - 2, br.height() - 2);
        if (bp > 0) {
            p.setBrush(pal.accent);
            p.setPen(Qt::NoPen);
            p.drawPie(br.left() + 2, br.top() + 2, br.width() - 4,
                      br.height() - 4, 90 * 16, -bp * 3600 / 100);
            if (br.right() + 34 <= rateRect().left() - 4) {
                p.setPen(pal.textMuted);
                p.drawText(QRect(br.right() + 2, br.top(), 30, br.height()),
                           Qt::AlignVCenter, QString::number(bp)
                                             + qFromUtf8("%"));
            }
        }
        p.restore();
    }

    /* 失败自动跳过:圆圈+勾(开=accent) */
    {
        QRect as = autoSkipRect();
        const QColor asCol = (m_hover == HitAutoSkip) ? pal.accent
                           : (m_autoSkip ? pal.accent : pal.textMuted);
        p.save();
        p.setPen(QPen(asCol, 1));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(as.left() + 1, as.top() + 1, as.width() - 2,
                      as.height() - 2);
        p.drawLine(as.left() + 5, as.top() + 10, as.left() + 8, as.top() + 14);
        p.drawLine(as.left() + 8, as.top() + 14, as.left() + 15, as.top() + 5);
        p.restore();
    }

    /* 循环模式:off=圆点 / all=双弧箭头 / one=圆+1 */
    {
        QRect rp = repeatRect();
        const QColor rc = (m_hover == HitRepeat) ? pal.accent
                        : (m_repeatMode ? pal.accent : pal.textMuted);
        p.save();
        p.setPen(QPen(rc, 1));
        p.setBrush(Qt::NoBrush);
        if (m_repeatMode == 1) {
            p.drawArc(rp.left() + 1, rp.top() + 1, rp.width() - 2, rp.height() - 2,
                      30 * 16, 150 * 16);
            p.drawArc(rp.left() + 1, rp.top() + 1, rp.width() - 2, rp.height() - 2,
                      210 * 16, 150 * 16);
            MuiPolygon a(3);
            a.setPoint(0, rp.right() - 5, rp.top() + 1);
            a.setPoint(1, rp.right() - 1, rp.top() + 3);
            a.setPoint(2, rp.right() - 3, rp.top() + 6);
            p.setBrush(rc);
            p.drawPolygon(a);
            MuiPolygon b(3);
            b.setPoint(0, rp.left() + 5, rp.bottom() - 1);
            b.setPoint(1, rp.left() + 1, rp.bottom() - 3);
            b.setPoint(2, rp.left() + 3, rp.bottom() - 6);
            p.drawPolygon(b);
        } else if (m_repeatMode == 2) {
            p.drawEllipse(rp.left() + 1, rp.top() + 1, rp.width() - 2,
                          rp.height() - 2);
            p.setPen(rc);
            p.drawText(rp, Qt::AlignCenter, qFromUtf8("1"));
        } else {
            p.drawEllipse(rp.left() + 1, rp.top() + 1, rp.width() - 2,
                          rp.height() - 2);
            p.setPen(rc);
            p.drawText(rp, Qt::AlignCenter, qFromUtf8("\u21bb"));
        }
        p.restore();
    }

    /* 随机:两道交叉箭头 */
    {
        QRect sr = shuffleRect();
        const QColor sc = (m_hover == HitShuffle) ? pal.accent
                        : (m_shuffle ? pal.accent : pal.textMuted);
        p.save();
        p.setPen(QPen(sc, 1));
        p.setBrush(Qt::NoBrush);
        p.drawLine(sr.left() + 2, sr.top() + 6, sr.right() - 5, sr.top() + 6);
        p.drawLine(sr.left() + 2, sr.bottom() - 6, sr.right() - 5, sr.bottom() - 6);
        p.drawLine(sr.left() + 5, sr.top() + 6, sr.left() + 10, sr.bottom() - 6);
        p.drawLine(sr.left() + 5, sr.bottom() - 6, sr.left() + 10, sr.top() + 6);
        MuiPolygon t(3);
        t.setPoint(0, sr.right() - 6, sr.top() + 3);
        t.setPoint(1, sr.right() - 1, sr.top() + 6);
        t.setPoint(2, sr.right() - 6, sr.top() + 9);
        p.setBrush(sc);
        p.drawPolygon(t);
        p.restore();
    }

    p.setFont(small);
    p.setPen(m_hover == HitRate ? pal.accent : pal.textMuted);
    p.drawText(rateRect(), Qt::AlignCenter,
               QString::number(m_rate, 'g', 3) + "x");
    p.setPen(m_hover == HitSub ? pal.accent : pal.textMuted);
    p.drawText(ccRect(), Qt::AlignCenter, "CC");
    p.setPen(m_hover == HitEq ? pal.accent : pal.textMuted);
    p.drawText(eqRect(), Qt::AlignCenter, "EQ");
    p.setPen(m_hover == HitShot ? pal.accent : pal.textMuted);
    p.drawText(shotRect(), Qt::AlignCenter, qFromUtf8("图"));

    /* 底部单行字幕:点击打开字幕列表 */
    if (!m_subtitleLine.isEmpty()) {
        const QRect sr = subtitleLineRect();
        p.setFont(small);
        p.setPen(m_hover == HitSubLine ? pal.accent : pal.textMuted);
        p.drawText(sr, Qt::AlignVCenter | Qt::AlignLeft,
                   pbElideOneLine(p, m_subtitleLine, sr.width()));
    }
}

void PlayerBar::mousePressEvent(QMouseEvent* e)
{
    if (getenv("QLVOTUBE_DEBUG") != 0)
        fprintf(stderr, "pbar w=%d click=%d,%d vol=%d,%d rate=%d,%d eq=%d,%d shot=%d,%d\n",
                width(), e->pos().x(), e->pos().y(),
                volRect().x(), volRect().y(), rateRect().x(), rateRect().y(),
                eqRect().x(), eqRect().y(), shotRect().x(), shotRect().y());
    if (!m_subtitleLine.isEmpty() && subtitleLineRect().contains(e->pos())) {
        emit subtitleListRequested();
    } else if (playRect().contains(e->pos())) {
        emit playPauseRequested();
    } else if (prevRect().contains(e->pos())) {
        emit prevRequested();
    } else if (nextRect().contains(e->pos())) {
        emit nextRequested();
    } else if (progressRect().contains(e->pos())) {
        m_dragging = true;
        setProgressFromX(e->pos().x());
    } else if (volRect().contains(e->pos())) {
        toggleVolumePopup();
    } else if (repeatRect().contains(e->pos())) {
        emit repeatClicked();
    } else if (shuffleRect().contains(e->pos())) {
        emit shuffleClicked();
    } else if (autoSkipRect().contains(e->pos())) {
        emit autoSkipClicked();
    } else if (rateRect().contains(e->pos())) {
        static const double kRates[5] = { 0.5, 1.0, 1.25, 1.5, 2.0 };
        int idx = 0;
        for (int i = 0; i < 5; i++)
            if (kRates[i] == m_rate) idx = i;
        m_rate = kRates[(idx + 1) % 5];
        update();
        emit rateChanged(m_rate);
    } else if (ccRect().contains(e->pos())) {
        emit subtitleRequested();
    } else if (eqRect().contains(e->pos())) {
        emit eqRequested();
    } else if (shotRect().contains(e->pos())) {
        emit shotRequested();
    }
}

void PlayerBar::mouseMoveEvent(QMouseEvent* e)
{
    if (m_dragging) {
        setProgressFromX(e->pos().x());
        return;
    }
    Hit h = HitNone;
    if (!m_subtitleLine.isEmpty() && subtitleLineRect().contains(e->pos())) h = HitSubLine;
    else if (playRect().contains(e->pos())) h = HitPlay;
    else if (prevRect().contains(e->pos())) h = HitPrev;
    else if (nextRect().contains(e->pos())) h = HitNext;
    else if (progressRect().contains(e->pos())) h = HitProgress;
    else if (volRect().contains(e->pos())) h = HitVol;
    else if (rateRect().contains(e->pos())) h = HitRate;
    else if (ccRect().contains(e->pos())) h = HitSub;
    else if (eqRect().contains(e->pos())) h = HitEq;
    else if (shotRect().contains(e->pos())) h = HitShot;
    else if (downloadRect().contains(e->pos())) h = HitDownload;
    else if (bufferedRect().contains(e->pos())) h = HitBuffered;
    else if (repeatRect().contains(e->pos())) h = HitRepeat;
    else if (shuffleRect().contains(e->pos())) h = HitShuffle;
    else if (autoSkipRect().contains(e->pos())) h = HitAutoSkip;
    if (h != m_hover) {
        m_hover = h;
        update();
    }
    /* 悬停下载/缓冲圈时显示进度 tooltip;移开即清除 */
    if (m_hover == HitDownload)
        showTempTooltip(this, downloadRect(), pbDownloadTip(m_dlVisible, m_dlPct));
    else if (m_hover == HitBuffered)
        showTempTooltip(this, bufferedRect(),
                        pbBufferedTip(m_bufferedMs, m_durationMs));
    else
        showTempTooltip(this, downloadRect(), QString());
}

void PlayerBar::mouseReleaseEvent(QMouseEvent*)
{
    if (m_dragging) {
        m_dragging = false;
        emit seekRequested(m_progressMs);
    }
    update();
}

void PlayerBar::leaveEvent(QEvent* e)
{
    QWidget::leaveEvent(e);
    if (m_hover != HitNone) {
        m_hover = HitNone;
        update();
    }
    showTempTooltip(this, downloadRect(), QString());
    showTempTooltip(this, bufferedRect(), QString());
}

// ── 播放模式 / 下载进度 / 音量弹层(2026-11-18,qlmusic 移植) ──

void PlayerBar::setRepeatMode(int mode)
{
    mode = qBound(0, mode, 2);
    if (m_repeatMode == mode) return;
    m_repeatMode = mode;
    update();
}

void PlayerBar::setAutoSkip(bool on)
{
    if (m_autoSkip == on) return;
    m_autoSkip = on;
    update();
}

void PlayerBar::setShuffle(bool on)
{
    if (m_shuffle == on) return;
    m_shuffle = on;
    update();
}

void PlayerBar::setDownloadState(int pct)
{
    if (pct == 100) {
        /* 完成:保持满圈常驻,不隐藏 */
        m_dlPct = 100;
        m_dlVisible = true;
        update();
    } else {
        if (pct < 0)
            m_dlPct = (pct == -2) ? -2 : -1;
        else
            m_dlPct = qBound(0, pct, 100);
        m_dlVisible = true;
        update();
    }
    /* 悬停下载圈时同步刷新 tooltip 文字 */
    if (m_hover == HitDownload)
        showTempTooltip(this, downloadRect(), pbDownloadTip(m_dlVisible, m_dlPct));
}

void PlayerBar::setBufferedMs(long ms)
{
    const long v = ms > 0 ? ms : 0;
    if (v == m_bufferedMs)
        return;                       // 250ms 轮询,值不变则不重绘
    m_bufferedMs = v;
    if (m_hover == HitBuffered)
        showTempTooltip(this, bufferedRect(),
                        pbBufferedTip(m_bufferedMs, m_durationMs));
    update();
}

void PlayerBar::onDlHide()
{
    m_dlVisible = false;
    m_dlPct = 0;
    update();
}

void PlayerBar::wheelEvent(QWheelEvent* e)
{
    const int dy = qWheelDeltaY(e);
    if (dy == 0) { e->ignore(); return; }
    emit volumeChanged(qBound(0, m_volume + (dy > 0 ? 5 : -5), 100));
    e->accept();
}

void PlayerBar::toggleVolumePopup()
{
    if (m_volumePopup) {
        QWidget* pw = (QWidget*)m_volumePopup;
        if (pw->isVisible()) { pw->close(); return; }
    }
    if (!m_volumePopup) {
        VolumePopupFrame* f = new VolumePopupFrame(this);
        VolumePanelSlider* sl = new VolumePanelSlider(this, f);
        f->setPanel(sl);
        sl->move(8, 9);
        QCheckBox* c = new QCheckBox(_("player.mute"), f);
        c->move(142, 13);
        QObject::connect(c, SIGNAL(toggled(bool)), this, SLOT(onMuteToggled(bool)));
        f->resize(246, 42);
        m_volumePopup = f;
        m_volumeSlider = sl;
        m_muteCheck = c;
    }
    VolumePanelSlider* sl = (VolumePanelSlider*)m_volumeSlider;
    sl->setValue(m_volume > 0 ? m_volume : m_lastVolume);
    QCheckBox* c = (QCheckBox*)m_muteCheck;
    c->blockSignals(true);
    c->setChecked(m_muted || m_volume == 0);
    c->blockSignals(false);
    QWidget* f = (QWidget*)m_volumePopup;
    f->move(mapToGlobal(QPoint(volRect().left() - 200, volRect().bottom() + 4)));
    f->resize(246, 42);
    f->show();
}

void PlayerBar::onPopupVolume(int v)
{
    if (v > 0) {
        m_volume = qBound(0, v, 100);
        m_lastVolume = m_volume;
        if (m_muted) {
            m_muted = false;
            emit muteChanged(false);
        }
    }
    emit volumeChanged(qBound(0, v, 100));
}

void PlayerBar::onMuteToggled(bool on)
{
    m_muted = on;
    if (on) {
        if (m_volume > 0) m_lastVolume = m_volume;
        m_volume = 0;
        emit volumeChanged(0);
    } else {
        m_volume = m_lastVolume > 0 ? m_lastVolume : 50;
        emit volumeChanged(m_volume);
    }
    update();
    emit muteChanged(on);
}
