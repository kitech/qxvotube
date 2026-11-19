#ifndef SIDENAV_H
#define SIDENAV_H

#include "qlcompat34.h"
#include "musicmodel.h"
#include "LimeScrollBar.h"
#include <qwidget.h>
#include <qstringlist.h>

class SearchLineEdit;
class UserInfoPanel;
class QComboBox;
class QLabel;

class SideNav : public QWidget {
    Q_OBJECT
public:
    explicit SideNav(QWidget* parent = 0);
    void setLibrary(const std::vector<Playlist>& lib);
    void setCurrentPlaylist(int playlistId);
    void setCurrentCategory(const QString& key);
    void retranslateUi();

    void setCategories(const QStringList& hot, const QStringList& groupNames,
                       const QStringList& groupCounts, const QStringList& categoryMatrix);

    /* 左上角用户区（搜索框上方两行块）；Phase 2 登录成功后调用 */
    void setUserLoggedIn(const QString& name, const QString& avatarSeed);
    UserInfoPanel* userPanel() { return m_user; }

    /* 视频站点（用户区下、搜索框上）。ids 为空 → 整行隐藏（Qt3/Qt4 stub）。
     * 显示名走 i18n "site.<id>"；缺译回退 id 原文。 */
    void setVideoSites(const QStringList& ids);
    QString currentSiteId() const;

    /* 视频源动态分类(左侧栏;首个「推荐」= 空 key) */
    void setVideoCategories(const QStringList& names, const QStringList& keys,
                            const QString& currentKey);
    void clearVideoCategories();
    void setCurrentVideoCategory(const QString& key);

public slots:
    void onNavScroll(int v);
    void onSiteChanged(int index);
    void onSearchSubmitted(const QString& text);

signals:
    void playlistSelected(int playlistId);
    void categorySelected(const QString& cat);
    void actionTriggered(int actionId);
    void loginRequested();
    void siteChanged(const QString& providerId);
    void videoCategorySelected(const QString& key);
    void searchSubmitted(const QString& kw);

protected:
    void paintEvent(QPaintEvent* e);
    void mousePressEvent(QMouseEvent* e);
    void mouseMoveEvent(QMouseEvent* e);
    void leaveEvent(QEvent* e);
    void resizeEvent(QResizeEvent* e);
    void wheelEvent(QWheelEvent* e);

private:
    enum NavKind {
        NavHeader,      /* 分组标题（不可点） */
        NavPlaylist,    /* 本地歌单（id = playlist id） */
        NavCategory,    /* 远端歌单分类（cat = 分类名） */
        NavHotPlaylists,/* 推荐 → 热门歌单 */
        NavRank,        /* 排行榜 → 暂缓提示 */
        NavMyMusic,     /* 我的音乐 → 需登录提示 */
        NavVideoCategory, /* 视频源动态分类(key 存在 cat 字段) */
    };
    struct NavItem {
        NavKind kind;
        int id;
        QString text;
        QString cat;
        int y;
        int h;
    };
    void relayout();
    void applyScrollBar();
    int itemAt(const QPoint& p) const;
    bool canScroll() const { return m_contentH > height() - 4; }
    void clampScroll() { if (m_contentH < height() - 4) m_scroll = 0; }

    SearchLineEdit* m_search;
    UserInfoPanel* m_user;
    QLabel* m_siteLabel;      /* "视频站点" */
    QComboBox* m_site;
    QStringList m_siteIds;    /* 索引 → provider id（不按文本匹配） */
    bool m_hasSite;           /* 显式标志:构造期 isVisible() 还不可靠 */
    LimeScrollBar* m_scrollBar;
    int m_scroll;
    int m_contentH;
    std::vector<Playlist> m_lib;
    std::vector<NavItem> m_items;
    int m_current;
    int m_hover;
    QString m_currentCat;   /* 当前分类 key（kHotViewKey=热门歌单视图） */
    QString m_navRecommend;
    QString m_navRank;
    QString m_navMine;
    QString m_groupCreated;
    QString m_groupCollected;
    QString m_groupCategories;
    QStringList m_hotCats;
    QStringList m_catGroups;      /* 组名（按序） */
    QStringList m_catGroupCounts; /* 与组名对应：该组分类个数 */
    QStringList m_catMatrix;      /* 与组名对应：该组全部分类名 */
    QStringList m_videoCatNames;
    QStringList m_videoCatKeys;
    QString m_groupVideoCats;
    QString m_curVideoCat;
};

#endif // SIDENAV_H
