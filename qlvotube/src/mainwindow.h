#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "qlcompat34.h"
#include "appearance.h"
#include "musicmodel.h"
#include "contentview.h"
#include "ConfigDialog.h"   /* SettingsChangedMap 类型随 Q_OBJECT 槽声明完整 */
#include "providers/common/provider.h"
#include "cxengine.h"          /* M7:CxVideoInfo */
#include "videoui.h"
#include <qmainwindow.h>
#include <qwidget.h>
#include <qlabel.h>
#include <qpushbutton.h>
#include <vector>
#include <qmap.h>
#include <qlist.h>

class FramelessHelper;
class CustomTitleBar;
class SystemTrayIcon;
class SideNav;
class ContentView;
class QueuePanel;
class PlayerBar;
class CXVideoPlayer;
class CXVideoRenderer;
class CXRendererGl;
class VideoPane;
class CXVideoEqDialog;
class CommentDialog;
class SubtitleDialog;
class QSplitter;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget* parent = 0);
    virtual ~MainWindow();
    /* 二期同步:argv 位置参数=多文件导入(优先于 QLVOTUBE_AUTOPLAY) */
    void setAutoplayFiles(const QStringList& files);
    /* 启动入口:有文件列表则起播首项(由 main() 在 show 后调用) */
    void startAutoplayFiles();

protected:
    bool eventFilter(QObject* o, QEvent* e);
    void showEvent(QShowEvent* e);
    virtual void closeEvent(QCloseEvent* event);
    virtual void customEvent(CustomEventBase* event);
    virtual void dragEnterEvent(QDragEnterEvent* event); /* 拖拽导入视频 */
    virtual void dropEvent(QDropEvent* event);

private slots:
    void onTitleUilang(int index);
    void onTitleStyle(int index);
    void onTitleDark(bool on);
    void onDarkMenuItem();
    void retranslateUi();
    void trayActivated(int reason);
    void trayShowMainWindow();
    void quitApp();
    void onMenu1Stub();
    void onDemoStatusInfo();
    void onDemoStatusWarning();
    void onDemoStatusError();
    void onDemoStatusClear();
    void onDemoTrayBubble();
    void onDemoToggleStatusWidgets();
    void onDemoApiRequest();
    void onAboutApp();
    void onTrackActivated(int playlistId, int trackIndex);
    void openSettings();
    void onSettingsSaved(const SettingsChangedMap& changed);
    void onUserLoginPlaceholder();
    void onNavAction(int action);
    /* 视频源(providers):切站点 / 切分类 / 点卡片 / 翻页 */
    void onSiteChanged(const QString& providerId);
    void onVideoCategorySelected(const QString& key);
    void onNetTvRegion(const QString& region);   /* 网络电视地区切换 */
    void onRepeatClicked();
    void onShuffleClicked();
    void onAutoSkipClicked();
    void onDownloadCurrent();
    void onVideoActivated(const QString& id);
    void onVideoContextMenu(const QString& id, const QPoint& globalPos);  /* M7 */
    void openComments(const QString& id);
    void requestComments(const QString& cursor);
    void onCommentLoadMore();
    /* 字幕(YouTube via yt-dlp;播放时自动拉,底部一行 + 点击打开列表) */
    void requestSubtitles(const QString& id);
    void openSubtitleList();
    void onPlayerContextMenu(const QPoint& globalPos);                    /* M7 */
    void applyVideoInfo();                                                /* M7 */
    void onGridLoadMore();
    void onSearchSubmitted(const QString& kw);
    void onListRefresh();        // 列表右上角刷新按钮
    void onPlayPause();
    void onPrev();
    void onNext();

    /* 播放引擎接线(cxvideo/qtav34;backend-abstraction.md Phase 1) */
    void onPlayerStarted();
    void switchToPlayerView(bool on);    /* M5-A 播放页切换 */
    void onVideoBackRequested();
    void onVideoSurfaceRequested();  /* 点 playbar 封面/标题 → 回播放页 */
    void onQaAutoplay();     /* QA 钩子:QLVOTUBE_AUTOPLAY=<url> 自动起播 */
    void startQaTimeline();  /* 二期:QLVOTUBE_QA=1 六步自检调度 */
    /* 二期同步:QLVOTUBE_QA=1 六步自检(与 qskvotube 同格式 qa: step=) */
    void onQaStepPause();
    void onQaStepPlay();
    void onQaStepSeek();
    void onQaStepRate();
    void onQaStepCc();
    void onQaStepEq();
    void onQaStepShot();
    /* M5-A2:音量/倍速/EQ/截图/引擎切换 */
    void onVolumeChanged(int pct);
    void onRateChanged(double r);
    void onMuteChanged(bool on);   /* 二期同步:静音切换 */
    void onOpenEqDialog();
    void onEqChanged();
    void onTakeScreenshot();
    void onToggleEngine();
    /* M5-A3:字幕(轨道循环 + 外挂) */
    void onCycleSubtitle();
    void onLoadSubtitleFile();
    void onSubtitleText(const QString& text);
    void onQaAutoSub();      /* QA:QLVOTUBE_AUTOSUB=1 自动选字幕轨0 */
    void onPlayerPaused();
    void onPlayerStopped();
    void onPlayerFinished();
    void onPlayerPosition(long ms);
    void onPlayerDuration(long ms);
    void onRefreshBuffered();    // 独立定时器:缓冲进度与播放进度分开刷新
    void onPlayerError(int code, const QString& msg);
    void onSeekRequested(int ms); // 与 PlayerBar::seekRequested(int) 同型

private:
    /* 快捷键(YouTube/MPV 惯例;搜索框聚焦时自动让行) */
    bool shortcutsDisabledByFocus() const;
    void seekBy(long deltaMs);
    void adjustVolume(int deltaPct);
    void toggleMuteShortcut();
    void adjustRate(double delta);
    void toggleFullscreen();
    void toggleVideoFullscreen();   /* F:视频全屏(仅 videoPane 提升为顶层) */
    void showShortcutHelp();
    /* 播放模式/连播(qlmusic 语义) */
    void advanceNext(bool fromAuto);
    /* 失败自动跳过(qlmusic skipOnError):连续失败止损 */
    bool skipOnError(const QString& why);
    enum { kErrSkipStopAt = 99 };
    void saveQueueSnapshot();
    void restoreQueueSnapshot();
    QRect m_normGeom;
    bool m_fullscreen = false;
    bool m_videoFullscreen = false;  /* 视频全屏态(与 m_fullscreen 互斥) */
    long m_durMs = 0;   /* 引擎时长(ffmpeg 的 duration() 可能返回 0) */
    int m_repeatMode = 0;          /* 0 off 1 all 2 one */
    bool m_shuffle = false;
    bool m_autoSkip = true;
    int m_errSkipRun = 0;
    QString m_skipGuardId;
    class Downloader* m_dl = nullptr;
    void* m_bufTimer = nullptr;  // QTimer*:250ms 独立刷新缓冲进度
    bool m_playFinished = false; // 当前项自然播完且未被自动切歌覆盖
    int m_pendingSeekMs = -1;    // 播完后拖进度:待跳转位置(ms),-1=无

    struct MenuItemRef {
        const char* key;
        bool darkToggle;
        void* owner;
#ifdef QT3_BUILD
        int id;
#else
        void* action;
#endif
        MenuItemRef() : key(0), darkToggle(false), owner(0)
#ifdef QT3_BUILD
            , id(-1)
#else
            , action(0)
#endif
        {}
    };

    void buildStatusBar();
    void buildCentralWidget();
    void buildAppearancePanel();
    void buildMenus();
    void buildAppearanceMenus();
    void buildTray();

    void setupVideoProviders();
    void loadVideoPage(int page);
    void playVideoAt(int index);
    void replayCurrent();        // 按当前模式重播视频/argv/曲目(从头)
    void enterVideoMode(int index);      /* 网格 → 播放队列 */
    void showVideoError(const QString& where, const QString& serverMsg,
                        qxv::Err code);
    void showTrackStub(int playlistId, int trackIndex);
    void tryPlayTrack(const Track& t);
    void ensurePlayerReady();
    void playArgvAt(int idx);   /* 二期:argv 列表切歌(回绕/播完停) */

    CXVideoPlayer* m_player;  /* 布局时段内建(CXVideoPlayer 见 cxvideo 说明) */

    void addMenuItem(void* menu, const QObject* receiver, const char* slot,
                     const char* key, bool darkToggle = false);
    void* addTopMenu(const char* key);
    void* addSubMenu(void* parentMenu, const QString& title, const char* key);
    void addMenuSeparator(void* menu);
    void refreshMenuTexts();
    void setTopMenuText(int index, const QString& text);
    void refreshAppearanceMenuTexts();
    void updateAppearanceTooltips();
    void updateAppearanceMenuChecks();

    void addDemoWidgets();
    void removeDemoWidgets();
    bool hasDemoWidgets() const;

    FramelessHelper* framelessHelper;
    CustomTitleBar* titleBar;
    SystemTrayIcon* tray;
    SideNav* sideNav;
    ContentView* contentView;
    QueuePanel* queuePanel;
    PlayerBar* playerBar;
    QSplitter* bodySplitter;
    StackedWidget* centerStack;   /* 列表页/播放页切换(M5-A;compat34 别名) */
    VideoPane* videoPane;
    QWidget*         m_rendererWidget;   /* 输出后端控件(auto = 无 GL 优先) */
    CXVideoRenderer* m_videoRenderer;    /* 交给 CXVideoPlayer */
    CXVideoEqDialog* m_eqDialog;
    QString m_curUrl;        /* 当前播放源(引擎切换后重开) */
    /* 视频源网格状态 */
    QList36<qxv::VideoProvider*> m_providers;
    qxv::VideoProvider* m_curProv;
    qxv::ProxyConfig m_proxyCfg;           /* 供 tvlist 频道 mustProxy 解析 */
    QString m_netTvRegion;                 /* 当前网络电视地区 key(空=非电视分类) */
    /* 评论窗口状态 */
    CommentDialog* m_commentDlg;          /* 懒创建 */
    qxv::VideoProvider* m_commentProv;    /* 评论请求所用源 */
    QString m_commentId;
    QString m_commentCursor;              /* 下一页游标(空=无更多) */
    /* 字幕状态 */
    SubtitleDialog* m_subDlg;             /* 懒创建 */
    QList36<qxv::SubtitleTrack> m_subtitleTracks;
    QString m_subtitleId;                 /* 当前字幕对应视频 id(防串台) */
    int m_subCueIdx;                      /* 当前高亮 cue(-1=无) */
    int m_gridPage;
    bool m_gridHasMore;
    bool m_gridLoading;
    QString m_gridChip;                    /* 当前分类 key(空=推荐) */
    CxVideoInfo m_videoInfo;      /* M7:最近一次播放信息(详情弹窗) */
    QString m_curQuality;         /* M7:实际档位 */
    QString m_curRequested;       /* M7:请求档位 */
    bool m_curDegraded;           /* M7 */
    QString m_curWebUrl;          /* M7:当前视频网页地址 */
    QMap<QString, QString> m_videoTitles;  /* 点播时补标题(不额外请求 detail) */
    int m_gridGen;                         /* 代际:切源/切分类/新请求自增 */
    QString m_catLoadSite;                 /* 分类加载标识所属站点(切源即换) */
    int m_catLoadPending;                  /* 待完成的分类回调数:categories + refresh = 2 */
    bool m_gridIsSearch;                   /* 当前网格是搜索结果 */
    QString m_gridSearchKw;
    std::vector<VideoCard> m_videoCards;   /* 当前网格全量(播放队列来源) */
    std::vector<CategoryChip> m_chipList;  /* 当前站点分类(搜索时保留可回切) */
    std::vector<QString> m_videoIds;       /* 与 m_videoQueue 平行 */
    std::vector<Track> m_videoQueue;
    int m_videoIdx;
    bool m_videoMode;                      /* true=播放/上一首/下一首走视频队列 */
    int m_subIdx;            /* 当前字幕轨枚举序(-1=关) */
    std::vector<Playlist> m_library;
    int m_curPlaylistId;
    int m_curTrackIndex;
    QStringList m_argvUrls;  /* 二期同步:argv 多文件列表(不进 m_library) */
    int m_argvIdx;           /* -1=argv 模式未起播 */

    // 演示用状态栏控件（演示菜单可反复增删）
    QLabel* demoLeftLabel;
    QPushButton* demoLeftBtn;
    QLabel* demoRightLabel;
    QPushButton* demoRightBtn;
    QLineEdit* demoEdit;

    QPushButton* m_langBtn;
    QPushButton* m_styleBtn;
    QPushButton* m_darkBtn;
    void* m_langMenu;
    void* m_styleMenu;
    void* m_darkMenu;

    std::vector<AppearanceMenuItemRef> m_appearanceItems;
    std::vector<MenuItemRef> menuItemRefs;
    std::vector<MenuItemRef> topMenuRefs;
    bool forceQuit;
};
#endif // MAINWINDOW_H
