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
      m_volDrag(false), m_volume(100), m_rate(1.0), m_muted(false)
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
    int cx = width() / 2;
    return QRect(cx - 15, 14, 30, 30);
}

QRect PlayerBar::prevRect() const
{
    int cx = width() / 2;
    return QRect(cx - 60, 19, 20, 20);
}

QRect PlayerBar::nextRect() const
{
    int cx = width() / 2;
    return QRect(cx + 40, 19, 20, 20);
}

QRect PlayerBar::progressRect() const
{
    int cx = width() / 2;
    return QRect(cx - 130, 58, 260, 6);
}

/* M5-A2 右侧:音量条 + 倍速 + EQ + 截图 */
QRect PlayerBar::volRect() const   { return QRect(width() - 250, 29, 70, 6); }
QRect PlayerBar::rateRect() const  { return QRect(width() - 172, 22, 40, 20); }
QRect PlayerBar::ccRect() const    { return QRect(width() - 126, 22, 30, 20); }
QRect PlayerBar::eqRect() const    { return QRect(width() - 90, 22, 34, 20); }
QRect PlayerBar::shotRect() const  { return QRect(width() - 52, 22, 34, 20); }
/* 二期同步:静音按钮(音量条左侧) */
QRect PlayerBar::muteRect() const  { return QRect(width() - 288, 22, 30, 20); }

void PlayerBar::setVolumeFromX(int x)
{
    QRect vr = volRect();
    if (vr.width() <= 0) return;
    int rel = qBound(0, x - vr.x(), vr.width());
    m_volume = (int)((double)rel / vr.width() * 100.0);
    update();
    emit volumeChanged(m_volume);
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

    /* 静音按钮(🔊/🔇;二期同步自 qskvotube) */
    p.setFont(small);
    p.setPen(m_hover == HitMute ? pal.accent : pal.textMuted);
    p.drawText(muteRect(), Qt::AlignCenter,
               m_muted ? qFromUtf8("🔇") : qFromUtf8("🔊"));

    /* 音量条 + 倍速/EQ/截图(右侧) */
    QRect vr = volRect();
    p.setPen(Qt::NoPen);
    p.setBrush(pal.border);
    muiDrawRoundRect(p, vr, 3, 3);
    int vfill = vr.width() * m_volume / 100;
    if (vfill > 0) {
        p.setBrush(m_hover == HitVol ? pal.accent : pal.textMuted);
        muiDrawRoundRect(p, QRect(vr.x(), vr.y(), vfill, vr.height()), 3, 3);
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
}

void PlayerBar::mousePressEvent(QMouseEvent* e)
{
    if (getenv("QLVOTUBE_DEBUG") != 0)
        fprintf(stderr, "pbar w=%d click=%d,%d vol=%d,%d rate=%d,%d eq=%d,%d shot=%d,%d\n",
                width(), e->pos().x(), e->pos().y(),
                volRect().x(), volRect().y(), rateRect().x(), rateRect().y(),
                eqRect().x(), eqRect().y(), shotRect().x(), shotRect().y());
    if (playRect().contains(e->pos())) {
        emit playPauseRequested();
    } else if (prevRect().contains(e->pos())) {
        emit prevRequested();
    } else if (nextRect().contains(e->pos())) {
        emit nextRequested();
    } else if (progressRect().contains(e->pos())) {
        m_dragging = true;
        setProgressFromX(e->pos().x());
    } else if (muteRect().contains(e->pos())) {
        m_muted = !m_muted;
        update();
        emit muteChanged(m_muted);
    } else if (volRect().contains(e->pos())) {
        m_volDrag = true;
        setVolumeFromX(e->pos().x());
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
    if (m_volDrag) {
        setVolumeFromX(e->pos().x());
        return;
    }
    Hit h = HitNone;
    if (playRect().contains(e->pos())) h = HitPlay;
    else if (prevRect().contains(e->pos())) h = HitPrev;
    else if (nextRect().contains(e->pos())) h = HitNext;
    else if (progressRect().contains(e->pos())) h = HitProgress;
    else if (volRect().contains(e->pos())) h = HitVol;
    else if (rateRect().contains(e->pos())) h = HitRate;
    else if (ccRect().contains(e->pos())) h = HitSub;
    else if (eqRect().contains(e->pos())) h = HitEq;
    else if (shotRect().contains(e->pos())) h = HitShot;
    else if (muteRect().contains(e->pos())) h = HitMute;
    if (h != m_hover) {
        m_hover = h;
        update();
    }
}

void PlayerBar::mouseReleaseEvent(QMouseEvent*)
{
    if (m_dragging) {
        m_dragging = false;
        emit seekRequested(m_progressMs);
    }
    m_volDrag = false;
    update();
}

void PlayerBar::leaveEvent(QEvent* e)
{
    QWidget::leaveEvent(e);
    if (m_hover != HitNone) {
        m_hover = HitNone;
        update();
    }
}
