#ifndef CONTENTVIEW_H
#define CONTENTVIEW_H

#include "qlcompat34.h"
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
    qint64 replyCount;     /* 评论数(0=未知/该源未提供) */
    qint64 publishMs;      /* M7 */
    QString webUrl;        /* M7 */
    VideoCard() : durationMs(0), playCount(0), replyCount(0), publishMs(0) {}
};

/* 分类项(仅数据载体;渲染在 SideNav 左侧栏) */
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
    bool isVideoGridMode() const;   // 供 MainWindow 刷新时分流
    void setPlayingVideoId(const QString& id);  // 当前播放视频(列表 ▶ 标识)
    void setVideoGridPage(int page);            // 页码显示(0=不显示)

    /* PlaylistListMode：远端歌单列表（热门歌单/分类列表） */
    void showPlaylistList(const QString& title, const std::vector<RemotePlaylistCard>& cards,
                          bool hasMore, bool loading);
    void setListLoading(bool loading);

    /* CategoryGridMode：远端分类宫格 */
    void showCategoryGrid(const QStringList& cats);

    /* VideoGridMode：封面网格 + 加载更多/触底翻页 + 错误条
     * (分类选择在 SideNav 左侧栏,本视图不再画 chips 行) */
    void showVideoGrid(const QString& title);
    void appendVideoCards(const std::vector<VideoCard>& cards, bool hasMore);
    void replaceVideoCards(const std::vector<VideoCard>& cards, bool hasMore);
    void clearVideoCards();
    void setVideoGridLoading(bool loading);
    void setVideoGridError(const QString& text);   // 空=清错
    void setVideoGridTitle(const QString& title);   /* 只更新视频列表页页头标题 */
    void dismissTip();                              /* Esc/外部主动收起标题提示 */

public slots:
    /* TracksMode：本地库歌单（SideNav::playlistSelected 连接目标） */
    void showPlaylist(int playlistId);

    void onScroll(int value);

private slots:
    void onTipTimer();          // 500ms 到点显示标题提示

signals:
    void trackActivated(int playlistId, int trackIndex);
    void playlistActivated(qint64 remotePlaylistId);
    void categoryActivated(const QString& cat);
    void loadMoreRequested();
    void refreshRequested();
    void backRequested();
    void videoActivated(const QString& id);
    void videoContextMenu(const QString& id, const QPoint& globalPos);  /* M7 */
    void commentBadgeClicked(const QString& id);   /* 封面评论数徽标点击 → 评论窗口 */

protected:
    void paintEvent(QPaintEvent* e);
    void mousePressEvent(QMouseEvent* e);
    void contextMenuEvent(QContextMenuEvent* e);   /* M7 */
    void mouseMoveEvent(QMouseEvent* e);
    void wheelEvent(QWheelEvent* e);
    void resizeEvent(QResizeEvent* e);
    void leaveEvent(QEvent* e);
    void hideEvent(QHideEvent* e);          // 切到播放页/隐藏时收提示
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
    QRect refreshRect() const;
    QRect loadMoreRect() const;
    QRect retryRect() const;
    QRect cardRect(int i) const;
    void statBadgeRects(int i, QRect* viewRect, QRect* replyRect) const;  /* 封面左下角两徽标(绘制与命中共用) */
    int  badgeAt(const QPoint& p) const;   /* 0=无 1=播放 2=评论 */
    void cancelTip();                      // 停表 + 隐藏 + 清目标
    QRect videoTitleRect(int rowPos) const;// 卡片标题矩形(与 paint 同公式)
    void updateVideoTip(const QPoint& pos);// 命中/截断判断 + 起延时
    int    gridCols() const;
    int    gridCardH() const;

    std::vector<Playlist> m_lib;
    const Playlist* m_pl;
    ViewMode m_mode;
    std::vector<Row> m_rows;
    std::vector<RemotePlaylistCard> m_cards;
    std::vector<VideoCard> m_videos;
    QString m_gridErr;
    void maybeLoadMore();  /* 触底翻页(滚轮/拖条共用) */
    int m_lastMoreAt;      /* 触底翻页节流(time(0),1s) */
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
    QString m_refresh;
    QString m_loadMore;
    QString m_loading;
    bool m_hasMore;
    bool m_loadingNow;
    QString m_playingVideoId;   // 当前播放视频 id(空=无)
    int m_gridPage;             // 视频列表当前页(0=未知)
    /* 主视频网格标题 Tooltip:-1=无,>=0=m_rows 下标;timer 活动=延时中 */
    int m_tipRow;
    QTimer* m_tipTimer;
};

#endif // CONTENTVIEW_H
