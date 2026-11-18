#include "contentview.h"
#include "musicstyle.h"
#include "translator.h"
#include "identicon.h"
#include "LimeScrollBar.h"

#include <qglobal.h>
#include <qpainter.h>
#include <qevent.h>
#include <qdatetime.h>
#include <qfontmetrics.h>
#if QT_VERSION >= 0x040000
#include <QDebug>
#endif

#include "videothumbcache.h"
#include "eventpoller.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace {
const int kCardTextH = 46;
const int kCardGap = 12;
const int kMinCardW = 200;
const int kFooterH = 56;

QString fmtMs(int ms)
{
    int total = ms / 1000;
    int m = total / 60;
    int s = total % 60;
    QString ss = QString::number(s);
    if (s < 10) ss = QString("0") + ss;
    return QString::number(m) + ":" + ss;
}

/* 单行截断（Qt3 无 QFontMetrics::elidedText 的等价多行能力） */
QString elideOneLine(QPainter& p, const QString& text, int maxW)
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

/* 播放次数：>=1e8 → x.x亿，>=1e4 → x.x万，其余原样 */
QString fmtCount(qint64 n)
{
    if (n >= 100000000LL)
        return QString::number((double)n / 100000000.0, 'f', 1) + _("num.yi");
    if (n >= 10000LL)
        return QString::number((double)n / 10000.0, 'f', 1) + _("num.wan");
    return QString::number((unsigned long long)n);
}
}

ContentView::ContentView(QWidget* parent)
    : QWidget(parent), m_pl(0), m_mode(TracksMode), m_scrollBar(0), m_scrollY(0),
      m_scrollDelta(0), m_hover(-1), m_current(-1), m_playlistId(-1),
      m_headerH(64), m_rowH(48), m_lastMoreAt(0),
      m_hasMore(false), m_loadingNow(false)
{
    setMouseTracking(true);
    /* 可聚焦:点击网格后把焦点从搜索框拿回,快捷键(空格/方向键)才生效 */
#if QT_VERSION >= 0x040000
    setFocusPolicy(Qt::StrongFocus);
#else
    setFocusPolicy(QWidget::StrongFocus);   /* Qt3:枚举在 QWidget 内 */
#endif
    m_scrollBar = new LimeScrollBar(Qt::Vertical, this);
    QObject::connect(m_scrollBar, SIGNAL(valueChanged(int)), this, SLOT(onScroll(int)));
    retranslateUi();
}

void ContentView::retranslateUi()
{
    m_playAll = _("content.playall");
    m_back = _("content.back");
    m_loadMore = _("content.load_more");
    m_loading = _("content.loading");
    update();
}

void ContentView::setLibrary(const std::vector<Playlist>& lib)
{
    m_lib = lib;
    /* m_pl 指向旧 m_lib 元素，重拷贝后失效：按 m_playlistId 重找，
     * 找不到再回落到第一个歌单 */
    if (m_mode == TracksMode) {
        const Playlist* cur = (m_playlistId >= 0) ? findPlaylist(m_lib, m_playlistId) : 0;
        if (cur) {
            m_pl = cur;
            m_current = -1;
            m_scrollY = 0;
            rebuildRows();
            updateScrollBar();
        } else if (!m_lib.empty()) {
            showPlaylist(m_lib[0].id);
        } else {
            m_pl = 0;
            m_playlistId = -1;
            rebuildRows();
        }
    }
    update();
}

void ContentView::showPlaylist(int playlistId)
{
    m_mode = TracksMode;
    m_cards.clear();
    m_gridCats.clear();
    m_pl = findPlaylist(m_lib, playlistId);
    m_playlistId = playlistId;
    m_current = -1;
    m_scrollY = 0;
    m_hasMore = false;
    m_loadingNow = false;
    rebuildRows();
    updateScrollBar();
    update();
}

void ContentView::showPlaylistList(const QString& title, const std::vector<RemotePlaylistCard>& cards,
                                   bool hasMore, bool loading)
{
    m_mode = PlaylistListMode;
    m_pl = 0;
    m_playlistId = -1;
    m_listTitle = title;
    m_cards = cards;
    m_hasMore = hasMore;
    m_loadingNow = loading;
    m_hover = -1;
    m_current = -1;
    m_scrollY = 0;
    rebuildRows();
    updateScrollBar();
    update();
}

void ContentView::setListLoading(bool loading)
{
    if (m_mode != PlaylistListMode) return;
    m_loadingNow = loading;
    rebuildRows();
    updateScrollBar();
    update();
}

void ContentView::showCategoryGrid(const QStringList& cats)
{
    m_mode = CategoryGridMode;
    m_pl = 0;
    m_playlistId = -1;
    m_listTitle = _("content.categories");
    m_gridCats = cats;
    m_hasMore = false;
    m_loadingNow = false;
    m_hover = -1;
    m_current = -1;
    m_scrollY = 0;
    rebuildRows();
    updateScrollBar();
    update();
}

void ContentView::showVideoGrid(const QString& title)
{
    m_mode = VideoGridMode;
    m_pl = 0;
    m_playlistId = -1;
    m_listTitle = title;
    m_scrollY = 0;
    m_hover = -1;
    m_current = -1;
    m_gridErr = QString();
    m_hasMore = true;
    m_loadingNow = true;
    rebuildRows();
    updateScrollBar();
    update();
}

void ContentView::replaceVideoCards(const std::vector<VideoCard>& cards,
                                    bool hasMore)
{
    m_videos = cards;
    m_hasMore = hasMore;
    m_loadingNow = false;
    m_gridErr = QString();
    m_scrollY = 0;              /* 新分类/搜索回到顶部 */
    rebuildRows();
    updateScrollBar();
    update();
}

void ContentView::clearVideoCards()
{
    m_videos.clear();
    m_gridErr = QString();
    m_scrollY = 0;
    rebuildRows();
    updateScrollBar();
    update();
}

void ContentView::appendVideoCards(const std::vector<VideoCard>& cards, bool hasMore)
{
    for (size_t vi = 0; vi < cards.size(); ++vi) m_videos.push_back(cards[vi]);
    m_hasMore = hasMore;
    m_loadingNow = false;
    m_gridErr = QString();
    rebuildRows();
    updateScrollBar();
    update();
}

void ContentView::setVideoGridLoading(bool loading)
{
    if (m_mode != VideoGridMode) return;
    m_loadingNow = loading;
    rebuildRows();
    updateScrollBar();
    update();
}

void ContentView::setVideoGridError(const QString& text)
{
    if (m_mode != VideoGridMode) return;
    m_gridErr = text;
    m_loadingNow = false;
    rebuildRows();
    updateScrollBar();
    update();
}

int ContentView::gridCols() const
{
    return qMax(1, (width() - 40 + kCardGap) / (kMinCardW + kCardGap));
}

int ContentView::gridCardH() const
{
    const int cols = gridCols();
    const int cardW = (width() - 40 - (cols - 1) * kCardGap) / cols;
    return cardW * 9 / 16 + kCardTextH;   /* 16:9 封面 + 文案区 */
}

void ContentView::rebuildRows()
{
    m_rows.clear();
    if (m_mode == TracksMode) {
        if (!m_pl) return;
        for (size_t i = 0; i < m_pl->tracks.size(); ++i) {
            Row r;
            r.index = (int)i;
            r.y = m_headerH + (int)i * m_rowH;
            r.h = m_rowH;
            m_rows.push_back(r);
        }
    } else if (m_mode == PlaylistListMode) {
        for (size_t i = 0; i < m_cards.size(); ++i) {
            Row r;
            r.index = (int)i;
            r.y = m_headerH + (int)i * 56;
            r.h = 56;
            m_rows.push_back(r);
        }
    } else if (m_mode == CategoryGridMode) {
        const int cols = qMax(1, (width() - 40) / 180);
        for (int i = 0; i < (int)m_gridCats.size(); ++i) {
            Row r;
            r.index = i;
            r.y = m_headerH + (i / cols) * 52;
            r.h = 44;
            m_rows.push_back(r);
        }
    } else if (m_mode == VideoGridMode) {
        const int cols = gridCols();
        const int step = gridCardH() + kCardGap;
        for (int i = 0; i < (int)m_videos.size(); ++i) {
            Row r;
            r.index = i;
            r.y = m_headerH + (i / cols) * step;
            r.h = gridCardH();
            m_rows.push_back(r);
        }
    }
}

int ContentView::totalHeight() const
{
    if (m_mode == TracksMode)
        return m_headerH + (int)m_rows.size() * m_rowH;
    if (m_mode == PlaylistListMode) {
        int h = m_headerH + (int)m_rows.size() * 56;
        if (m_loadingNow) h += 44;
        else if (m_hasMore) h += 56;
        return h;
    }
    if (m_mode == CategoryGridMode) {
        const int cols = qMax(1, (width() - 40) / 180);
        if (m_rows.empty()) return m_headerH;
        const Row& last = m_rows.back();
        return last.y + last.h + 8;
    }
    if (m_mode == VideoGridMode) {
        int h = m_headerH;
        if (!m_rows.empty()) {
            const int cols = gridCols();
            const int rows = (int)((m_rows.size() + cols - 1) / cols);
            h += rows * (gridCardH() + kCardGap);
        }
        if (m_loadingNow || m_hasMore || !m_gridErr.isEmpty()
            || m_videos.empty())
            h += kFooterH;
        return h;
    }
    return m_headerH;
}

QRect ContentView::playAllRect() const
{
    return QRect(20, 34, 92, 24);
}

QRect ContentView::backRect() const
{
    return QRect(width() - 92, 34, 72, 24);
}

QRect ContentView::loadMoreRect() const
{
    if (m_mode == VideoGridMode) {
        const int cols = gridCols();
        const int rows = (int)((m_rows.size() + cols - 1) / cols);
        const int y = m_headerH + rows * (gridCardH() + kCardGap) + 8;
        return QRect(width() / 2 - 60, y, 120, 32);
    }
    return QRect(width() / 2 - 90, m_headerH + (int)m_rows.size() * 56 + 6, 180, 36);
}

QRect ContentView::retryRect() const
{
    const int cols = gridCols();
    const int rows = (int)((m_rows.size() + cols - 1) / cols);
    const int y = m_headerH + rows * (gridCardH() + kCardGap) + 30;
    return QRect(width() / 2 - 50, y, 100, 28);
}

QRect ContentView::cardRect(int i) const
{
    const int cols = gridCols();
    const int cardW = (width() - 40 - (cols - 1) * kCardGap) / cols;
    const int x = 20 + (i % cols) * (cardW + kCardGap);
    const int y = m_headerH + (i / cols) * (gridCardH() + kCardGap);
    return QRect(x, y, cardW, gridCardH());
}

void ContentView::contextMenuEvent(QContextMenuEvent* e)
{
    if (m_mode != VideoGridMode)
        return;
    const int idx = rowAt(e->pos());
    if (idx < 0 || idx >= (int)m_videos.size())
        return;
    emit videoContextMenu(m_videos[idx].id, e->globalPos());
    e->accept();
}

int ContentView::rowAt(const QPoint& p) const
{
    int y = p.y() + m_scrollY;
    if (y < m_headerH) return -1;
    if (m_mode == TracksMode) {
        int idx = (y - m_headerH) / m_rowH;
        if (idx < 0 || idx >= (int)m_rows.size()) return -1;
        return idx;
    }
    if (m_mode == PlaylistListMode) {
        int idx = (y - m_headerH) / 56;
        if (idx < 0 || idx >= (int)m_rows.size()) return -1;
        return idx;
    }
    if (m_mode == CategoryGridMode) {
        for (size_t i = 0; i < m_rows.size(); ++i)
            if (y >= m_rows[i].y && y < m_rows[i].y + m_rows[i].h)
                return (int)i;
    }
    if (m_mode == VideoGridMode) {
        for (size_t i = 0; i < m_rows.size(); ++i) {
            QRect r = cardRect((int)i);
            r.moveTop(r.top() - m_scrollY);
            if (r.contains(p)) return m_rows[i].index;
        }
        return -1;
    }
    return -1;
}

void ContentView::onScroll(int value)
{
    if (value == m_scrollY) return;
    m_scrollY = value;
    update();
    maybeLoadMore();
}

void ContentView::updateScrollBar()
{
    int viewH = height() - m_headerH;
    int totalH = totalHeight();
    int maxScroll = qMax(0, totalH - viewH);
    if (m_scrollY > maxScroll) m_scrollY = maxScroll;
    if (m_scrollBar) {
        m_scrollBar->blockSignals(true);
        m_scrollBar->setRange(0, maxScroll);
        m_scrollBar->setPageStep(qMax(1, viewH));
        m_scrollBar->setValue(m_scrollY);
        m_scrollBar->blockSignals(false);
    }
}

void ContentView::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    if (m_scrollBar)
        m_scrollBar->setGeometry(width() - 12, m_headerH, 12, qMax(0, height() - m_headerH));
    if (m_mode == CategoryGridMode || m_mode == VideoGridMode)
        rebuildRows();
    updateScrollBar();
}

void ContentView::wheelEvent(QWheelEvent* e)
{
    m_scrollDelta += qWheelDeltaY(e);
    int steps = m_scrollDelta / 120;
    if (steps == 0) return;
    m_scrollDelta -= steps * 120;
    int newY = m_scrollY - steps * (m_rowH * 3);
    int viewH = height() - m_headerH;
    int maxScroll = qMax(0, totalHeight() - viewH);
    m_scrollY = qBound(0, newY, maxScroll);
    updateScrollBar();
    maybeLoadMore();
    update();
}

void ContentView::maybeLoadMore()
{
    if (m_mode != VideoGridMode || !m_hasMore || m_loadingNow
        || !m_gridErr.isEmpty())
        return;
    const int viewH = height() - m_headerH;
    const int totalH = totalHeight();
    if (viewH <= 0 || m_scrollY + viewH < totalH - 120) return;
    const int now = (int)::time(0);
    if (now == m_lastMoreAt) return;   /* 1s 节流 */
    m_lastMoreAt = now;
    if (getenv("QLVOTUBE_DEBUG") != 0)
        qDebug("grid: near bottom -> loadMore");
    emit loadMoreRequested();
}

void ContentView::customEvent(CustomEventBase* e)
{
    if (e->type() == ThumbReadyEventType) {
        ThumbReadyEvent* te = static_cast<ThumbReadyEvent*>(e);
        thumbcache::accept(te->url, te->image);
        update();   /* 封面到货 → 重绘(事件循环负责 delete) */
        return;
    }
    QWidget::customEvent(e);
}

void ContentView::paintEvent(QPaintEvent*)
{
    const StyleParams::Palette& pal = muiPalette();
    QPainter p(this);
    p.fillRect(rect(), pal.windowBg);

    QFont titleFont = font();
    titleFont.setBold(true);
    titleFont.setPointSize(qMax(10, titleFont.pointSize() + 4));
    QFont normal = font();
    QFont bold = font();
    bold.setBold(true);
    QFont small = font();
    small.setPointSize(qMax(7, small.pointSize() - 1));

    /* ── 标题区 ── */
    if (m_mode == TracksMode) {
        QString title = m_pl ? m_pl->name : QString();
        p.setFont(titleFont);
        p.setPen(pal.textPrimary);
        p.drawText(QRect(20, 4, width() - 40, 30), Qt::AlignVCenter | Qt::AlignLeft, title);

        QRect pa = playAllRect();
        p.setPen(Qt::NoPen);
        p.setBrush(pal.accent);
        muiDrawRoundRect(p, pa, pa.height() / 2, pa.height() / 2);
        p.setPen(pal.accentText);
        p.setFont(small);
        p.drawText(pa, Qt::AlignCenter, m_playAll);
    } else {
        /* List/Grid 模式：标题 + 返回按钮 */
        p.setFont(titleFont);
        p.setPen(pal.textPrimary);
        p.drawText(QRect(20, 4, width() - 200, 30), Qt::AlignVCenter | Qt::AlignLeft, m_listTitle);

        QRect br = backRect();
        p.setPen(Qt::NoPen);
        p.setBrush(pal.hoverBg);
        muiDrawRoundRect(p, br, br.height() / 2, br.height() / 2);
        p.setPen(pal.textPrimary);
        p.setFont(small);
        p.drawText(br, Qt::AlignCenter, QString::fromUtf8("← ") + m_back);
    }

    p.setPen(pal.border);
    p.drawLine(0, m_headerH - 1, width(), m_headerH - 1);

    p.setClipRect(0, m_headerH, width(), height() - m_headerH);

    if (m_mode == TracksMode) {
        if (m_pl) {
            for (size_t i = 0; i < m_rows.size(); ++i) {
                const Row& row = m_rows[i];
                int y = row.y - m_scrollY;
                if (y + row.h < m_headerH || y > height()) continue;
                const Track& t = m_pl->tracks[row.index];

                QRect rr(0, y, width() - 12, row.h);
                bool selected = ((int)i == m_current);
                bool hovered = ((int)i == m_hover);
                if (selected)
                    p.fillRect(rr, pal.activeBg);
                else if (hovered)
                    p.fillRect(rr, pal.hoverBg);

                p.setFont(small);
                p.setPen(selected ? pal.accent : pal.textMuted);
                p.drawText(QRect(16, y, 28, row.h), Qt::AlignVCenter | Qt::AlignRight,
                           QString::number(row.index + 1));

                QPixmap cover = generateIdenticon(t.coverUrl.isEmpty() ? t.coverSeed : t.coverUrl, 36);
                p.drawPixmap(56, y + (row.h - 36) / 2, cover);

                p.setFont(selected ? bold : normal);
                p.setPen(selected ? pal.accent : pal.textPrimary);
                p.drawText(QRect(104, y, width() - 200, row.h), Qt::AlignVCenter | Qt::AlignLeft, t.title);

                QFontMetrics fm(selected ? bold : normal);
                int titleW = qFontWidth(fm, t.title);
                p.setFont(small);
                p.setPen(pal.textMuted);
                QString sub = t.artist + QString::fromUtf8(" · ") + t.album;
                p.drawText(QRect(104 + titleW + 14, y, width() - 260 - titleW, row.h),
                           Qt::AlignVCenter | Qt::AlignLeft, sub);

                p.drawText(QRect(width() - 90, y, 60, row.h), Qt::AlignVCenter | Qt::AlignRight,
                           fmtMs(t.durationMs));
            }
        }
    } else if (m_mode == PlaylistListMode) {
        for (size_t i = 0; i < m_rows.size(); ++i) {
            const Row& row = m_rows[i];
            int y = row.y - m_scrollY;
            if (y + row.h < m_headerH || y > height()) continue;
            const RemotePlaylistCard& c = m_cards[row.index];

            QRect rr(0, y, width() - 12, row.h);
            bool hovered = ((int)i == m_hover);
            if (hovered)
                p.fillRect(rr, pal.hoverBg);

            p.setFont(small);
            p.setPen(pal.textMuted);
            p.drawText(QRect(16, y, 28, row.h), Qt::AlignVCenter | Qt::AlignRight,
                       QString::number(row.index + 1));

            QPixmap cover = generateIdenticon(c.coverUrl.isEmpty() ? QString::number((long long)c.id)
                                                                   : c.coverUrl, 40);
            p.drawPixmap(56, y + (row.h - 40) / 2, cover);

            p.setFont(bold);
            p.setPen(pal.textPrimary);
            p.drawText(QRect(108, y, width() - 220, row.h), Qt::AlignVCenter | Qt::AlignLeft, c.name);

            QFontMetrics fm(bold);
            int nameW = qFontWidth(fm, c.name);
            p.setFont(small);
            p.setPen(pal.textMuted);
            QString sub = c.creator + QString::fromUtf8(" · ") +
                          QString::number(c.trackCount) + QString::fromUtf8(" · ") +
                          fmtCount(c.playCount);
            p.drawText(QRect(108 + nameW + 14, y, width() - 280 - nameW, row.h),
                       Qt::AlignVCenter | Qt::AlignLeft, sub);
        }

        /* footer：加载中 or 加载更多 */
        if (m_loadingNow) {
            QRect lr(0, m_headerH + (int)m_rows.size() * 56 - m_scrollY, width(), 44);
            p.setFont(small);
            p.setPen(pal.textMuted);
            p.drawText(lr, Qt::AlignCenter, m_loading);
        } else if (m_hasMore) {
            QRect lr = loadMoreRect();
            lr.moveTop(lr.top() - m_scrollY);
            bool hovered = (m_hover == 1000000);
            const StyleParams::Palette& pal2 = muiPalette();
            p.setPen(Qt::NoPen);
            p.setBrush(hovered ? pal2.activeBg : pal2.hoverBg);
            muiDrawRoundRect(p, lr, lr.height() / 2, lr.height() / 2);
            p.setPen(pal2.textPrimary);
            p.setFont(small);
            p.drawText(lr, Qt::AlignCenter, m_loadMore);
        }
    } else if (m_mode == CategoryGridMode) {
        int cols = qMax(1, (width() - 40) / 180);
        int cellW = (width() - 40 - (cols - 1) * 12) / cols;
        for (size_t i = 0; i < m_rows.size(); ++i) {
            const Row& row = m_rows[i];
            int y = row.y - m_scrollY;
            if (y + row.h < m_headerH || y > height()) continue;
            int col = (int)i % cols;
            QRect cr(20 + col * (cellW + 12), y, cellW, row.h);
            bool hovered = ((int)i == m_hover);
            p.setPen(Qt::NoPen);
            p.setBrush(hovered ? pal.activeBg : pal.hoverBg);
            muiDrawRoundRect(p, cr, cr.height() / 2, cr.height() / 2);
            p.setPen(pal.textPrimary);
            p.setFont(normal);
            p.drawText(cr, Qt::AlignCenter, m_gridCats[row.index]);
        }
    } else if (m_mode == VideoGridMode) {
        const int cols = gridCols();
        for (size_t i = 0; i < m_rows.size(); ++i) {
            const Row& row = m_rows[i];
            int y = row.y - m_scrollY;
            if (y + row.h < m_headerH || y > height()) continue;
            const VideoCard& c = m_videos[row.index];
            QRect cr = cardRect((int)i);
            cr.moveTop(y);
            if ((int)i == m_hover)
                p.fillRect(cr, pal.hoverBg);

            /* 封面：异步缓存命中即画；未命中先用 identicon 占位并入队 */
            const int coverH = cr.width() * 9 / 16;
            QPixmap cover = thumbcache::pixmap(c.cover);
            if (cover.isNull()) {
                if (!c.cover.isEmpty()) thumbcache::request(c.cover, this);
                cover = generateIdenticon(c.cover.isEmpty() ? c.id : c.cover,
                                          qMin(coverH, 96));
            }
            if (cover.width() != cr.width() || cover.height() != coverH) {
#ifdef QT3_BUILD
#ifdef QT3_BUILD
                cover.resize(cr.width(), coverH);
#else
                cover = cover.scaled(cr.width(), coverH,
                                     Qt::IgnoreAspectRatio,
                                     Qt::SmoothTransformation);
#endif
#else
                cover = cover.scaled(cr.width(), coverH,
                                     Qt::IgnoreAspectRatio,
                                     Qt::SmoothTransformation);
#endif
            }
            p.drawPixmap(cr.x(), cr.y(), cover);

            p.setFont(bold);
            p.setPen(pal.textPrimary);
            p.drawText(QRect(cr.x(), cr.y() + coverH + 4, cr.width(), 18),
                       Qt::AlignVCenter | Qt::AlignLeft,
                       elideOneLine(p, c.title, cr.width()));
            p.setFont(small);
            p.setPen(pal.textMuted);
            QString sub = c.author;
            if (c.playCount > 0) {
                if (!sub.isEmpty()) sub += QString::fromUtf8(" · ");
                sub += fmtCount(c.playCount);
            }
            /* M7:发布日期(0=未知,不显示,避免假数据) */
            if (c.publishMs > 0) {
                if (!sub.isEmpty()) sub += QString::fromUtf8(" · ");
#ifdef QT3_BUILD
                QDateTime dt;
                dt.setTime_t((uint)(c.publishMs / 1000));
#elif QT_VERSION < 0x050800
                QDateTime dt = QDateTime::fromTime_t((uint)(c.publishMs / 1000));
#else
                QDateTime dt = QDateTime::fromSecsSinceEpoch(c.publishMs / 1000);
#endif
                sub += dt.toString(QString::fromLatin1("yyyy-MM-dd"));
            }
            if (!sub.isEmpty())
                p.drawText(QRect(cr.x(), cr.y() + coverH + 24,
                                 cr.width(), 16),
                           Qt::AlignVCenter | Qt::AlignLeft,
                           elideOneLine(p, sub, cr.width()));

            /* M7:封面右下角时长徽标 */
            if (c.durationMs > 0) {
                const QString dur = fmtMs((int)c.durationMs);
                QFontMetrics dfm(small);
                const int tw = qFontWidth(dfm, dur) + 10;
                QRect br(cr.x() + cr.width() - tw - 6,
                         cr.y() + coverH - 20, tw, 16);
                p.setPen(Qt::NoPen);
#ifdef QT3_BUILD
                p.setBrush(QColor(32, 32, 32));   /* Qt3 QColor 无 alpha 构造 */
#else
                p.setBrush(QColor(0, 0, 0, 170));
#endif
                muiDrawRoundRect(p, br, 3, 3);
                p.setPen(QColor(255, 255, 255));
                p.drawText(br, Qt::AlignCenter, dur);
            }
        }

        /* footer：错误+重试 / 加载中 / 加载更多 / 空态 */
        const int rows = (int)((m_rows.size() + cols - 1) / cols);
        QRect fr(0, m_headerH
                        + rows * (gridCardH() + kCardGap) - m_scrollY,
                 width(), kFooterH);
        p.setFont(small);
        if (!m_gridErr.isEmpty()) {
            p.setPen(pal.textMuted);
            p.drawText(QRect(fr.x(), fr.y(), fr.width(), 24),
                       Qt::AlignCenter, m_gridErr);
            QRect rr = retryRect();
            rr.moveTop(rr.top() - m_scrollY);
            p.setPen(Qt::NoPen);
            p.setBrush(pal.hoverBg);
            muiDrawRoundRect(p, rr, rr.height() / 2, rr.height() / 2);
            p.setPen(pal.textPrimary);
            p.drawText(rr, Qt::AlignCenter, _("content.retry"));
        } else if (m_loadingNow) {
            p.setPen(pal.textMuted);
            p.drawText(fr, Qt::AlignCenter, m_loading);
        } else if (m_hasMore) {
            QRect lr = loadMoreRect();
            lr.moveTop(lr.top() - m_scrollY);
            p.setPen(Qt::NoPen);
            p.setBrush(pal.hoverBg);
            muiDrawRoundRect(p, lr, lr.height() / 2, lr.height() / 2);
            p.setPen(pal.textPrimary);
            p.drawText(lr, Qt::AlignCenter, m_loadMore);
        } else if (m_videos.empty()) {
            p.setPen(pal.textMuted);
            p.drawText(fr, Qt::AlignCenter, _("content.empty"));
        }
    }
}

void ContentView::mousePressEvent(QMouseEvent* e)
{
    setFocus();   /* 点内容区 = 退出文本输入焦点,启用播放器快捷键 */
    /* M7:右键交给 contextMenuEvent,不能顺带激活条目 */
    if (e->button() != Qt::LeftButton)
        return;
    if (m_mode == PlaylistListMode || m_mode == CategoryGridMode
        || m_mode == VideoGridMode) {
        if (backRect().contains(e->pos())) {
            emit backRequested();
            return;
        }
    }
    if (m_mode == VideoGridMode) {
        if (!m_gridErr.isEmpty() && retryRect().contains(e->pos())) {
            emit loadMoreRequested();     /* 重试 = 重拉当前页 */
            return;
        }
        if (m_hasMore && !m_loadingNow && loadMoreRect().contains(e->pos())) {
            emit loadMoreRequested();
            return;
        }
        const int idx = rowAt(e->pos());
        if (idx < 0) return;
        if (getenv("QLVOTUBE_DEBUG") != 0)
#if QT_VERSION >= 0x060000
            qDebug() << "grid: video" << m_videos[idx].id;
#else
            qDebug("grid: video %s", qToUtf8(m_videos[idx].id).data());
#endif
        emit videoActivated(m_videos[idx].id);
        return;
    }
    if (m_mode == PlaylistListMode) {
        /* footer 劫持：load more */
        if (m_hasMore && !m_loadingNow) {
            QRect lr = loadMoreRect();
            lr.moveTop(lr.top() - m_scrollY);
            if (lr.contains(e->pos())) {
                emit loadMoreRequested();
                return;
            }
        }
        int idx = rowAt(e->pos());
        if (idx < 0) return;
        emit playlistActivated(m_cards[idx].id);
        return;
    }
    if (m_mode == CategoryGridMode) {
        int idx = rowAt(e->pos());
        if (idx < 0) return;
        emit categoryActivated(m_gridCats[idx]);
        return;
    }

    /* TracksMode */
    if (playAllRect().contains(e->pos())) {
        if (m_pl && !m_pl->tracks.empty()) {
            m_current = 0;
            emit trackActivated(m_playlistId, 0);
            update();
        }
        return;
    }
    int idx = rowAt(e->pos());
    if (idx < 0 || !m_pl) return;
    m_current = idx;
    emit trackActivated(m_playlistId, m_rows[idx].index);
    update();
}

void ContentView::mouseMoveEvent(QMouseEvent* e)
{
    if (m_mode == VideoGridMode) {
        int h = rowAt(e->pos());
        if (h < 0 && !m_gridErr.isEmpty() && retryRect().contains(e->pos()))
            h = 3000000;
        if (h < 0 && m_hasMore && !m_loadingNow
            && loadMoreRect().contains(e->pos()))
            h = 3000001;
        if (h != m_hover) { m_hover = h; update(); }
        return;
    }
    if (m_mode == PlaylistListMode) {
        int hIdx = -1;
        if (m_hasMore && !m_loadingNow) {
            QRect lr = loadMoreRect();
            lr.moveTop(lr.top() - m_scrollY);
            if (lr.contains(e->pos()))
                hIdx = 1000000;   /* load more 悬停哨兵 */
        }
        if (hIdx < 0)
            hIdx = rowAt(e->pos());
        if (hIdx != m_hover) {
            m_hover = hIdx;
            update();
        }
        return;
    }
    if (m_mode == TracksMode || m_mode == CategoryGridMode) {
        int idx = rowAt(e->pos());
        if (idx != m_hover) {
            m_hover = idx;
            update();
        }
        return;
    }
}

void ContentView::leaveEvent(QEvent* e)
{
    QWidget::leaveEvent(e);
    if (m_hover != -1) {
        m_hover = -1;
        update();
    }
}
