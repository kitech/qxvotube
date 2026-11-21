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
#include <qtimer.h>
#ifdef QT3_BUILD
#include <qpointarray.h>
typedef QPointArray MuiPolygon;
#else
typedef QPolygon MuiPolygon;
#endif
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

/* 封面统计徽标:半透明黑底 + 白字(Qt3 用不透明底色) */
static void drawStatBadge(QPainter& p, const QRect& r, const QString& text)
{
    p.setPen(Qt::NoPen);
#ifdef QT3_BUILD
    p.setBrush(QColor(32, 32, 32));   /* Qt3 QColor 无 alpha 构造 */
#else
    p.setBrush(QColor(0, 0, 0, 170));
#endif
    muiDrawRoundRect(p, r, 3, 3);
    p.setPen(QColor(255, 255, 255));
    p.drawText(r, Qt::AlignVCenter | Qt::AlignLeft,
               elideOneLine(p, text, r.width() - 8));
}

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
      m_hasMore(false), m_loadingNow(false), m_gridPage(0),
      m_tipRow(-1), m_tipTimer(0)
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
    m_tipTimer = new QTimer(this);
    QObject::connect(m_tipTimer, SIGNAL(timeout()), this, SLOT(onTipTimer()));
    retranslateUi();
}

void ContentView::retranslateUi()
{
    m_playAll = _("content.playall");
    m_back = _("content.back");
    m_refresh = qFromUtf8("刷新");   // 不依赖翻译资源,与"评"等文案同口径
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

void ContentView::setVideoGridTitle(const QString& title)
{
    if (m_listTitle == title) return;
    m_listTitle = title;
    if (m_mode == VideoGridMode) update();   /* 页头在 paintEvent 用 m_listTitle 绘制 */
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
    cancelTip();                 /* 数据/模式/几何变化:提示锚点一律作废 */
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

QRect ContentView::refreshRect() const
{
    return QRect(width() - 164, 34, 64, 24);   // 返回按钮左侧
}

bool ContentView::isVideoGridMode() const { return m_mode == VideoGridMode; }

void ContentView::setPlayingVideoId(const QString& id)
{
    if (m_playingVideoId == id) return;
    m_playingVideoId = id;
    update();
}

void ContentView::setVideoGridPage(int page)
{
    if (m_gridPage == page) return;
    m_gridPage = page;
    update();
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

void ContentView::statBadgeRects(int i, QRect* viewRect, QRect* replyRect) const
{
    if (viewRect) *viewRect = QRect();
    if (replyRect) *replyRect = QRect();
    if (i < 0 || i >= (int)m_rows.size()) return;
    const Row& row = m_rows[i];
    QRect cr = cardRect(i);
    cr.moveTop(row.y - m_scrollY);            /* 与 paint 同一 viewport 坐标 */
    const int coverH = cr.width() * 9 / 16;
    const VideoCard& c = m_videos[row.index];
    const int room = cr.width() - (c.durationMs > 0 ? 64 : 12);  /* 给右下角时长留位 */
    if (room < 24) return;                    /* 卡片过窄不画 */
    QFont sf = font();
    sf.setPointSize(qMax(7, sf.pointSize() - 1));   /* 与 paint 的 small 一致 */
    QFontMetrics sfm(sf);
    const int by = cr.y() + coverH - 21;
    const int bh = 16;
    int bx = cr.x() + 6;
    if (c.playCount > 0) {
        const int vw = qMin(qFontWidth(sfm, fmtCount(c.playCount)) + 12, room);
        if (viewRect) *viewRect = QRect(bx, by, vw, bh);
        bx += vw + 4;
    }
    if (c.replyCount > 0) {
        const int maxw = room - (bx - (cr.x() + 6));
        if (maxw >= 24) {
            const int rw = qMin(qFontWidth(sfm, fmtCount(c.replyCount)
                                          + qFromUtf8("评")) + 12, maxw);
            if (replyRect) *replyRect = QRect(bx, by, rw, bh);
        }
    }
}

int ContentView::badgeAt(const QPoint& p) const
{
    if (m_mode != VideoGridMode) return 0;
    for (int i = 0; i < (int)m_rows.size(); ++i) {
        QRect vr, rr;
        statBadgeRects(i, &vr, &rr);
        if (rr.isValid() && rr.contains(p)) return 2;   /* 评论优先 */
        if (vr.isValid() && vr.contains(p)) return 1;
    }
    return 0;
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
    cancelTip();                 /* 滚动后卡片位置变化 */
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
    cancelTip();                 /* 列数/卡片宽度变化,矩形失效 */
    QWidget::resizeEvent(e);
    if (m_scrollBar)
        m_scrollBar->setGeometry(width() - 12, m_headerH, 12, qMax(0, height() - m_headerH));
    if (m_mode == CategoryGridMode || m_mode == VideoGridMode)
        rebuildRows();
    updateScrollBar();
}

void ContentView::wheelEvent(QWheelEvent* e)
{
    cancelTip();                 /* 滚轮滚动:旧锚点失效 */
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
        p.drawText(QRect(20, 4, width() - 180, 30), Qt::AlignVCenter | Qt::AlignLeft, title);

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
        p.drawText(QRect(20, 4, width() - 260, 30), Qt::AlignVCenter | Qt::AlignLeft, m_listTitle);

        QRect br = backRect();
        p.setPen(Qt::NoPen);
        p.setBrush(pal.hoverBg);
        muiDrawRoundRect(p, br, br.height() / 2, br.height() / 2);
        p.setPen(pal.textPrimary);
        p.setFont(small);
        p.drawText(br, Qt::AlignCenter, QString::fromUtf8("← ") + m_back);
    }

    if (m_mode == VideoGridMode && m_gridPage > 0) {
        p.setFont(small);
        p.setPen(pal.textMuted);
        p.drawText(QRect(width() - 260, 4, 84, 30),
                   Qt::AlignVCenter | Qt::AlignRight,
                   qFromUtf8("第 %1 页").arg(m_gridPage));
    }

    /* 所有列表模式右上角:刷新按钮(在返回按钮左侧,两种模式通用) */
    {
        QRect rf = refreshRect();
        p.setPen(Qt::NoPen);
        p.setBrush(pal.hoverBg);
        muiDrawRoundRect(p, rf, rf.height() / 2, rf.height() / 2);
        p.setPen(pal.textPrimary);
        p.setFont(small);
        p.drawText(rf, Qt::AlignCenter, m_refresh);
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

            /* 封面左下角:播放数(点击无动作) + 评论数(点击开评论窗口) */
            QRect vr, rr;
            statBadgeRects((int)i, &vr, &rr);
            p.setFont(small);
            if (vr.isValid()) drawStatBadge(p, vr, fmtCount(c.playCount));
            if (rr.isValid())
                drawStatBadge(p, rr, fmtCount(c.replyCount) + qFromUtf8("评"));

            p.setFont(bold);
            p.setPen(pal.textPrimary);
            QRect titleRect(cr.x(), cr.y() + coverH + 4, cr.width(), 18);
            int titleLeft = cr.x();
            if (!m_playingVideoId.isEmpty() && c.id == m_playingVideoId) {
                /* 当前播放:自绘向右三角(不依赖 ▶ 字形与字体行高) */
                drawPlayTriangle(p, QRect(cr.x() + 2, titleRect.y() + 2, 12, 14),
                                 pal.accent);
                titleLeft += 18;
            }
            p.drawText(QRect(titleLeft, titleRect.y(),
                             titleRect.width() - (titleLeft - cr.x()),
                             titleRect.height()),
                       Qt::AlignVCenter | Qt::AlignLeft,
                       elideOneLine(p, c.title, titleRect.width() - (titleLeft - cr.x())));
            p.setFont(small);
            p.setPen(pal.textMuted);
            QString sub = c.author;
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
    cancelTip();  /* 点击即收标题提示 */
    setFocus();   /* 点内容区 = 退出文本输入焦点,启用播放器快捷键 */
    /* M7:右键交给 contextMenuEvent,不能顺带激活条目 */
    if (e->button() != Qt::LeftButton)
        return;
    if (m_mode == PlaylistListMode || m_mode == CategoryGridMode
        || m_mode == VideoGridMode) {
        if (refreshRect().contains(e->pos())) {
            emit refreshRequested();
            return;
        }
        if (backRect().contains(e->pos())) {
            emit backRequested();
            return;
        }
    }
    if (m_mode == VideoGridMode) {
        if (!m_gridErr.isEmpty()) {
            QRect rr = retryRect();
            rr.moveTop(rr.top() - m_scrollY);   /* 与绘制同一 viewport 坐标 */
            if (rr.contains(e->pos())) {
                emit loadMoreRequested();       /* 重试 = 重拉当前页 */
                return;
            }
        }
        if (m_hasMore && !m_loadingNow) {
            QRect lr = loadMoreRect();
            lr.moveTop(lr.top() - m_scrollY);
            if (lr.contains(e->pos())) {
                emit loadMoreRequested();
                return;
            }
        }
        const int badge = badgeAt(e->pos());
        if (badge == 2) {                    /* 评论数徽标 → 评论窗口 */
            const int idx = rowAt(e->pos());
            if (idx >= 0) emit commentBadgeClicked(m_videos[idx].id);
            return;
        }
        if (badge == 1) return;              /* 播放数徽标 → 点击无动作 */
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
    if (refreshRect().contains(e->pos())) {
        emit refreshRequested();
        return;
    }
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
    if (m_mode != VideoGridMode)
        cancelTip();             /* 其它模式/切换后不保留提示 */
    if (m_mode == VideoGridMode) {
        int h = rowAt(e->pos());
        if (h < 0 && !m_gridErr.isEmpty()) {
            QRect rr = retryRect();
            rr.moveTop(rr.top() - m_scrollY);
            if (rr.contains(e->pos())) {
                h = 3000000;
            }
        }
        if (h < 0 && m_hasMore && !m_loadingNow) {
            QRect lr = loadMoreRect();
            lr.moveTop(lr.top() - m_scrollY);
            if (lr.contains(e->pos())) {
                h = 3000001;
            }
        }
        if (h != m_hover) { m_hover = h; update(); }
        updateVideoTip(e->pos());
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
    cancelTip();
    QWidget::leaveEvent(e);
    if (m_hover != -1) {
        m_hover = -1;
        update();
    }
}

void ContentView::hideEvent(QHideEvent* e)
{
    cancelTip();                 /* 切到播放页/窗口隐藏,别把提示留在屏幕上 */
    QWidget::hideEvent(e);
}

/* 卡片标题矩形(与 paint 的 titleRect/titleLeft 完全一致) */
QRect ContentView::videoTitleRect(int rowPos) const
{
    if (rowPos < 0 || rowPos >= (int)m_rows.size())
        return QRect();
    const Row& row = m_rows[rowPos];
    if (row.index < 0 || row.index >= (int)m_videos.size())
        return QRect();
    QRect cr = cardRect(rowPos);
    cr.moveTop(row.y - m_scrollY);
    const int coverH = cr.width() * 9 / 16;
    int left = cr.x();
    const VideoCard& c = m_videos[row.index];
    if (!m_playingVideoId.isEmpty() && c.id == m_playingVideoId)
        left += 18;              /* 与 paint 的 ▶ 偏移一致 */
    return QRect(left, cr.y() + coverH + 4, cr.right() - left + 1, 18);
}

void ContentView::cancelTip()
{
    if (m_tipTimer != 0)
        m_tipTimer->stop();
    if (m_tipRow == -1)
        return;
    m_tipRow = -1;
    showTempTooltip(this, QRect(), QString());   /* 空文本 = 隐藏 */
}

void ContentView::dismissTip()
{
    cancelTip();
}

void ContentView::updateVideoTip(const QPoint& pos)
{
    /* 命中卡片 -> 必须落在标题行内才提示(封面/作者/徽标不算) */
    int rowPos = -1;
    for (size_t k = 0; k < m_rows.size(); ++k) {
        QRect cr = cardRect((int)k);
        cr.moveTop(m_rows[k].y - m_scrollY);
        if (cr.contains(pos)) { rowPos = (int)k; break; }
    }
    if (rowPos >= 0 && !videoTitleRect(rowPos).contains(pos))
        rowPos = -1;
    if (rowPos == m_tipRow)
        return;                  /* 同一目标:延时中/已显示,不重置 */
    cancelTip();                 /* 目标变化:先收旧提示 */
    if (rowPos < 0)
        return;
    const VideoCard& c = m_videos[m_rows[rowPos].index];
    QFont tf = font();
    tf.setBold(true);            /* 与 paint 标题字体一致 */
    if (qFontWidth(QFontMetrics(tf), c.title) <= videoTitleRect(rowPos).width())
        return;                  /* 未截断:不提示 */
    m_tipRow = rowPos;
    m_tipTimer->start(500);      /* 延时显示 */
}

void ContentView::onTipTimer()
{
    m_tipTimer->stop();          /* Qt3 无 setSingleShot:到点手动停 */
    if (m_tipRow < 0 || m_tipRow >= (int)m_rows.size())
        return;
    if (m_mode != VideoGridMode) { m_tipRow = -1; return; }
    const Row& row = m_rows[m_tipRow];
    if (row.index < 0 || row.index >= (int)m_videos.size()) { m_tipRow = -1; return; }
    const VideoCard& c = m_videos[row.index];
    const QRect r = videoTitleRect(m_tipRow);
    QFont tf = font();
    tf.setBold(true);
    if (qFontWidth(QFontMetrics(tf), c.title) <= r.width()) {
        m_tipRow = -1;
        return;
    }
    /* 60s 兜底,实际由移出/滚动/缩放/点击/Esc/隐藏主动关闭 */
    showTempTooltip(this, r, c.title, 60000);
}
