#include "queuepanel.h"
#include "musicstyle.h"
#include "translator.h"
#include "identicon.h"

#include <qglobal.h>
#include <qpainter.h>
#include <qevent.h>
#include <qfontmetrics.h>

QueuePanel::QueuePanel(QWidget* parent)
    : QWidget(parent), m_hasCurrent(false), m_currentIdx(-1)
{
    setMinimumWidth(200);
    setMaximumWidth(320);
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
    update();
}

void QueuePanel::setQueue(const std::vector<Track>& queue, int startIndex)
{
    m_queue = queue;
    m_currentIdx = startIndex;
    update();
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
        QPixmap cover = generateIdenticon(m_current.coverSeed, coverSize);
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

    p.setClipRect(0, y, width(), height() - y);
    for (size_t i = 0; i < m_queue.size(); ++i) {
        if (y > height()) break;
        const Track& t = m_queue[i];
        bool selected = ((int)i == m_currentIdx);
        if (selected)
            p.fillRect(QRect(8, y, width() - 16, 40), pal.activeBg);
        p.setFont(small);
        p.setPen(selected ? pal.accent : pal.textMuted);
        p.drawText(QRect(16, y, 22, 40), Qt::AlignVCenter | Qt::AlignLeft, QString::number((int)i + 1));
        p.setFont(selected ? bold : font());
        p.setPen(selected ? pal.accent : pal.textPrimary);
        p.drawText(QRect(40, y + 3, width() - 56, 20), Qt::AlignVCenter | Qt::AlignLeft, t.title);
        p.setFont(small);
        p.setPen(pal.textMuted);
        p.drawText(QRect(40, y + 21, width() - 56, 18), Qt::AlignVCenter | Qt::AlignLeft, t.artist);
        y += 44;
    }
}
