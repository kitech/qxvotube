#include "queuepanel.h"
#include "musicstyle.h"
#include "translator.h"
#include "identicon.h"
#include "videothumbcache.h"

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

/* 当前播放标识:向右实心三角(自绘,不依赖 ▶ 字形/字体行高,Qt3/Qt4 一致) */
static void drawPlayTriangle(QPainter& p, const QRect& r, const QColor& c)
{
    MuiPolygon tri(3);
    tri.setPoint(0, r.left(), r.top());
    tri.setPoint(1, r.left(), r.bottom());
    tri.setPoint(2, r.right(), r.center().y());
    const QPen oldPen = p.pen();
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawPolygon(tri);
    p.setPen(oldPen);
}

QueuePanel::QueuePanel(QWidget* parent)
    : QWidget(parent), m_hasCurrent(false), m_currentIdx(-1),
      m_scrollBar(0), m_scrollY(0), m_scrollDelta(0), m_listTop(46)
{
    setMinimumWidth(200);
    setMaximumWidth(320);
    m_scrollBar = new LimeScrollBar(Qt::Vertical, this);
    m_scrollBar->hide();
    connect(m_scrollBar, SIGNAL(valueChanged(int)), this, SLOT(onScroll(int)));
    retranslateUi();
}

void QueuePanel::retranslateUi()
{
    m_title = _("queue.title");
    update();
}

void QueuePanel::setCurrentTrack(const Track& t)
{
    m_current = t;
    m_hasCurrent = true;
    updateScrollBar();
    update();
}

void QueuePanel::setCurrentIndex(int index)
{
    if (m_currentIdx == index) return;
    m_currentIdx = index;
    update();
}

void QueuePanel::setQueue(const std::vector<Track>& queue, int startIndex)
{
    m_queue = queue;
    m_currentIdx = startIndex;
    m_scrollY = 0;
    updateScrollBar();
    update();
}

void QueuePanel::updateScrollBar()
{
    if (!m_scrollBar) return;
    int max = qMax(0, (int)m_queue.size() * 44 - qMax(0, height() - m_listTop));
    if (m_scrollY > max) m_scrollY = max;
    if (m_scrollY < 0) m_scrollY = 0;
    m_scrollBar->setRange(0, max);
    m_scrollBar->setPageStep(qMax(1, height() - m_listTop));
    if (max > 0) {
        m_scrollBar->blockSignals(true);
        m_scrollBar->setValue(m_scrollY);
        m_scrollBar->blockSignals(false);
        m_scrollBar->show();
    } else {
        m_scrollBar->hide();
    }
}

void QueuePanel::onScroll(int v)
{
    m_scrollY = v;
    update();
}

void QueuePanel::wheelEvent(QWheelEvent* e)
{
    m_scrollDelta += qWheelDeltaY(e);
    int steps = m_scrollDelta / 120;
    if (steps == 0) return;
    m_scrollDelta -= steps * 120;
    int newY = m_scrollY - steps * (44 * 3);
    int max = qMax(0, (int)m_queue.size() * 44 - qMax(0, height() - m_listTop));
    m_scrollY = qBound(0, newY, max);
    updateScrollBar();
    update();
}

void QueuePanel::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    if (m_scrollBar)
        m_scrollBar->setGeometry(width() - 12, m_listTop, 12, qMax(0, height() - m_listTop));
    updateScrollBar();
}

void QueuePanel::customEvent(CustomEventBase* e)
{
    if (e->type() == ThumbReadyEventType) {
        ThumbReadyEvent* te = static_cast<ThumbReadyEvent*>(e);
        thumbcache::accept(te->url, te->image);
        update();
        return;
    }
    QWidget::customEvent(e);
}

void QueuePanel::paintEvent(QPaintEvent*)
{
    const StyleParams::Palette& pal = muiPalette();
    QPainter p(this);
    p.fillRect(rect(), pal.baseBg);

    QFont hdr = font();
    hdr.setBold(true);
    QFont bold = font();
    bold.setBold(true);
    QFont small = font();
    small.setPointSize(qMax(7, small.pointSize() - 2));

    p.setFont(hdr);
    p.setPen(pal.textPrimary);
    p.drawText(QRect(16, 10, width() - 32, 24), Qt::AlignVCenter | Qt::AlignLeft, m_title);

    int y = 46;
    if (m_hasCurrent) {
        int coverSize = qMin(width() - 48, 150);
        QPixmap cover = thumbcache::pixmap(m_current.coverUrl);
        if (cover.isNull()) {
            if (!m_current.coverUrl.isEmpty()) {
                thumbcache::request(m_current.coverUrl, this);
            }
            cover = generateIdenticon(m_current.coverSeed, coverSize);
        }
        if (cover.width() != coverSize || cover.height() != coverSize) {
#ifdef QT3_BUILD
            cover.resize(coverSize, coverSize);
#else
            cover = cover.scaled(coverSize, coverSize, Qt::IgnoreAspectRatio,
                                 Qt::SmoothTransformation);
#endif
        }
        int cx = (width() - coverSize) / 2;
        p.drawPixmap(cx, y, cover);
        y += coverSize + 12;

        p.setFont(bold);
        p.setPen(pal.textPrimary);
        p.drawText(QRect(16, y, width() - 32, 22), Qt::AlignVCenter | Qt::AlignHCenter, m_current.title);
        y += 22;
        p.setFont(small);
        p.setPen(pal.textMuted);
        p.drawText(QRect(16, y, width() - 32, 20), Qt::AlignVCenter | Qt::AlignHCenter, m_current.artist);
        y += 30;
    }

    p.setFont(small);
    p.setPen(pal.textMuted);
    p.drawText(QRect(16, y, width() - 32, 20), Qt::AlignVCenter | Qt::AlignLeft, m_title);
    y += 24;

    /* 列表区顶部固定，曲目区进入滚动域（对齐 ContentView 滚动模式） */
    m_listTop = y;
    p.setClipRect(0, y, width(), height() - y);
    for (size_t i = 0; i < m_queue.size(); ++i) {
        int ry = y + (int)i * 44 - m_scrollY;
        if (ry + 44 <= y) continue;            /* 滚出上方 */
        if (ry > height()) break;              /* 滚出下方 */
        const Track& t = m_queue[i];
        bool selected = ((int)i == m_currentIdx);
        if (selected)
            p.fillRect(QRect(8, ry, width() - 16, 40), pal.activeBg);
        p.setFont(small);
        p.setPen(selected ? pal.accent : pal.textMuted);
        /* 当前播放行:序号前自绘向右三角(不依赖 ▶ 字形) */
        if (selected)
            drawPlayTriangle(p, QRect(5, ry + 13, 11, 14), pal.accent);
        p.drawText(QRect(16, ry, 22, 40), Qt::AlignVCenter | Qt::AlignLeft, QString::number((int)i + 1));
        p.setFont(selected ? bold : font());
        p.setPen(selected ? pal.accent : pal.textPrimary);
        p.drawText(QRect(40, ry + 3, width() - 56, 20), Qt::AlignVCenter | Qt::AlignLeft, t.title);
        p.setFont(small);
        p.setPen(pal.textMuted);
        p.drawText(QRect(40, ry + 21, width() - 56, 18), Qt::AlignVCenter | Qt::AlignLeft, t.artist);
    }
    /* 滚动条几何随列表区实高（封面高度动态，paint 内同步最准） */
    if (m_scrollBar)
        m_scrollBar->setGeometry(width() - 12, y, 12, qMax(0, height() - y));
    updateScrollBar();
}
