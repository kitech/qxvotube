#include "mainwindow.h"

#include "CustomTitleBar.h"
#include "FramelessHelper.h"
#include "EmbeddedMenuBar.h"
#include "lambdaslot.h"
#include "sharedstatusbar.h"
#include "systemtrayicon.h"
#include "translator.h"
#include "ThemeManager.h"
#include "StyleParams.h"
#include "appsetup.h"
#include "config.h"
#include "globaluiutil.h"
#include "version.h"
#include "storage.h"
#include "api.h"
#include "eventpoller.h"
#include "sidenav.h"
#include "contentview.h"
#include "queuepanel.h"
#include "playerbar.h"
#include "toastwidget.h"
#include "userinfopanel.h"
#include "videopane.h"
#include "eqdialog.h"

#include "compat34.h"
#include "cxvideoplayer.h"
#include "cxengine.h"
#include "renderer_gl.h"     /* M5-A:ffmpeg 路 GL 渲染器 */
#include "appearance.h"
#include <qlabel.h>
#include <qpushbutton.h>
#include <string>

#include <qpixmap.h>
#include <qevent.h>
#include <qtimer.h>
#include <qmessagebox.h>
#include <qlineedit.h>
#include <qsplitter.h>
#include <qdatetime.h>

#include <cstdlib>
#include <fstream>

#include "app_icon.xpm"

static const char* const kTopMenuKeys[] = {
    "menu.file", "menu.edit", "menu.tool", "menu.help"
};
static const int kTopMenuKeyCount = (int)(sizeof(kTopMenuKeys) / sizeof(kTopMenuKeys[0]));

static const char* const kLangCodes[3] = { "zh-CN", "zh-TW", "en-US" };

// sticonShowStatusMessage 使用的托盘实例（MainWindow 创建/销毁时维护，不导出）
static SystemTrayIcon* s_trayIcon = 0;

static QPushButton* makeTitleMenuButton(QWidget* parent, const QString& glyph)
{
    QPushButton* b = new QPushButton(glyph, parent);
    b->setFixedSize(30, 30);
    return b;
}

static void attachTitleMenu(QPushButton* btn, MenuWidget34* menu)
{
#ifdef QT3_BUILD
    menu->setCheckable(true);
    btn->setPopup(menu);
#else
    btn->setMenu(menu);
#endif
}

static void initDemoWidgets() {
    SharedStatusBar* bar = SharedStatusBar::instance();
    QLabel* leftLabel = new QLabel(qFromUtf8("Left"), bar);
    bar->addWidget(leftLabel);
    QPushButton* leftBtn = new QPushButton(qFromUtf8("LBtn"), bar);
    leftBtn->setFixedWidth(36);
    bar->addWidget(leftBtn);
    QLabel* rightLabel = new QLabel(qFromUtf8("Right"), bar);
    bar->addPermanentWidget(rightLabel);
    QPushButton* rightBtn = new QPushButton(qFromUtf8("RBtn"), bar);
    rightBtn->setFixedWidth(36);
    bar->addPermanentWidget(rightBtn);
    QLineEdit* demoEdit = new QLineEdit(bar);
    demoEdit->setFixedWidth(100);
#ifndef QT3_BUILD
    demoEdit->setPlaceholderText(qFromUtf8("input..."));
#endif
    bar->addPermanentWidget(demoEdit);
    SharedStatusBar::instance()->showMessage(qFromUtf8("Demo: 左端 widgets 隐藏中..."), 3000);
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent
#ifdef QT3_BUILD
        , "qlvotube"
        , Qt::WType_TopLevel | Qt::WStyle_Customize | Qt::WSubWindow
              | Qt::WStyle_MinMax | Qt::WStyle_SysMenu | Qt::WStyle_NoBorder
#endif
#ifndef QT3_BUILD
        , Qt::FramelessWindowHint
#endif
    )
    , framelessHelper(0)
    , titleBar(0)
    , tray(0)
    , sideNav(0)
    , contentView(0)
    , queuePanel(0)
    , playerBar(0)
    , m_player(0)
    , bodySplitter(0)
    , centerStack(0)
    , videoPane(0)
    , m_glRenderer(0)
    , m_eqDialog(0)
    , m_curPlaylistId(0)
    , m_curTrackIndex(-1)
    , demoLeftLabel(0)
    , demoLeftBtn(0)
    , demoRightLabel(0)
    , demoRightBtn(0)
    , demoEdit(0)
    , m_langBtn(0)
    , m_styleBtn(0)
    , m_darkBtn(0)
    , m_langMenu(0)
    , m_styleMenu(0)
    , m_darkMenu(0)
    , forceQuit(false)
{
    /* Qt 抢占 locale 后回拨(mpv lib 内核硬性要求;首次触播前必须)
     * 如 main() 后置连接更宜,但确保先于引擎第一次 open 调用即可 */
    cxSetLocaleNumericC();

    // 初始化本地存储（对照 qldox mainwindow.cpp:513-521）
    QString dataDir = qGetHomePath() + "/.cache/qlvotube";
    Storage::instance().init(
#ifdef QT3_BUILD
        dataDir.utf8()
#else
        dataDir.toUtf8().constData()
#endif
    );

    setGeometry(100, 50, 880, 640);
    buildStatusBar();
    buildCentralWidget();
    buildAppearancePanel();
    buildMenus();
    buildAppearanceMenus();
    buildTray();

    /* QA 钩子(M5-A 嵌入自测):QLVOTUBE_AUTOPLAY=<url> 起播 */
    if (getenv("QLVOTUBE_AUTOPLAY") != 0)
        QTimer::singleShot(800, this, SLOT(onQaAutoplay()));
    QObject::connect(&Translator::instance(), SIGNAL(languageChanged()),
                     this, SLOT(retranslateUi()));

    // 启动事件轮询引擎（对照 qldox mainwindow.cpp:630-631）
    EventPoller::start();
    Api::setEventTarget(this);
}

MainWindow::~MainWindow() {
    s_trayIcon = 0;
    EventPoller::stop();          // 先停泵线程，再关库（qldox mainwindow.cpp:943-945 顺序）
    Storage::instance().close();
}

// 托盘气泡通知：仅显示提示气泡，不触碰 SharedStatusBar（状态栏各管各的）。
void sticonShowStatusMessage(const QString &msg, SticonIcon iconType, int timeout)
{
    if (s_trayIcon && s_trayIcon->isVisible() && SystemTrayIcon::supportsMessages())
        s_trayIcon->showMessage(msg, QString(),
            (SystemTrayIcon::MessageIcon)iconType, timeout);
}

void MainWindow::buildStatusBar()
{
    SharedStatusBar::instance()->show();
    addDemoWidgets();
}

void MainWindow::addDemoWidgets()
{
    if (hasDemoWidgets()) { return; }
    SharedStatusBar* bar = SharedStatusBar::instance();

    demoLeftLabel = new QLabel(qFromUtf8("Left"), bar);
    bar->addWidget(demoLeftLabel);

    demoLeftBtn = new QPushButton(qFromUtf8("LBtn"), bar);
    demoLeftBtn->setFixedWidth(36);
    bar->addWidget(demoLeftBtn);

    demoRightLabel = new QLabel(qFromUtf8("Right"), bar);
    bar->addPermanentWidget(demoRightLabel);

    demoRightBtn = new QPushButton(qFromUtf8("RBtn"), bar);
    demoRightBtn->setFixedWidth(36);
    bar->addPermanentWidget(demoRightBtn);

    demoEdit = new QLineEdit(bar);
    demoEdit->setFixedWidth(100);
#ifndef QT3_BUILD
    demoEdit->setPlaceholderText(qFromUtf8("input..."));
#endif
    bar->addPermanentWidget(demoEdit);

    SharedStatusBar::instance()->showMessage(
        qFromUtf8("Demo: 左端 widgets 隐藏中..."), 3000);
}

void MainWindow::removeDemoWidgets()
{
    SharedStatusBar* bar = SharedStatusBar::instance();
    if (demoLeftLabel)  { bar->removeWidget(demoLeftLabel);  delete demoLeftLabel;  demoLeftLabel = 0; }
    if (demoLeftBtn)    { bar->removeWidget(demoLeftBtn);    delete demoLeftBtn;    demoLeftBtn = 0; }
    if (demoRightLabel) { bar->removeWidget(demoRightLabel); delete demoRightLabel; demoRightLabel = 0; }
    if (demoRightBtn)   { bar->removeWidget(demoRightBtn);   delete demoRightBtn;   demoRightBtn = 0; }
    if (demoEdit)       { bar->removeWidget(demoEdit);       delete demoEdit;       demoEdit = 0; }
}

bool MainWindow::hasDemoWidgets() const
{
    return demoLeftLabel != 0;
}

void MainWindow::buildCentralWidget()
{
    QWidget* central = new QWidget(this);
    QBoxLayout* lay = qNewBoxLayout(central, QBoxLayout::TopToBottom, 0, 0);
    qSetMargins(lay, -1, 0, -1, -1);
    titleBar = new CustomTitleBar(central);
    titleBar->setLabel(_("app_title"));
    lay->addWidget(titleBar, 0);

    bodySplitter = new QSplitter(Qt::Horizontal, central);
    bodySplitter->setOpaqueResize(true);
    sideNav = new SideNav(bodySplitter);
    centerStack = new StackedWidget(bodySplitter);
    contentView = new ContentView(centerStack);
    videoPane = new VideoPane(centerStack);
    centerStack->addWidget(contentView);
    centerStack->addWidget(videoPane);
    queuePanel = new QueuePanel(bodySplitter);
    bodySplitter->addWidget(sideNav);
    bodySplitter->addWidget(centerStack);
    bodySplitter->addWidget(queuePanel);
#ifdef QT3_BUILD
    QValueList<int> sizes;
    sizes << 200 << 440 << 240;
#else
    QList<int> sizes;
    sizes << 200 << 440 << 240;
#endif
    bodySplitter->setSizes(sizes);
    lay->addWidget(bodySplitter, 1);

    playerBar = new PlayerBar(central);
    lay->addWidget(playerBar, 0);
    setCentralWidget(central);

    m_library = buildTestLibrary();
    sideNav->setLibrary(m_library);
    contentView->setLibrary(m_library);

    QObject::connect(sideNav, SIGNAL(playlistSelected(int)),
                     contentView, SLOT(showPlaylist(int)));
    QObject::connect(contentView, SIGNAL(trackActivated(int, int)),
                     this, SLOT(onTrackActivated(int, int)));

    /* 侧栏左上用户区（登录占位） */
    QObject::connect(sideNav, SIGNAL(loginRequested()),
                     this, SLOT(onUserLoginPlaceholder()));
    QObject::connect(sideNav, SIGNAL(actionTriggered(int)),
                     this, SLOT(onNavAction(int)));

    if (playerBar) {
        QObject::connect(playerBar, SIGNAL(playPauseRequested()),
                         this, SLOT(onPlayPause()));
        QObject::connect(playerBar, SIGNAL(prevRequested()),
                         this, SLOT(onPrev()));
        QObject::connect(playerBar, SIGNAL(nextRequested()),
                         this, SLOT(onNext()));
        QObject::connect(playerBar, SIGNAL(seekRequested(int)),
                         this, SLOT(onSeekRequested(int)));
        QObject::connect(playerBar, SIGNAL(volumeChanged(int)),
                         this, SLOT(onVolumeChanged(int)));
        QObject::connect(playerBar, SIGNAL(rateChanged(double)),
                         this, SLOT(onRateChanged(double)));
        QObject::connect(playerBar, SIGNAL(eqRequested()),
                         this, SLOT(onOpenEqDialog()));
        QObject::connect(playerBar, SIGNAL(shotRequested()),
                         this, SLOT(onTakeScreenshot()));
    }

    /* 播放引擎接线(掌面延迟:首次选曲时真正 setEngine) */
    m_player = new CXVideoPlayer(this);
    /* M5-A:嵌入播放页；ffmpeg 路渲染器(GL)由 M4-A 提供 */
    m_glRenderer = new CXRendererGl(videoPane->surface());
    videoPane->setRendererWidget(m_glRenderer);
    QObject::connect(videoPane, SIGNAL(backRequested()),
                     this, SLOT(onVideoBackRequested()));

    /* 引擎回执接线(进程型 cmdmpv 为 M1 主线;started/finished 已映射) */
    QObject::connect(m_player, SIGNAL(started()), this, SLOT(onPlayerStarted()));
    QObject::connect(m_player, SIGNAL(paused()), this, SLOT(onPlayerPaused()));
    QObject::connect(m_player, SIGNAL(stopped()), this, SLOT(onPlayerStopped()));
    QObject::connect(m_player, SIGNAL(finished()), this, SLOT(onPlayerFinished()));
    QObject::connect(m_player, SIGNAL(positionChanged(long)), this, SLOT(onPlayerPosition(long)));
    QObject::connect(m_player, SIGNAL(durationChanged(long)), this, SLOT(onPlayerDuration(long)));
    QObject::connect(m_player, SIGNAL(noticedError(int,const QString&)),
                     this, SLOT(onPlayerError(int,const QString&)));

    framelessHelper = new FramelessHelper(this);
    framelessHelper->setup(this);
    titleBar->connectFramelessHelper(framelessHelper);
    QObject::connect(titleBar, SIGNAL(appMenuClicked()), titleBar, SLOT(toggleMenu()));
}

void MainWindow::buildAppearancePanel()
{
    QWidget* panel = new QWidget(titleBar);
    QBoxLayout* lay = qNewBoxLayout(panel, QBoxLayout::LeftToRight, 2, 0);
    m_langBtn = makeAppearanceMenuButton(panel, qFromUtf8("文"));
    lay->addWidget(m_langBtn, 0);
    m_styleBtn = makeAppearanceMenuButton(panel, qFromUtf8("饰"));
    lay->addWidget(m_styleBtn, 0);
    m_darkBtn = makeAppearanceMenuButton(panel, qFromUtf8("☾"));
    lay->addWidget(m_darkBtn, 0);
    titleBar->addTitleWidget(panel, 0);
}

void MainWindow::addMenuItem(void* menu, const QObject* receiver, const char* slot,
                             const char* key, bool darkToggle)
{
    QString text = _(qFromUtf8(key));
    MenuItemRef ref;
    ref.key = key;
    ref.darkToggle = darkToggle;
    ref.owner = menu;
#ifdef QT3_BUILD
    QPopupMenu* m = static_cast<QPopupMenu*>(menu);
    ref.id = m->insertItem(text, receiver, slot);
#else
    QMenu* m = static_cast<QMenu*>(menu);
    ref.action = m->addAction(text, receiver, slot);
#endif
    menuItemRefs.push_back(ref);
}

void* MainWindow::addTopMenu(const char* key)
{
    EmbeddedMenuBar* mb = titleBar->menuBar();
    QString title = _(qFromUtf8(key));
    MenuItemRef ref;
    ref.key = key;
    ref.owner = mb;
    void* sub = 0;
#ifdef QT3_BUILD
    QPopupMenu* menu = new QPopupMenu(mb);
    menu->setFont(mb->font());
    ref.id = mb->insertItem(title, menu);
    sub = menu;
#else
    QMenu* menu = mb->addMenu(title);
    sub = menu;
#endif
    topMenuRefs.push_back(ref);
    return sub;
}

void* MainWindow::addSubMenu(void* parentMenu, const QString& title, const char* key)
{
    Q_UNUSED(key);
    EmbeddedMenuBar* mb = titleBar->menuBar();
    void* sub = 0;
#ifdef QT3_BUILD
    QPopupMenu* pm = static_cast<QPopupMenu*>(parentMenu);
    QPopupMenu* subMenu = new QPopupMenu(mb);
    subMenu->setFont(mb->font());
    pm->insertItem(title, subMenu);
    sub = subMenu;
#else
    QMenu* pm = static_cast<QMenu*>(parentMenu);
    QMenu* subMenu = pm->addMenu(title);
    sub = subMenu;
#endif
    return sub;
}

void MainWindow::addMenuSeparator(void* menu)
{
#ifdef QT3_BUILD
    static_cast<QPopupMenu*>(menu)->insertSeparator();
#else
    static_cast<QMenu*>(menu)->addSeparator();
#endif
}

void MainWindow::setTopMenuText(int index, const QString& text)
{
    if (index < 0 || index >= (int)topMenuRefs.size()) { return; }
#ifdef QT3_BUILD
    EmbeddedMenuBar* mb = static_cast<EmbeddedMenuBar*>(topMenuRefs[index].owner);
    mb->changeItem(topMenuRefs[index].id, text);
#else
    EmbeddedMenuBar* mb = titleBar->menuBar();
    QList<QAction*> acts = mb->actions();
    if (index < acts.size()) acts[index]->setText(text);
#endif
}

void MainWindow::refreshMenuTexts()
{
    for (size_t i = 0; i < menuItemRefs.size(); i++) {
        const MenuItemRef& ref = menuItemRefs[i];
        if (!ref.key) continue;
        QString text = _(qFromUtf8(ref.key));
#ifdef QT3_BUILD
        static_cast<QPopupMenu*>(ref.owner)->changeItem(ref.id, text);
#else
        if (ref.action) static_cast<QAction*>(ref.action)->setText(text);
#endif
    }
    for (int i = 0; i < (int)topMenuRefs.size() && i < kTopMenuKeyCount; i++) {
        setTopMenuText(i, _(qFromUtf8(kTopMenuKeys[i])));
    }
}

void MainWindow::buildMenus()
{
    EmbeddedMenuBar* mb = titleBar->menuBar();
    menuItemRefs.clear();
    topMenuRefs.clear();
    MenuWidget34* file = static_cast<MenuWidget34*>(addTopMenu("menu.file"));
    addMenuItem(file, this, SLOT(onMenu1Stub()), "action.new");
    addMenuItem(file, this, SLOT(openSettings()), "menu.settings");
    addMenuSeparator(file);
    addMenuItem(file, this, SLOT(quitApp()), "action.quit");
    MenuWidget34* edit = static_cast<MenuWidget34*>(addTopMenu("menu.edit"));
    addMenuItem(edit, this, SLOT(onMenu1Stub()), "action.undo");
    addMenuItem(edit, this, SLOT(onMenu1Stub()), "action.redo");
    MenuWidget34* tool = static_cast<MenuWidget34*>(addTopMenu("menu.tool"));
    addMenuItem(tool, this, SLOT(onDemoStatusInfo()), "demo.status_info");
    addMenuItem(tool, this, SLOT(onDemoStatusWarning()), "demo.status_warning");
    addMenuItem(tool, this, SLOT(onDemoStatusError()), "demo.status_error");
    addMenuSeparator(tool);
    addMenuItem(tool, this, SLOT(onDemoStatusClear()), "demo.status_clear");
    addMenuItem(tool, this, SLOT(onDemoToggleStatusWidgets()), "demo.status_widgets");
    addMenuSeparator(tool);
    addMenuItem(tool, this, SLOT(onDemoTrayBubble()), "demo.tray_bubble");
    addMenuSeparator(tool);
    addMenuItem(tool, this, SLOT(onDemoApiRequest()), "demo.api_request");
    addMenuSeparator(tool);
    addMenuItem(tool, this, SLOT(openSettings()), "menu.settings");
    /* M5-A2:EQ / 截图 / 引擎切换(中文 key 作 fallback 文案) */
    addMenuSeparator(tool);
    addMenuItem(tool, this, SLOT(onOpenEqDialog()), "音视频均衡器(EQ)...");
    addMenuItem(tool, this, SLOT(onTakeScreenshot()), "截图(保存到缓存目录)");
    addMenuItem(tool, this, SLOT(onToggleEngine()), "切换播放引擎(ffmpeg/mpv)");
    MenuWidget34* help = static_cast<MenuWidget34*>(addTopMenu("menu.help"));
    addMenuItem(help, qApp, SLOT(aboutQt()), "menu.aboutqt");
    addMenuItem(help, this, SLOT(onAboutApp()), "menu.about_qlvotube");
    mb->finalize();
}

void MainWindow::buildAppearanceMenus()
{
    m_langMenu = new MenuWidget34(this);
    static_cast<MenuWidget34*>(m_langMenu)->setMinimumWidth(150);
    {
        const char* const* langKeys = appearanceLangLabelKeys();
        for (int i = 0; i < 3; i++) {
            LambdaSlot* slot = new LambdaSlot(this, [this, i]() {
                onTitleUilang(i);
                updateAppearanceTooltips();
                updateAppearanceMenuChecks();
            });
            addAppearanceMenuItem(&m_appearanceItems, static_cast<MenuWidget34*>(m_langMenu),
                                  slot, SLOT(call()), langKeys[i], 0, i);
        }
    }
    attachTitleMenu(m_langBtn, static_cast<MenuWidget34*>(m_langMenu));

    m_styleMenu = new MenuWidget34(this);
    static_cast<MenuWidget34*>(m_styleMenu)->setMinimumWidth(150);
    {
        const auto& panelStyles = StyleParams::registeredStyles();
        for (int i = 0; i < (int)panelStyles.size(); i++) {
            LambdaSlot* slot = new LambdaSlot(this, [this, i]() {
                onTitleStyle(i);
                updateAppearanceTooltips();
                updateAppearanceMenuChecks();
            });
            addAppearanceMenuItem(&m_appearanceItems, static_cast<MenuWidget34*>(m_styleMenu),
                                  slot, SLOT(call()), panelStyles[i].displayKey, 1, i);
        }
    }
    attachTitleMenu(m_styleBtn, static_cast<MenuWidget34*>(m_styleMenu));

    m_darkMenu = new MenuWidget34(this);
    static_cast<MenuWidget34*>(m_darkMenu)->setMinimumWidth(150);
    {
        LambdaSlot* onSlot = new LambdaSlot(this, [this]() {
            onTitleDark(true);
            updateAppearanceTooltips();
            updateAppearanceMenuChecks();
        });
        addAppearanceMenuItem(&m_appearanceItems, static_cast<MenuWidget34*>(m_darkMenu),
                              onSlot, SLOT(call()), "theme_dark", 2, 0);
        LambdaSlot* offSlot = new LambdaSlot(this, [this]() {
            onTitleDark(false);
            updateAppearanceTooltips();
            updateAppearanceMenuChecks();
        });
        addAppearanceMenuItem(&m_appearanceItems, static_cast<MenuWidget34*>(m_darkMenu),
                              offSlot, SLOT(call()), "theme_light", 2, 1);
    }
    attachTitleMenu(m_darkBtn, static_cast<MenuWidget34*>(m_darkMenu));
    updateAppearanceTooltips();
    updateAppearanceMenuChecks();
}

void MainWindow::buildTray()
{
    if (!SystemTrayIcon::isSystemTrayAvailable()) return;
    tray = new SystemTrayIcon(QPixmap(app_icon), this);
    s_trayIcon = tray;
    tray->setToolTip(_("app_title"));
    tray->setVisible(true);
    QObject::connect(tray, SIGNAL(activated(int)), this, SLOT(trayActivated(int)));
    QObject::connect(tray, SIGNAL(messageClicked()), this, SLOT(trayShowMainWindow()));
    PopupMenu* trayMenu = new PopupMenu(this);
    addMenuItem(trayMenu, this, SLOT(trayShowMainWindow()), "tray.open");
    addMenuSeparator(trayMenu);
    addMenuItem(trayMenu, this, SLOT(quitApp()), "tray.quit");
    tray->setContextMenu(trayMenu);
}

void MainWindow::trayActivated(int reason)
{
    if (reason == SystemTrayIcon::Trigger || reason == SystemTrayIcon::DoubleClick) {
        trayShowMainWindow();
    }
}

void MainWindow::trayShowMainWindow()
{
    show();
    raise();
    qActivateWindow(this);
}

void MainWindow::quitApp()
{
    forceQuit = true;
    close();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!forceQuit && tray && tray->isVisible()) {
        hide();
        event->ignore();
        return;
    }
    /* 借关闭事件明确停播:cmdmpv 走 killProcess;libmpv 走 terminate */
    if (m_player != 0 && m_player->engine() != 0)
        m_player->close();
    event->accept();
}

void MainWindow::onTitleUilang(int index)
{
    if (index < 0 || index > 2) return;
    QString langCode = qFromUtf8(kLangCodes[index]);
    Config::setValue("uilang", langCode);
    Translator::instance().loadLanguage(langCode);
    QtappSetup::installQtTranslations(langCode);
    updateAppearanceTooltips();
    updateAppearanceMenuChecks();
}

void MainWindow::onTitleStyle(int index)
{
    const std::vector<StyleParams::Definition>& styles = StyleParams::registeredStyles();
    if (index >= 0 && index < (int)styles.size()) {
        ThemeManager::setStyle(styles[index].id, ThemeManager::isDarkMode());
    }
}

void MainWindow::onTitleDark(bool on)
{
    if (on == ThemeManager::isDarkMode()) return;
    ThemeManager::applyTheme(on);
    refreshMenuTexts();
    updateAppearanceTooltips();
    updateAppearanceMenuChecks();
}

void MainWindow::onDarkMenuItem()
{
    onTitleDark(!ThemeManager::isDarkMode());
}

void MainWindow::refreshAppearanceMenuTexts()
{
    for (size_t i = 0; i < m_appearanceItems.size(); i++) {
        const AppearanceMenuItemRef& ref = m_appearanceItems[i];
        if (!ref.key) continue;
        QString text = _(qFromUtf8(ref.key));
#ifdef QT3_BUILD
        static_cast<QPopupMenu*>(ref.owner)->changeItem(ref.id, text);
#else
        if (ref.action) static_cast<QAction*>(ref.action)->setText(text);
#endif
    }
    refreshMenuTexts();
}

void MainWindow::updateAppearanceTooltips()
{
    if (m_langBtn) {
        const char* const* langKeys = appearanceLangLabelKeys();
        int li = appearanceLangIndex(Translator::instance().currentLang());
        qSetToolTip(m_langBtn, _("title.tip_lang") + ": " + qFromUtf8(langKeys[li]));
    }
    if (m_styleBtn) {
        const std::vector<StyleParams::Definition>& styles = StyleParams::registeredStyles();
        int si = 0;
        for (int i = 0; i < (int)styles.size(); i++) {
            if (QString(styles[i].id) == QString(ThemeManager::styleId())) { si = i; break; }
        }
        QString label = (si >= 0 && si < (int)styles.size()) ? _(qFromUtf8(styles[si].displayKey)) : QString();
        qSetToolTip(m_styleBtn, _("title.tip_style") + ": " + label);
    }
    if (m_darkBtn) {
        qSetToolTip(m_darkBtn, _("title.tip_dark") + ": " + (ThemeManager::isDarkMode() ? _("title.dark_on") : _("title.dark_off")));
    }
}

void MainWindow::updateAppearanceMenuChecks()
{
    int cur[3];
    cur[0] = appearanceLangIndex(Translator::instance().currentLang());
    cur[1] = 0;
    {
        const std::vector<StyleParams::Definition>& styles = StyleParams::registeredStyles();
        for (int i = 0; i < (int)styles.size(); i++) {
            if (QString(styles[i].id) == QString(ThemeManager::styleId())) { cur[1] = i; break; }
        }
    }
    cur[2] = ThemeManager::isDarkMode() ? 0 : 1;
    for (size_t i = 0; i < m_appearanceItems.size(); i++) {
        const AppearanceMenuItemRef& ref = m_appearanceItems[i];
        if (ref.group < 0) continue;
        bool on = (ref.index == cur[ref.group]);
#ifdef QT3_BUILD
        static_cast<QPopupMenu*>(ref.owner)->setItemChecked(ref.id, on);
#else
        if (ref.action) static_cast<QAction*>(ref.action)->setChecked(on);
#endif
    }
}

void MainWindow::retranslateUi()
{
    QString title = _("app_title");
    qSetWindowTitle(this, title);
    if (titleBar) titleBar->setLabel(title);
    if (tray) tray->setToolTip(title);
    if (sideNav) sideNav->retranslateUi();
    if (contentView) contentView->retranslateUi();
    if (queuePanel) queuePanel->retranslateUi();
    if (playerBar) playerBar->retranslateUi();
    updateAppearanceTooltips();
    refreshAppearanceMenuTexts();
    updateAppearanceMenuChecks();
}

void MainWindow::showTrackStub(int playlistId, int trackIndex)
{
    const Playlist* pl = findPlaylist(m_library, playlistId);
    if (!pl) return;
    if (trackIndex < 0 || trackIndex >= (int)pl->tracks.size()) return;

    const Track& t = pl->tracks[trackIndex];
    m_curPlaylistId = playlistId;
    m_curTrackIndex = trackIndex;
    playerBar->setTrack(t);
    queuePanel->setCurrentTrack(t);
    queuePanel->setQueue(pl->tracks, trackIndex);
    sideNav->setCurrentPlaylist(playlistId);

    /* 曲目有 url → 走引擎;无 url 才占位提示(backend-abstraction.md Phase 1)
     * url 待折叠:(仅 None);resolve 属后续 Phase 拉取 */
    if (t.url.isEmpty()) {
        playerBar->setPlaying(false);
        SharedStatusBar::instance()->showMessage(
            _("player.no_media") + qFromUtf8(" ") + t.title, 4000);
    } else {
        tryPlayTrack(t);
    }
}

void MainWindow::ensurePlayerReady()
{
    if (m_player == 0)
        return;
    if (m_player->engine() != 0)
        return;
    /* M5-A:嵌入首选 ffmpeg(渲染器可控);编内缺失则回落 auto
     * QA/排障:QLVOTUBE_ENGINE=mpv 强制走 libmpv(mpv-gl 路) */
    const char* want = getenv("QLVOTUBE_ENGINE");
    bool wantMpv = want != 0 && strcmp(want, "mpv") == 0;
    if (wantMpv) {
        if (!m_player->setEngine(CxEngineMpv))
            m_player->setEngine(CxEngineAuto);
    } else if (!m_player->setEngine(CxEngineFfmpeg)) {
        m_player->setEngine(CxEngineAuto);
    }
    m_player->setHostWidget(videoPane->surface());
    m_player->setRenderer(m_glRenderer);
    if (m_player->engine() != 0) {
        videoPane->setEngineLabel(
            QString::fromLatin1(m_player->engine()->name()));
        if (getenv("QLVOTUBE_DEBUG") != 0)
            fprintf(stderr, "engine=%s\n", m_player->engine()->name());
    }
}

void MainWindow::onQaAutoplay()
{
    const char* u = getenv("QLVOTUBE_AUTOPLAY");
    if (u == 0 || u[0] == 0)
        return;
    Track t;
    t.id = -1;
    t.title = QString::fromLatin1(u);
    t.url = QString::fromLatin1(u);
    t.playable = true;
    t.durationMs = 0;
    t.ncmId = 0;
    t.albumId = 0;
    if (playerBar) playerBar->setTrack(t);
    tryPlayTrack(t);
}

void MainWindow::tryPlayTrack(const Track& t)
{
    ensurePlayerReady();
    if (m_player == 0 || m_player->engine() == 0) {
        SharedStatusBar::instance()->showMessage(
            _("player.no_media") + qFromUtf8(" ") + t.title, 4000);
        return;
    }
    m_player->open(t.url);
    m_curUrl = t.url;
    m_player->play(); // 外观层 play() 为 void;失败路径经 noticedError 走状态栏
}

void MainWindow::onTrackActivated(int playlistId, int trackIndex)
{
    showTrackStub(playlistId, trackIndex);
}

static QString langCodeForLabel(const QString& label)
{
    const char* const* langKeys = appearanceLangLabelKeys();
    for (int i = 0; i < 3; i++)
        if (label == _(qFromUtf8(langKeys[i]))) return qFromUtf8(kLangCodes[i]);
    return QString();
}

void MainWindow::openSettings()
{
    ConfigDialog* dlg = new ConfigDialog(_("settings.window_title"), this);

    /* —— 通用页：语言 + 深色模式 ——
     * ConfigDialog 真源 = ~/.config/qlvotube/ui.conf（QSettings34）；
     * 落盘值在槽里回写到各自生效端：语言→config.json uilang，深色→ThemeManager。 */
    CategoryPage* page = new CategoryPage(_("settings.general_tab"), dlg);

    const char* const* langKeys = appearanceLangLabelKeys();
    int curLang = appearanceLangIndex(Translator::instance().currentLang());
    SelectConfigItem* langItem = new SelectConfigItem(
        "ui/lang", (curLang >= 0 && curLang < 3) ? qFromUtf8(langKeys[curLang]) : "",
        "",
        QStringList() << _(qFromUtf8(langKeys[0])) << _(qFromUtf8(langKeys[1]))
                      << _(qFromUtf8(langKeys[2])), page);
    page->addLabeledControl(_("settings.lang_label"), langItem->comboBox());

    SelectConfigItem* darkItem = new SelectConfigItem(
        "ui/dark", ThemeManager::isDarkMode() ? _("theme_dark") : _("theme_light"), "",
        QStringList() << _("theme_dark") << _("theme_light"), page);
    page->addLabeledControl(_("settings.dark_label"), darkItem->comboBox());

    dlg->addCategory(_("settings.general_tab"), page);
    dlg->registerConfigItem(langItem);
    dlg->registerConfigItem(darkItem);

    /* —— 关于页 —— */
    CategoryPage* about = new CategoryPage(_("menu.help"), dlg);
    QLabel* aboutLabel = new QLabel(
        QString("<b>qlvotube %1</b><br>Qt: %2")
            .arg(APP_VERSION_FULL).arg(qVersion()), about);
    about->addWidget(aboutLabel);
    dlg->addCategory(_("menu.help"), about);

    dlg->setSettingsName("qlvotube", "ui");
    QObject::connect(dlg, SIGNAL(settingsSaved(const SettingsChangedMap&)),
                     this, SLOT(onSettingsSaved(const SettingsChangedMap&)));
    dlg->loadSettings();
    dlg->exec();
    delete dlg;
}

void MainWindow::onSettingsSaved(const SettingsChangedMap& changed)
{
    if (changed.contains("ui/lang")) {
        QString code = langCodeForLabel(changed["ui/lang"].toString());
        if (!code.isEmpty() && code != Translator::instance().currentLang()) {
            Config::setValue("uilang", code);
            Translator::instance().loadLanguage(code);
            QtappSetup::installQtTranslations(code);
        }
    }
    if (changed.contains("ui/dark")) {
        bool on = (changed["ui/dark"].toString() == _("theme_dark"));
        if (on != ThemeManager::isDarkMode())
            ThemeManager::applyTheme(on);
    }
}

void MainWindow::onUserLoginPlaceholder()
{
    /* 后续接入登录；成功后经 sideNav->setUserLoggedIn(昵称, 头像 seed) 更新区域 */
    ToastWidget::showCenter(this, qFromUtf8("登录功能开发中"),
                            ToastWidget::Info, 2500);
}

void MainWindow::onNavAction(int action)
{
    if (action == 0) {
        SharedStatusBar::instance()->showMessage(_("nav.rank_coming"), 3000);
        return;
    }
    if (action == 1) {
        SharedStatusBar::instance()->showMessage(_("user.not_login"), 3000);
        return;
    }
}

void MainWindow::onPlayPause()
{
    if (m_player == 0 || m_player->engine() == 0) {
        SharedStatusBar::instance()->showMessage(qFromUtf8("播放引擎未接入"), 2000);
        return;
    }
    if (m_player->state() == CxPlaying)
        m_player->pause();
    else if (m_player->state() == CxPaused)
        m_player->play();
    else if (m_curPlaylistId >= 0 && m_curTrackIndex >= 0) {
        /* 空闲状态点 Play:从选中曲目起播 */
        const Playlist* pl = findPlaylist(m_library, m_curPlaylistId);
        if (pl && m_curTrackIndex < (int)pl->tracks.size())
            tryPlayTrack(pl->tracks[m_curTrackIndex]);
    }
}

/* ── 播放引擎回执(cxvideo M1)── */
void MainWindow::onPlayerStarted()
{
    if (playerBar) playerBar->setPlaying(true);
    switchToPlayerView(true);
}

void MainWindow::onPlayerPaused()
{
    if (playerBar) playerBar->setPlaying(false);
}

void MainWindow::onPlayerStopped()
{
    if (playerBar) playerBar->setPlaying(false);
    switchToPlayerView(false);
}

/* M5-A:播放页/列表页切换 */
void MainWindow::switchToPlayerView(bool on)
{
    if (centerStack == 0)
        return;
    qStackSetCurrent(centerStack, on ? (QWidget*)videoPane
                                     : (QWidget*)contentView);
}

void MainWindow::onVideoBackRequested()
{
    switchToPlayerView(false);
}

void MainWindow::onPlayerFinished()
{
    if (playerBar) playerBar->setPlaying(false);
    /* 简易连播:顺序换下一首(引擎 finished 后 UI 流迁移) */
    onNext();
}

void MainWindow::onPlayerPosition(long ms)
{
    if (playerBar) playerBar->setProgress((int)ms);
}

void MainWindow::onPlayerDuration(long ms)
{
    if (playerBar) playerBar->setDuration((int)ms);
}

void MainWindow::onVolumeChanged(int pct)
{
    if (m_player != 0) m_player->setVolume(pct);
}

void MainWindow::onRateChanged(double r)
{
    if (m_player != 0) m_player->setRate(r);
}

void MainWindow::onOpenEqDialog()
{
    if (m_eqDialog == 0) {
        m_eqDialog = new CXVideoEqDialog(this);
        QObject::connect(m_eqDialog, SIGNAL(eqChanged()),
                         this, SLOT(onEqChanged()));
    }
    m_eqDialog->show();
    m_eqDialog->raise();
}

void MainWindow::onEqChanged()
{
    if (m_player != 0 && m_eqDialog != 0)
        m_player->setEq(m_eqDialog->eq());
}

void MainWindow::onTakeScreenshot()
{
    if (m_player == 0 || m_player->engine() == 0) {
        SharedStatusBar::instance()->showMessage(qFromUtf8("播放引擎未接入"), 2000);
        return;
    }
    QString dir = qGetHomePath() + "/.cache/qlvotube";
#ifdef QT3_BUILD
    unsigned long long stamp = (unsigned long long)QDateTime::currentDateTime().toTime_t();
#else
    unsigned long long stamp =
        (unsigned long long)QDateTime::currentDateTime().toSecsSinceEpoch();
#endif
    QString path = dir + "/shot_" + QString::number(stamp) + ".png";
    bool ok = m_player->saveScreenshot(path);
#ifdef QT3_BUILD
    const char* pathC = path.latin1();
#else
    QByteArray pathBa = path.toLatin1();
    const char* pathC = pathBa.constData();
#endif
    if (getenv("QLVOTUBE_DEBUG") != 0)
        fprintf(stderr, "shot: %s ok=%d\n", pathC, (int)ok);
    if (ok)
        SharedStatusBar::instance()->showMessage(qFromUtf8("截图: ") + path, 4000);
    else
        SharedStatusBar::instance()->showMessage(qFromUtf8("截图失败(内核不支持)"), 3000);
}

/* M5-A2:ffmpeg ↔ mpv 切换(重建引擎 + 重挂宿主/渲染器;在播则重开) */
void MainWindow::onToggleEngine()
{
    if (m_player == 0)
        return;
    bool toMpv = (m_player->engine() == 0) ||
                 QString::fromLatin1(m_player->engine()->name()) != "mpv";
    m_player->stop();
    if (!m_player->setEngine(toMpv ? CxEngineMpv : CxEngineFfmpeg)) {
        SharedStatusBar::instance()->showMessage(qFromUtf8("引擎切换失败"), 3000);
        return;
    }
    m_player->setHostWidget(videoPane->surface());
    m_player->setRenderer(m_glRenderer);
    videoPane->setEngineLabel(QString::fromLatin1(m_player->engine()->name()));
    if (!m_curUrl.isEmpty()) {
        m_player->open(m_curUrl);
        m_player->play();
        switchToPlayerView(true);
    }
    SharedStatusBar::instance()->showMessage(
        qFromUtf8("当前引擎: ") + QString::fromLatin1(m_player->engine()->name()), 3000);
}

void MainWindow::onPlayerError(int code, const QString& msg)
{
    (void)code;
    if (playerBar) playerBar->setPlaying(false);
    SharedStatusBar::instance()->showMessageTyped(msg, StatusError, 4000);
}

void MainWindow::onSeekRequested(int ms)
{
    /* cmdmpv 进程型 M1 暂不支持外控(M2 接 IPC);libmpv 内核可精细 seek */
    if (m_player != 0 && m_player->engine() != 0)
        m_player->seek(ms);
}

void MainWindow::onPrev()
{
    if (m_curTrackIndex > 0) showTrackStub(m_curPlaylistId, m_curTrackIndex - 1);
}

void MainWindow::onNext()
{
    const Playlist* pl = findPlaylist(m_library, m_curPlaylistId);
    if (!pl) return;
    if (m_curTrackIndex + 1 < (int)pl->tracks.size())
        showTrackStub(m_curPlaylistId, m_curTrackIndex + 1);
}

void MainWindow::onMenu1Stub() {}
void MainWindow::onDemoStatusInfo() { SharedStatusBar::instance()->showMessageTyped(_("demo.status_info"), StatusInfo, 3000); }
void MainWindow::onDemoStatusWarning() { SharedStatusBar::instance()->showMessageTyped(_("demo.status_warning"), StatusWarning, 3000); }
void MainWindow::onDemoStatusError() { SharedStatusBar::instance()->showMessageTyped(_("demo.status_error"), StatusError, 3000); }
void MainWindow::onDemoStatusClear() { SharedStatusBar::instance()->clearMessage(); }
void MainWindow::onDemoToggleStatusWidgets()
{
    if (hasDemoWidgets()) {
        removeDemoWidgets();
        SharedStatusBar::instance()->showMessage(_("demo.status_clear"), 3000);
    } else {
        addDemoWidgets();
        SharedStatusBar::instance()->showMessage(_("demo.ready"), 3000);
    }
}
void MainWindow::onDemoTrayBubble()
{
    if (!tray || !tray->isVisible() || !SystemTrayIcon::supportsMessages()) {
        SharedStatusBar::instance()->showMessage(_("demo.tray_bubble"), 3000);
        return;
    }
    tray->showMessage(_("app_title"), _("demo.tray_bubble"), SystemTrayIcon::Information, 5000);
}
void MainWindow::onAboutApp()
{
    QString text = QString("<h3>qlvotube %1</h3><p>qlvotube shell (Qt Widgets)</p><p>Qt: %2</p>").arg(APP_VERSION_FULL).arg(qVersion());
    QMessageBox::about(this, _("menu.about_qlvotube"), text);
}

void MainWindow::onDemoApiRequest()
{
    HttpRequest req("http://localhost:8181/api/self", "GET", "", 10);
    Api::request(req, ApiGetSelf);
}

void MainWindow::customEvent(CustomEventBase* event)
{
    if (event->type() == ApiResultReadyType) {
        ApiHttpResultEvent* e = static_cast<ApiHttpResultEvent*>(event);
        if (e->resp.curlErrStr.empty()) {
            stbarShowStatusMessage(qFromUtf8("HTTP ") + QString::number(e->resp.httpCode),
                                   SticonInfo, 3000);
        } else {
            stbarShowStatusMessage(
                qFromUtf8("HTTP ") + QString::number(e->resp.httpCode) + " " +
                    qFromUtf8(e->resp.curlErrStr),
                SticonCritical, 8000);
        }
        return;   // postEvent 投递的事件由事件循环自动删除，不手动 delete
    }
    QMainWindow::customEvent(event);
}

