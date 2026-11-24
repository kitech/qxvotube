#include "queuepanel.h"
#include "musicstyle.h"
#include "translator.h"
#include "identicon.h"
#include "videothumbcache.h"

#include <qglobal.h>
#include <qpainter.h>
#include <qevent.h>
#include <qfontmetrics.h>
#include <qtimer.h>
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
      m_scrollBar(0), m_scrollY(0), m_scrollDelta(0), m_listTop(46),
      m_tipRow(-1), m_tipTimer(0)
{
    setMinimumWidth(200);
    setMaximumWidth(320);
    setMouseTracking(true);
    m_scrollBar = new LimeScrollBar(Qt::Vertical, this);
    m_scrollBar->hide();
    connect(m_scrollBar, SIGNAL(valueChanged(int)), this, SLOT(onScroll(int)));
    m_tipTimer = new QTimer(this);
    connect(m_tipTimer, SIGNAL(timeout()), this, SLOT(onTipTimer()));
    retranslateUi();
}

void QueuePanel::retranslateUi()
{
    cancelTip();
    m_title = _("queue.title");
    update();
}

void QueuePanel::setCurrentTrack(const Track& t)
{
    cancelTip();
    m_current = t;
    m_hasCurrent = true;
    updateScrollBar();
    update();
}

void QueuePanel::setCurrentIndex(int index)
{
    if (m_currentIdx == index) return;
    cancelTip();                       /* 高亮行变了,旧提示锚点失效 */
    m_currentIdx = index;
    update();
}

void QueuePanel::setQueue(const std::vector<Track>& queue, int startIndex)
{
    cancelTip();
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

QString QueuePanel::queueTitle() const
{
    return m_title + QString(" (%1/%2)")
                        .arg(m_currentIdx >= 0 ? m_currentIdx + 1 : 0)
                        .arg((int)m_queue.size());
}

void QueuePanel::onScroll(int v)
{
    m_scrollY = v;
    update();
}

void QueuePanel::wheelEvent(QWheelEvent* e)
{
    cancelTip();                       /* 滚动后悬停行已变,立即收提示 */
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
    cancelTip();                       /* 宽度/位置变化,矩形失效 */
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

/* 封面下方当前大标题的矩形(与 paint 同公式) */
QRect QueuePanel::currentTitleRect() const
{
    const int coverSize = qMin(width() - 48, 150);
    return QRect(16, 46 + coverSize + 12, width() - 32, 22);
}

QRect QueuePanel::rowTitleRect(int row) const
{
    return QRect(40, m_listTop + row * 44 - m_scrollY + 3,
                 width() - 56, 20);
}

void QueuePanel::cancelTip()
{
    if (m_tipTimer != 0)
        m_tipTimer->stop();
    if (m_tipRow == -1)
        return;
    m_tipRow = -1;
    showTempTooltip(this, QRect(), QString());   /* 空文本 = 隐藏 */
}

void QueuePanel::dismissTip()
{
    cancelTip();
}

void QueuePanel::onTipTimer()
{
    m_tipTimer->stop();   /* Qt3 无 setSingleShot:到点手动停,保持单次语义 */
    /* 延时到点:按当前滚动/尺寸重新取矩形与文字,避免用到过期坐标 */
    if (m_tipRow == -1)
        return;
    QRect r;
    QString text;
    QFont tf = font();
    if (m_tipRow == -2) {
        if (!m_hasCurrent) { m_tipRow = -1; return; }
        r = currentTitleRect();
        text = m_current.title;
        tf.setBold(true);
    } else {
        if (m_tipRow < 0 || m_tipRow >= (int)m_queue.size()) {
            m_tipRow = -1;
            return;
        }
        r = rowTitleRect(m_tipRow);
        text = m_queue[m_tipRow].title;
        if (m_tipRow == m_currentIdx)
            tf.setBold(true);
    }
    /* 二次校验:期间可能变宽/换行,已不截断就不再提示 */
    if (qFontWidth(QFontMetrics(tf), text) <= r.width()) {
        m_tipRow = -1;
        return;
    }
    /* 60s 兜底,实际由移出/换行/滚动/点击/Esc 主动关闭 */
    showTempTooltip(this, r, text, 60000);
}

void QueuePanel::mouseMoveEvent(QMouseEvent* e)
{
    int row = -1;
    if (e->pos().y() < m_listTop) {
        if (m_hasCurrent && currentTitleRect().contains(e->pos()))
            row = -2;
    } else {
        const int idx = (e->pos().y() - m_listTop + m_scrollY) / 44;
        if (idx >= 0 && idx < (int)m_queue.size()
            && rowTitleRect(idx).contains(e->pos()))
            row = idx;
    }
    if (row == m_tipRow)
        return;          /* 同一目标:延时中/已显示,不重置、不闪烁 */
    cancelTip();         /* 目标变化:先收旧提示 */
    if (row == -1)
        return;
    QString text;
    QRect r;
    QFont tf = font();
    if (row == -2) {
        text = m_current.title;
        r = currentTitleRect();
        tf.setBold(true);
    } else {
        text = m_queue[row].title;
        r = rowTitleRect(row);
        if (row == m_currentIdx)
            tf.setBold(true);
    }
    if (qFontWidth(QFontMetrics(tf), text) <= r.width())
        return;          /* 未截断:不提示 */
    m_tipRow = row;
    m_tipTimer->start(500);   /* 延时显示 */
}

void QueuePanel::mousePressEvent(QMouseEvent* e)
{
    cancelTip();         /* 点击立刻收起(不新增行点击行为) */
    QWidget::mousePressEvent(e);
}

void QueuePanel::leaveEvent(QEvent* e)
{
    cancelTip();
    QWidget::leaveEvent(e);
}

void QueuePanel::hideEvent(QHideEvent* e)
{
    cancelTip();
    QWidget::hideEvent(e);
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
    p.drawText(QRect(16, 10, width() - 32, 24),
               Qt::AlignVCenter | Qt::AlignLeft,
               queueTitle());

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
        p.drawText(currentTitleRect(), Qt::AlignVCenter | Qt::AlignHCenter, m_current.title);
        y += 22;
        p.setFont(small);
        p.setPen(pal.textMuted);
        p.drawText(QRect(16, y, width() - 32, 20), Qt::AlignVCenter | Qt::AlignHCenter, m_current.artist);
        y += 30;
    }

    p.setFont(small);
    p.setPen(pal.textMuted);
    p.drawText(QRect(16, y, width() - 32, 20),
               Qt::AlignVCenter | Qt::AlignLeft, queueTitle());
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
        p.drawText(rowTitleRect((int)i), Qt::AlignVCenter | Qt::AlignLeft, t.title);
        p.setFont(small);
        p.setPen(pal.textMuted);
        p.drawText(QRect(40, ry + 21, width() - 56, 18), Qt::AlignVCenter | Qt::AlignLeft, t.artist);
    }
    /* 滚动条几何随列表区实高（封面高度动态，paint 内同步最准） */
    if (m_scrollBar)
        m_scrollBar->setGeometry(width() - 12, y, 12, qMax(0, height() - y));
    updateScrollBar();
}
