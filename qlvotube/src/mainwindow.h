#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "compat34.h"
#include "appearance.h"
#include "musicmodel.h"
#include "contentview.h"
#include "ConfigDialog.h"   /* SettingsChangedMap 类型随 Q_OBJECT 槽声明完整 */
#include <qmainwindow.h>
#include <qwidget.h>
#include <qlabel.h>
#include <qpushbutton.h>
#include <vector>

class FramelessHelper;
class CustomTitleBar;
class SystemTrayIcon;
class SideNav;
class ContentView;
class QueuePanel;
class PlayerBar;
class CXVideoPlayer;
class CXRendererGl;
class VideoPane;
class CXVideoEqDialog;
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
    void onPlayPause();
    void onPrev();
    void onNext();

    /* 播放引擎接线(cxvideo/qtav34;backend-abstraction.md Phase 1) */
    void onPlayerStarted();
    void switchToPlayerView(bool on);    /* M5-A 播放页切换 */
    void onVideoBackRequested();
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
    void onPlayerError(int code, const QString& msg);
    void onSeekRequested(int ms); // 与 PlayerBar::seekRequested(int) 同型

private:
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
    CXRendererGl* m_glRenderer;
    CXVideoEqDialog* m_eqDialog;
    QString m_curUrl;        /* 当前播放源(引擎切换后重开) */
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
