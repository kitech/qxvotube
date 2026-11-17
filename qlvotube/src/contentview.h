#ifndef CONTENTVIEW_H
#define CONTENTVIEW_H

#include "compat34.h"
#include "musicmodel.h"
#include <qwidget.h>
#include <qrect.h>
#include <qstringlist.h>

class LimeScrollBar;

/* 视频网格卡片(providers VideoItem 的 UI 投影;空字段 UI 侧不绘制) */
struct VideoCard {
    QString id, title, author, cover;
    qint64 durationMs;
    qint64 playCount;
    qint64 publishMs;      /* M7 */
    QString webUrl;        /* M7 */
    VideoCard() : durationMs(0), playCount(0), publishMs(0) {}
};

/* 分类 chip(key 由站点动态提供;空 key = 推荐) */
struct CategoryChip {
    QString key, name;
    CategoryChip() {}
    CategoryChip(const QString& k, const QString& n) : key(k), name(n) {}
};

/* 远端歌单卡片，PlaylistListMode 数据 */
struct RemotePlaylistCard {
    qint64 id;
    QString name;
    QString creator;
    QString coverUrl;
    int trackCount;
    qint64 playCount;
    RemotePlaylistCard() : id(0), trackCount(0), playCount(0) {}
};

class ContentView : public QWidget {
    Q_OBJECT
public:
    explicit ContentView(QWidget* parent = 0);
    void setLibrary(const std::vector<Playlist>& lib);
    void retranslateUi();

    /* PlaylistListMode：远端歌单列表（热门歌单/分类列表） */
    void showPlaylistList(const QString& title, const std::vector<RemotePlaylistCard>& cards,
                          bool hasMore, bool loading);
    void setListLoading(bool loading);

    /* CategoryGridMode：远端分类宫格 */
    void showCategoryGrid(const QStringList& cats);

    /* VideoGridMode：分类 chips + 封面网格 + 加载更多/触底翻页 + 错误条 */
    void showVideoGrid(const QString& title, const std::vector<CategoryChip>& chips,
                       const QString& activeKey);
    void setVideoChips(const std::vector<CategoryChip>& chips,
                       const QString& activeKey);
    void setActiveChip(const QString& key);   /* 仅切高亮，不动列表 */
    void appendVideoCards(const std::vector<VideoCard>& cards, bool hasMore);
    void replaceVideoCards(const std::vector<VideoCard>& cards, bool hasMore);
    void clearVideoCards();
    void setVideoGridLoading(bool loading);
    void setVideoGridError(const QString& text);   // 空=清错

public slots:
    /* TracksMode：本地库歌单（SideNav::playlistSelected 连接目标） */
    void showPlaylist(int playlistId);

    void onScroll(int value);

signals:
    void trackActivated(int playlistId, int trackIndex);
    void playlistActivated(qint64 remotePlaylistId);
    void categoryActivated(const QString& cat);
    void loadMoreRequested();
    void backRequested();
    void chipActivated(const QString& key);
    void videoActivated(const QString& id);
    void videoContextMenu(const QString& id, const QPoint& globalPos);  /* M7 */

protected:
    void paintEvent(QPaintEvent* e);
    void mousePressEvent(QMouseEvent* e);
    void contextMenuEvent(QContextMenuEvent* e);   /* M7 */
    void mouseMoveEvent(QMouseEvent* e);
    void wheelEvent(QWheelEvent* e);
    void resizeEvent(QResizeEvent* e);
    void leaveEvent(QEvent* e);
    void customEvent(CustomEventBase* e);   // 封面解码完成(泵线程 → GUI)

private:
    enum ViewMode { TracksMode, PlaylistListMode, CategoryGridMode, VideoGridMode };

    struct Row {
        int index;      /* TracksMode: trackIndex; List/Grid: 卡片/分类下标 */
        int y;
        int h;
    };

    void rebuildRows();
    void updateScrollBar();
    int rowAt(const QPoint& p) const;
    int totalHeight() const;
    QRect playAllRect() const;
    QRect backRect() const;
    QRect loadMoreRect() const;
    QRect retryRect() const;
    QRect chipRect(int i) const;
    int   chipContentWidth() const;
    QRect cardRect(int i) const;
    int    gridCols() const;
    int    gridCardH() const;

    std::vector<Playlist> m_lib;
    const Playlist* m_pl;
    ViewMode m_mode;
    std::vector<Row> m_rows;
    std::vector<RemotePlaylistCard> m_cards;
    std::vector<VideoCard> m_videos;
    std::vector<CategoryChip> m_chips;
    QString m_activeChip;
    QString m_gridErr;
    void maybeLoadMore();  /* 触底翻页(滚轮/拖条共用) */
    int m_lastMoreAt;      /* 触底翻页节流(time(0),1s) */
    int m_chipScroll;      /* chips 横向偏移(滚轮横向滚动) */
    QStringList m_gridCats;
    LimeScrollBar* m_scrollBar;
    int m_scrollY;
    int m_scrollDelta;
    int m_hover;
    int m_current;
    int m_playlistId;
    int m_headerH;
    int m_rowH;
    QString m_playAll;
    QString m_listTitle;
    QString m_back;
    QString m_loadMore;
    QString m_loading;
    bool m_hasMore;
    bool m_loadingNow;
};

#endif // CONTENTVIEW_H
