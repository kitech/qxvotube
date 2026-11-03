#include "playerbar.h"
#include "musicstyle.h"
#include "translator.h"
#include "identicon.h"

#include <qglobal.h>
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
      m_progressMs(0), m_durationMs(0), m_hover(HitNone), m_dragging(false)
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
    p.drawText(QRect(pr.right() + 4, pr.y() - 8, 44, 20), Qt::AlignVCenter | Qt::AlignLeft,
               fmtMs(m_hasTrack ? m_durationMs : 0));
}

void PlayerBar::mousePressEvent(QMouseEvent* e)
{
    if (playRect().contains(e->pos())) {
        emit playPauseRequested();
    } else if (prevRect().contains(e->pos())) {
        emit prevRequested();
    } else if (nextRect().contains(e->pos())) {
        emit nextRequested();
    } else if (progressRect().contains(e->pos())) {
        m_dragging = true;
        setProgressFromX(e->pos().x());
    }
}

void PlayerBar::mouseMoveEvent(QMouseEvent* e)
{
    if (m_dragging) {
        setProgressFromX(e->pos().x());
        return;
    }
    Hit h = HitNone;
    if (playRect().contains(e->pos())) h = HitPlay;
    else if (prevRect().contains(e->pos())) h = HitPrev;
    else if (nextRect().contains(e->pos())) h = HitNext;
    else if (progressRect().contains(e->pos())) h = HitProgress;
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
}

void PlayerBar::leaveEvent(QEvent* e)
{
    QWidget::leaveEvent(e);
    if (m_hover != HitNone) {
        m_hover = HitNone;
        update();
    }
}
