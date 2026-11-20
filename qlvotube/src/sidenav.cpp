#include "sidenav.h"
#include "musicstyle.h"
#include "searchline.h"
#include "userinfopanel.h"
#include "translator.h"
#include "tvlist.h"

#include <qglobal.h>
#include <qpainter.h>
#include <qevent.h>
#include <qfontmetrics.h>
#if QT_VERSION >= 0x040000
#include <QDebug>
#endif
#include <qcombobox.h>
#include <qlabel.h>

#include <cstdio>
#include <cstdlib>

namespace {
const int kTop = 8;
const int kUserH = 60;   /* 用户信息块（两行：昵称 + 副信息），搜索框上方 */
const int kSiteH = 24;   /* 视频站点行（label + combo），搜索框上方 */
const int kSearchH = 28;
const int kItemH = 30;
const int kGroupH = 26;
/* 热门歌单视图的哨兵 key（与分类名不冲突） */
const char* kHotViewKey = "__hot__";

/* i18n 文本兜底：key 未命中返回中文原文，避免把翻译键画到界面上 */
QString siteText(const char* key, const char* fallback)
{
    const QString v = _(key);
    const QString k = qFromUtf8(key);
    return (v.isEmpty() || v == k) ? qFromUtf8(fallback) : v;
}

/* Qt3 的 QComboBox 用 currentItem/insertItem/setCurrentItem，
 * Qt4+ 用 currentIndex/addItem/setCurrentIndex（同名 API 在 Qt3 不存在） */
int comboIndex(QComboBox* c)
{
#ifdef QT3_BUILD
    return c->currentItem();
#else
    return c->currentIndex();
#endif
}

void comboSetIndex(QComboBox* c, int i)
{
#ifdef QT3_BUILD
    c->setCurrentItem(i);
#else
    c->setCurrentIndex(i);
#endif
}

void comboAdd(QComboBox* c, const QString& text)
{
#ifdef QT3_BUILD
    c->insertItem(text);
#else
    c->addItem(text);
#endif
}
}

SideNav::SideNav(QWidget* parent)
    : QWidget(parent), m_search(0), m_user(0), m_siteLabel(0), m_site(0),
      m_hasSite(false),
      m_scrollBar(0), m_scroll(0), m_contentH(0), m_current(-1), m_hover(-1),
      m_videoCatsLoading(false)   /* 按声明顺序置末尾 */
{
    setMouseTracking(true);
    setMinimumWidth(170);
    setMaximumWidth(280);
    m_user = new UserInfoPanel(this);
    connect(m_user, SIGNAL(loginRequested()), this, SIGNAL(loginRequested()));
    /* 视频站点 combo（参考 qxmusic/qlmusic/src/sidenav.cpp）：
     * 只连 activated(int)（Qt3/Qt4+ 都有，程序填充不触发），
     * 索引 → provider id 映射，不做显示文本匹配 */
    m_siteLabel = new QLabel(siteText("nav.site", "视频站点"), this);
    m_site = new QComboBox(this);
    m_siteLabel->hide();
    m_site->hide();
    connect(m_site, SIGNAL(activated(int)), this, SLOT(onSiteChanged(int)));
    m_search = new SearchLineEdit(this);
    connect(m_search, SIGNAL(triggered(const QString&)),
            this, SLOT(onSearchSubmitted(const QString&)));
    m_scrollBar = new LimeScrollBar(Qt::Vertical, this);
    m_scrollBar->hide();
    connect(m_scrollBar, SIGNAL(valueChanged(int)), this, SLOT(onNavScroll(int)));
    retranslateUi();
}

void SideNav::setVideoCategoriesLoading(bool on)
{
    if (m_videoCatsLoading == on) return;
    m_videoCatsLoading = on;
    update();                 /* 只重绘侧栏,不影响网络/网格 */
}

void SideNav::onSearchSubmitted(const QString& text)
{
#ifdef QT3_BUILD
    const QString kw = text.stripWhiteSpace();
#else
    const QString kw = qTrim(text);
#endif
    if (kw.isEmpty()) return;
#if QT_VERSION >= 0x060000
    qDebug() << "search submitted:" << kw;
#else
    qDebug("search submitted: %s", qToUtf8(kw).data());
#endif
    emit searchSubmitted(kw);
}

void SideNav::setVideoSites(const QStringList& ids)
{
    m_siteIds = ids;
    if (!m_site || !m_siteLabel) return;
    const bool show = !ids.isEmpty();
    if (show) m_site->clear();
    for (int i = 0; i < ids.size(); ++i) {
        const QString key = QString::fromUtf8("site.") + ids[i];
        QString label = _(qToUtf8(key).data());
        if (label.isEmpty() || label == key) label = ids[i];   /* 缺译回退 id */
        comboAdd(m_site, label);
    }
    comboSetIndex(m_site, 0);
    m_hasSite = show;
    if (show) { m_siteLabel->show(); m_site->show(); }
    else { m_siteLabel->hide(); m_site->hide(); }
    resizeEvent(0);
    update();
    if (show) emit siteChanged(ids[0]);
}

QString SideNav::currentSiteId() const
{
    if (!m_site || m_siteIds.isEmpty()) return QString();
    const int i = comboIndex(m_site);
    return (i >= 0 && i < m_siteIds.size()) ? m_siteIds[i] : QString();
}

void SideNav::onSiteChanged(int index)
{
    if (index < 0 || index >= m_siteIds.size()) return;
    const QString id = m_siteIds[index];
    if (getenv("QLVOTUBE_DEBUG") != 0)
#if QT_VERSION >= 0x060000
        qDebug() << "sidenav: site index" << index << "->" << id;
#else
        qDebug("sidenav: site index %d -> %s", index, qToUtf8(id).data());
#endif
    emit siteChanged(id);
}

void SideNav::setUserLoggedIn(const QString& name, const QString& avatarSeed)
{
    if (m_user) m_user->setLoggedIn(name, avatarSeed);
}

void SideNav::retranslateUi()
{
    m_navRecommend = _("nav.recommend");
    m_navRank = _("nav.rank");
    m_navMine = _("nav.mymusic");          /* 文案已改为「我的视频」 */
    m_navLocalVideo = siteText("nav.local_video", "本地视频");
    m_navSiteLive = siteText("nav.site_live", "站点直播");
    m_navNetTv = siteText("nav.net_tv", "网络电视");
    /* 地区子项显示名走 i18n "tv.region.<key>"；缺译回退中文 */
    m_netTvRegionNames.clear();
    {
        int n = 0;
        const char* const* regs = tvlist::regions(n);
        for (int i = 0; i < n; ++i) {
            const QString key = qFromUtf8(regs[i]);
            const QString ikey = tvlist::regionTitleKey(key);
            QString label = _(qToUtf8(ikey).data());
            if (label.isEmpty() || label == ikey) {
                if (key == qFromUtf8("china")) label = qFromUtf8("中国大陆");
                else if (key == qFromUtf8("hongkong")) label = qFromUtf8("港澳台");
                else if (key == qFromUtf8("asia")) label = qFromUtf8("亚洲");
                else if (key == qFromUtf8("international")) label = qFromUtf8("国际");
                else label = key;
            }
            /* 地区子项后缀显示频道个数,如「中国大陆 (111)」 */
            label += qFromUtf8(" (")
                  + QString::number((int)tvlist::byRegion(key).size())
                  + qFromUtf8(")");
            m_netTvRegionNames.push_back(label);
        }
    }
    m_groupCreated = _("nav.created");
    m_groupCollected = _("nav.collected");
    m_groupCategories = _("nav.categories");
    m_groupVideoCats = siteText("nav.video_categories", "视频分类");
    if (m_search)
        m_search->setPlaceholders(_("nav.search"), _("nav.search"));
    if (m_siteLabel) m_siteLabel->setText(siteText("nav.site", "视频站点"));
    if (m_site && !m_siteIds.isEmpty()) {   /* 重建显示名，保持当前索引 */
        const int cur = comboIndex(m_site);
        m_site->clear();
        for (int i = 0; i < m_siteIds.size(); ++i) {
            const QString key = QString::fromUtf8("site.") + m_siteIds[i];
            QString label = _(qToUtf8(key).data());
            if (label.isEmpty() || label == key) label = m_siteIds[i];
            comboAdd(m_site, label);
        }
        comboSetIndex(m_site, cur < 0 ? 0 : cur);
    }
    relayout();
    update();
}

void SideNav::setLibrary(const std::vector<Playlist>& lib)
{
    m_lib = lib;
    relayout();
    update();
}

void SideNav::setCurrentPlaylist(int playlistId)
{
    m_current = playlistId;
    m_currentCat = QString();
    update();
}

void SideNav::setCurrentCategory(const QString& key)
{
    m_currentCat = key;
    m_current = -1;
    update();
}

void SideNav::setCategories(const QStringList& hot, const QStringList& groupNames,
                            const QStringList& groupCounts, const QStringList& categoryMatrix)
{
    m_hotCats = hot;
    m_catGroups = groupNames;
    m_catGroupCounts = groupCounts;
    m_catMatrix = categoryMatrix;
    relayout();
    update();
}

void SideNav::setVideoCategories(const QStringList& names,
                                 const QStringList& keys,
                                 const QString& currentKey)
{
    m_videoCatNames = names;
    m_videoCatKeys = keys;
    m_curVideoCat = currentKey;
    relayout();
    update();
}

void SideNav::clearVideoCategories()
{
    m_videoCatNames.clear();
    m_videoCatKeys.clear();
    m_curVideoCat = QString();
    relayout();
    update();
}

void SideNav::setCurrentVideoCategory(const QString& key)
{
    m_curVideoCat = key;
    update();
}

void SideNav::setCurrentNetTvRegion(const QString& region)
{
    m_curNetTvRegion = region;
    update();
}

void SideNav::relayout()
{
    m_items.clear();
    m_contentH = 0;
    int y = kTop + kUserH + kSearchH + 8;

    NavItem it;
    it.kind = NavHotPlaylists; it.id = -1; it.text = m_navRecommend; it.cat = QString();
    it.y = y; it.h = kItemH; y += kItemH;
    m_items.push_back(it);
    it.kind = NavRank; it.id = -2; it.text = m_navRank; it.cat = QString();
    it.y = y; it.h = kItemH; y += kItemH;
    m_items.push_back(it);
    it.kind = NavMyMusic; it.id = -3; it.text = m_navMine; it.cat = QString();
    it.y = y; it.h = kItemH; y += kItemH;
    m_items.push_back(it);
    it.kind = NavLocalVideo; it.id = -4; it.text = m_navLocalVideo; it.cat = QString();
    it.y = y; it.h = kItemH; y += kItemH;
    m_items.push_back(it);
    it.kind = NavSiteLive; it.id = -5; it.text = m_navSiteLive; it.cat = QString();
    it.y = y; it.h = kItemH; y += kItemH;
    m_items.push_back(it);

    /* 网络电视地区子项(分组标题 + 四类地区) */
    {
        int n = 0;
        const char* const* regs = tvlist::regions(n);
        y += 8;
        it.kind = NavHeader; it.id = 0; it.text = m_navNetTv; it.cat = QString();
        it.y = y; it.h = kGroupH; y += kGroupH;
        m_items.push_back(it);
        for (int i = 0; i < n && i < (int)m_netTvRegionNames.size(); ++i) {
            it.kind = NavNetTvRegion; it.id = 0;
            it.text = m_netTvRegionNames[i]; it.cat = qFromUtf8(regs[i]);
            it.y = y; it.h = kItemH; y += kItemH;
            m_items.push_back(it);
        }
    }

    /* 视频源动态分类(站点下拉之后、音乐分类之前) */
    if (!m_videoCatKeys.isEmpty()) {
        y += 8;
        it.kind = NavHeader; it.id = 0; it.text = m_groupVideoCats;
        it.cat = QString();
        it.y = y; it.h = kGroupH; y += kGroupH;
        m_items.push_back(it);
        for (int i = 0; i < (int)m_videoCatKeys.size(); ++i) {
            it.kind = NavVideoCategory; it.id = 0;
            it.text = m_videoCatNames[i]; it.cat = m_videoCatKeys[i];
            it.y = y; it.h = kItemH; y += kItemH;
            m_items.push_back(it);
        }
    }

    if (!m_hotCats.isEmpty() || !m_catGroups.isEmpty()) {
        y += 8;
        it.kind = NavHeader; it.id = 0; it.text = m_groupCategories; it.cat = QString();
        it.y = y; it.h = kGroupH; y += kGroupH;
        m_items.push_back(it);

        /* 热门分类打平列出 */
        for (int i = 0; i < (int)m_hotCats.size(); ++i) {
            it.kind = NavCategory; it.id = 0;
            it.text = m_hotCats[i]; it.cat = m_hotCats[i];
            it.y = y; it.h = kItemH; y += kItemH;
            m_items.push_back(it);
        }

        /* 分组：groupCounts 为每组分类个数（按序），
         * categoryMatrix 为全部分类的扁平列表（顺序与分组一致） */
        int base = 0;
        for (int g = 0; g < (int)m_catGroups.size(); ++g) {
            int cnt = 0;
            if (g < (int)m_catGroupCounts.size())
                cnt = m_catGroupCounts[g].toInt();
            bool hasItems = (cnt > 0);
            if (hasItems) {
                it.kind = NavHeader; it.id = 0; it.text = m_catGroups[g]; it.cat = QString();
                it.y = y; it.h = kGroupH; y += kGroupH;
                m_items.push_back(it);
            }
            for (int c = 0; c < cnt; ++c) {
                int idx = base + c;
                if (idx >= (int)m_catMatrix.size()) break;
                it.kind = NavCategory; it.id = 0;
                it.text = m_catMatrix[idx]; it.cat = m_catMatrix[idx];
                it.y = y; it.h = kItemH; y += kItemH;
                m_items.push_back(it);
            }
            base += cnt;
        }
    }

    y += 6;
    it.kind = NavHeader; it.id = 0; it.text = m_groupCreated; it.cat = QString();
    it.y = y; it.h = kGroupH; y += kGroupH;
    m_items.push_back(it);
    for (size_t i = 0; i < m_lib.size(); ++i) {
        if (i % 2 != 0) continue;
        it.kind = NavPlaylist; it.id = m_lib[i].id; it.text = m_lib[i].name; it.cat = QString();
        it.y = y; it.h = kItemH; y += kItemH;
        m_items.push_back(it);
    }

    y += 6;
    it.kind = NavHeader; it.id = 0; it.text = m_groupCollected; it.cat = QString();
    it.y = y; it.h = kGroupH; y += kGroupH;
    m_items.push_back(it);
    for (size_t i = 0; i < m_lib.size(); ++i) {
        if (i % 2 == 0) continue;
        it.kind = NavPlaylist; it.id = m_lib[i].id; it.text = m_lib[i].name; it.cat = QString();
        it.y = y; it.h = kItemH; y += kItemH;
        m_items.push_back(it);
    }

    m_contentH = y + 8;
    applyScrollBar();
}

void SideNav::applyScrollBar()
{
    if (!m_scrollBar) return;
    int viewport = height() - 4;
    if (m_contentH <= viewport) {
        clampScroll();
        m_scrollBar->hide();
        return;
    }
    m_scrollBar->setGeometry(width() - 6, 2, 6, height() - 4);
    m_scrollBar->setRange(0, m_contentH - viewport);
    m_scrollBar->setPageStep(viewport);
#ifdef QT3_BUILD
    m_scrollBar->setLineStep(kItemH);
#else
    m_scrollBar->setSingleStep(kItemH);
#endif
    if (m_scroll > m_contentH - viewport) {
        m_scroll = m_contentH - viewport;
        m_scrollBar->blockSignals(true);
        m_scrollBar->setValue(m_scroll);
        m_scrollBar->blockSignals(false);
    }
    m_scrollBar->show();
}

void SideNav::onNavScroll(int v)
{
    m_scroll = v;
    update();
}

int SideNav::itemAt(const QPoint& p) const
{
    int yy = p.y() + m_scroll;
    for (size_t i = 0; i < m_items.size(); ++i)
        if (yy >= m_items[i].y && yy < m_items[i].y + m_items[i].h)
            return (int)i;
    return -1;
}

void SideNav::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    if (m_user)
        m_user->setGeometry(10, kTop, width() - 20, kUserH);
    const bool hasSite = m_hasSite;
    if (m_siteLabel && hasSite) {
        m_siteLabel->setGeometry(10, kTop + kUserH + 6, 56, kSiteH);
        m_site->setGeometry(68, kTop + kUserH + 4, width() - 78, kSiteH);
    }
    if (m_search)
        m_search->setGeometry(10,
            kTop + kUserH + (hasSite ? kSiteH + 4 : 4),
            width() - 20, kSearchH);
    applyScrollBar();
    update();
}

void SideNav::wheelEvent(QWheelEvent* e)
{
    if (!canScroll()) return;
    int dy = qWheelDeltaY(e);
    int steps = dy / 120;
    if (steps == 0 && dy != 0) steps = (dy > 0) ? 1 : -1;   /* 高精度触摸板 */
    if (steps == 0) return;
    int delta = steps > 0 ? -kItemH * 3 : kItemH * 3;
    int max = m_contentH - (height() - 4);
    int v = m_scroll + delta;
    if (v < 0) v = 0;
    if (v > max) v = max;
    if (v != m_scroll) {
        m_scroll = v;
        if (m_scrollBar) {
            m_scrollBar->blockSignals(true);
            m_scrollBar->setValue(v);
            m_scrollBar->blockSignals(false);
        }
        update();
    }
}

void SideNav::paintEvent(QPaintEvent*)
{
    const StyleParams::Palette& pal = muiPalette();
    QPainter p(this);
    p.fillRect(rect(), pal.baseBg);

    const int topH = kTop + kUserH
        + (m_hasSite ? kSiteH + 4 : 0) + kSearchH + 4;
    p.setClipRect(QRect(0, topH, width(), height() - topH));

    QFont normal = font();
    QFont bold = font();
    bold.setBold(true);
    QFont small = font();
    small.setPointSize(qMax(7, small.pointSize() - 2));

    int offset = -m_scroll;
    for (size_t i = 0; i < m_items.size(); ++i) {
        const NavItem& it = m_items[i];
        QRect r(0, it.y + offset, width(), it.h);
        if (it.kind == NavHeader) {
            p.setFont(small);
            p.setPen(pal.textMuted);
            const bool vcat = (it.text == m_groupVideoCats) && m_videoCatsLoading;
            p.drawText(QRect(18, r.y(), width() - 24 - (vcat ? 22 : 0), r.height()),
                       Qt::AlignVCenter | Qt::AlignLeft, it.text);
            if (vcat)
                p.drawText(QRect(width() - 24, r.y(), 18, r.height()),
                           Qt::AlignVCenter | Qt::AlignRight, qFromUtf8("…"));
            continue;
        }
        bool selected = false;
        if (it.kind == NavPlaylist)
            selected = (m_current == it.id);
        else if (it.kind == NavCategory)
            selected = (!m_currentCat.isEmpty() && m_currentCat == it.cat);
        else if (it.kind == NavHotPlaylists)
            selected = (!m_currentCat.isEmpty() && m_currentCat == QString::fromUtf8(kHotViewKey));
        else if (it.kind == NavVideoCategory)
            selected = (m_curVideoCat == it.cat);
        else if (it.kind == NavNetTvRegion)
            selected = (!m_curNetTvRegion.isEmpty() && m_curNetTvRegion == it.cat);
        bool hovered = ((int)i == m_hover);
        if (hovered)
            p.fillRect(r, pal.hoverBg);
        if (selected) {
            p.fillRect(r, pal.activeBg);
            p.fillRect(QRect(0, r.y() + 4, 3, r.height() - 8), pal.accent);
        }
        p.setFont(selected ? bold : normal);
        p.setPen(selected ? pal.accent : pal.textPrimary);
        p.drawText(QRect(20, r.y(), width() - 28, r.height()), Qt::AlignVCenter | Qt::AlignLeft, it.text);
    }
}

void SideNav::mousePressEvent(QMouseEvent* e)
{
    int idx = itemAt(e->pos());
    if (idx < 0) return;
    const NavItem& it = m_items[idx];

    if (it.kind == NavHeader) return;
    if (it.kind == NavPlaylist) {
        m_current = it.id;
        m_currentCat = QString();
        emit playlistSelected(it.id);
        update();
        return;
    }
    if (it.kind == NavCategory) {
        m_currentCat = it.cat;
        m_current = -1;
        emit categorySelected(it.cat);
        update();
        return;
    }
    if (it.kind == NavVideoCategory) {
        m_curVideoCat = it.cat;
        m_current = -1;
        m_currentCat = QString();
        emit videoCategorySelected(it.cat);
        update();
        return;
    }
    if (it.kind == NavHotPlaylists) {
        m_currentCat = QString::fromUtf8(kHotViewKey);
        m_current = -1;
        update();
        emit categorySelected(QString::fromUtf8(kHotViewKey));
        return;
    }
    if (it.kind == NavRank) {
        emit actionTriggered(0);   /* 暂缓：即将支持 */
        return;
    }
    if (it.kind == NavMyMusic) {
        emit actionTriggered(1);   /* 暂缓：需要登录提示 */
        return;
    }
    if (it.kind == NavLocalVideo) {
        emit actionTriggered(2);   /* 占位：本地视频 */
        return;
    }
    if (it.kind == NavSiteLive) {
        emit actionTriggered(3);   /* 占位：站点直播 */
        return;
    }
    if (it.kind == NavNetTvRegion) {
        m_curNetTvRegion = it.cat;
        m_current = -1;
        m_currentCat = QString();
        m_curVideoCat = QString();
        emit netTvRegionSelected(it.cat);
        update();
        return;
    }
}

void SideNav::mouseMoveEvent(QMouseEvent* e)
{
    int idx = itemAt(e->pos());
    if (idx != m_hover) {
        m_hover = idx;
        update();
    }
}

void SideNav::leaveEvent(QEvent* e)
{
    QWidget::leaveEvent(e);
    if (m_hover != -1) {
        m_hover = -1;
        update();
    }
}
