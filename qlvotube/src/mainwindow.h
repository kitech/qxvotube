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
class QSplitter;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget* parent = 0);
    virtual ~MainWindow();

protected:
    virtual void closeEvent(QCloseEvent* event);
    virtual void customEvent(CustomEventBase* event);

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
    std::vector<Playlist> m_library;
    int m_curPlaylistId;
    int m_curTrackIndex;

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
