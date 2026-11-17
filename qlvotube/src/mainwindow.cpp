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
#include "providers/common/provider.h"
#include "providers/common/errortext.h"
#include "providers/common/debuglog.h"   /* QXVOTUBE_DEBUG → providers 分级日志 */
#include "videoui.h"
#include "videothumbcache.h"
#include "translator.h"

#include "compat34.h"
#include "cxvideoplayer.h"
#include "cxengine.h"
#include "renderer_gl.h"     /* M5-A:ffmpeg 路 GL 渲染器 */
#include "videorendererfactory.h"   /* M6:输出后端可配置(cxvideo-vo.md) */
#include "videoinfodialog.h"        /* M7:视频详情弹窗 */
#include <qclipboard.h>
#include "appearance.h"
#include <qlabel.h>
#include <qpushbutton.h>
#include <string>

#include <qpixmap.h>
#include <qevent.h>
#include <qtimer.h>
#include <qmessagebox.h>
#if QT_VERSION >= 0x040000
#include <QDebug>
#endif
#include <qlineedit.h>
#include <qsplitter.h>
#include <qdatetime.h>
#include <qfiledialog.h>
#include <qfileinfo.h>
#include <qdir.h>
#include <qurl.h>
#ifdef QT3_BUILD
#include <qdragobject.h>   /* QUriDrag:Qt3 拖放 MIME 解码 */
#include <qstrlist.h>
#else
#include <QMimeData>       /* dragEnter/dropEvent 的 mimeData() 完整类型 */
#endif

#include <cstdlib>
#include <fstream>

#include "app_icon.xpm"

// 截图目录:QLVOTUBE_SHOT_DIR 环境变量优先,缺省缓存目录;确保目录存在
// (qa 自检与 onTakeScreenshot 共用;顺带修后者原不建目录)
static QString cxShotDir()
{
    const char* env = getenv("QLVOTUBE_SHOT_DIR");
    QString dir = (env != 0 && *env != 0)
        ? QString::fromLocal8Bit(env)
        : (qGetHomePath() + "/.cache/qlvotube");
#ifdef QT3_BUILD
    QDir d; d.mkdir(dir); /* Qt3 无 mkpath */
#else
    QDir().mkpath(dir);
#endif
    return dir;
}

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
    , m_rendererWidget(0)
    , m_videoRenderer(0)
    , m_eqDialog(0)
    , m_curProv(0)
    , m_gridPage(1)
    , m_gridHasMore(false)
    , m_gridLoading(false)
    , m_gridGen(0)
    , m_gridIsSearch(false)
    , m_videoIdx(-1)
    , m_videoMode(false)
    , m_subIdx(-1)
    , m_curPlaylistId(0)
    , m_curTrackIndex(-1)
    , m_argvIdx(-1)
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

    setGeometry(100, 50, 980, 640);
    buildStatusBar();
    buildCentralWidget();
    buildAppearancePanel();
    buildMenus();
    buildAppearanceMenus();
    buildTray();

    /* QA 钩子(M5-A 嵌入自测):QLVOTUBE_AUTOPLAY=<url> 起播 */
    if (getenv("QLVOTUBE_AUTOPLAY") != 0)
        QTimer::singleShot(800, this, SLOT(onQaAutoplay()));
    if (getenv("QLVOTUBE_AUTOQUIT_MS") != 0)
        QTimer::singleShot(atoi(getenv("QLVOTUBE_AUTOQUIT_MS")),
                           qApp, SLOT(quit()));
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
    QObject::connect(sideNav, SIGNAL(siteChanged(const QString&)),
                     this, SLOT(onSiteChanged(const QString&)));
    QObject::connect(contentView, SIGNAL(chipActivated(const QString&)),
                     this, SLOT(onChipActivated(const QString&)));
    QObject::connect(contentView, SIGNAL(videoActivated(const QString&)),
                     this, SLOT(onVideoActivated(const QString&)));
    QObject::connect(contentView, SIGNAL(loadMoreRequested()),
                     this, SLOT(onGridLoadMore()));
    QObject::connect(sideNav, SIGNAL(searchSubmitted(const QString&)),
                     this, SLOT(onSearchSubmitted(const QString&)));

    /* 侧栏左上用户区（登录占位） */
    QObject::connect(sideNav, SIGNAL(loginRequested()),
                     this, SLOT(onUserLoginPlaceholder()));
    QObject::connect(sideNav, SIGNAL(actionTriggered(int)),
                     this, SLOT(onNavAction(int)));

    setupVideoProviders();

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
        QObject::connect(playerBar, SIGNAL(muteChanged(bool)),
                         this, SLOT(onMuteChanged(bool)));
        QObject::connect(playerBar, SIGNAL(rateChanged(double)),
                         this, SLOT(onRateChanged(double)));
        QObject::connect(playerBar, SIGNAL(eqRequested()),
                         this, SLOT(onOpenEqDialog()));
        QObject::connect(playerBar, SIGNAL(shotRequested()),
                         this, SLOT(onTakeScreenshot()));
        QObject::connect(playerBar, SIGNAL(subtitleRequested()),
                         this, SLOT(onCycleSubtitle()));
    }

    /* 播放引擎接线(掌面延迟:首次选曲时真正 setEngine) */
    m_player = new CXVideoPlayer(this);
    /* M6:输出后端可配置(默认 auto = 无 GL 优先,gl 最后;见 docs/cxvideo-vo.md)
     * 优先级:QLVOTUBE_VO_OUT(env) > config.json video.out > auto */
    {
        const char* envOut = getenv("QLVOTUBE_VO_OUT");
        CxVideoOut vo = cxCreateVideoOut(videoPane->surface(),
                                         Config::value("video.out"),
                                         envOut != 0 ? qFromUtf8(envOut) : QString());
        m_rendererWidget = vo.widget;
        m_videoRenderer = vo.renderer;
        if (vo.widget != 0)
            videoPane->setRendererWidget(vo.widget);
        if (getenv("QLVOTUBE_DEBUG") != 0) {
#if QT_VERSION >= 0x060000
            qDebug() << "video out:" << vo.name << "(" << vo.why << ")";
#else
            qDebug("video out: %s (%s)", qToUtf8(vo.name).data(),
                   qToUtf8(vo.why).data());
#endif
        }
    }
    QObject::connect(videoPane, SIGNAL(backRequested()),
                     this, SLOT(onVideoBackRequested()));
    /* M7:播放器右键 → 视频详情/复制/浏览器打开 */
    QObject::connect(videoPane, SIGNAL(contextMenuRequested(const QPoint&)),
                     this, SLOT(onPlayerContextMenu(const QPoint&)));
    /* M7:列表卡片右键 → 复制/显示/浏览器打开 */
    QObject::connect(contentView,
                     SIGNAL(videoContextMenu(const QString&, const QPoint&)),
                     this, SLOT(onVideoContextMenu(const QString&, const QPoint&)));

    /* 引擎回执接线(进程型 cmdmpv 为 M1 主线;started/finished 已映射) */
    QObject::connect(m_player, SIGNAL(started()), this, SLOT(onPlayerStarted()));
    QObject::connect(m_player, SIGNAL(paused()), this, SLOT(onPlayerPaused()));
    QObject::connect(m_player, SIGNAL(stopped()), this, SLOT(onPlayerStopped()));
    QObject::connect(m_player, SIGNAL(finished()), this, SLOT(onPlayerFinished()));
    QObject::connect(m_player, SIGNAL(positionChanged(long)), this, SLOT(onPlayerPosition(long)));
    QObject::connect(m_player, SIGNAL(durationChanged(long)), this, SLOT(onPlayerDuration(long)));
    QObject::connect(m_player, SIGNAL(subtitleChanged(const QString&)),
                     this, SLOT(onSubtitleText(const QString&)));
    QObject::connect(m_player, SIGNAL(noticedError(int,const QString&)),
                     this, SLOT(onPlayerError(int,const QString&)));
    QObject::connect(m_player, SIGNAL(videoInfoChanged()),
                     this, SLOT(applyVideoInfo()));   /* M7 */

    framelessHelper = new FramelessHelper(this);
    framelessHelper->setup(this);
    setAcceptDrops(true);   /* 拖拽导入媒体文件(二期同步) */
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
    /* M5-A2:EQ / 截图 / 引擎切换 / 外挂字幕(JSON key,见 lang/*.json 的 menu.*) */
    addMenuSeparator(tool);
    addMenuItem(tool, this, SLOT(onOpenEqDialog()), "menu.eq");
    addMenuItem(tool, this, SLOT(onTakeScreenshot()), "menu.screenshot");
    addMenuItem(tool, this, SLOT(onToggleEngine()), "menu.engine");
    addMenuItem(tool, this, SLOT(onLoadSubtitleFile()), "menu.subtitle");
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
    m_player->setRenderer(m_videoRenderer);
    if (m_player->engine() != 0) {
        videoPane->setEngineLabel(
            QString::fromLatin1(m_player->engine()->name()));
        if (getenv("QLVOTUBE_DEBUG") != 0) {
#if QT_VERSION >= 0x060000
            qDebug() << "engine=" << m_player->engine()->name();
#else
            qDebug("engine=%s", m_player->engine()->name());
#endif
        }
    }
}

void MainWindow::setAutoplayFiles(const QStringList& files)
{
    m_argvUrls = files;
}

void MainWindow::startAutoplayFiles()
{
    if (m_argvUrls.isEmpty()) {
        /* 候选项 a:无 argv 且未设 AUTOPLAY 时恢复上次 argv 列表 */
        const char* au = getenv("QLVOTUBE_AUTOPLAY");
        if (au == 0 || au[0] == 0) {
            const QStringList saved = qStrSplit(Config::value("argv_playlist"), "\n", true);
            for (int i = 0; i < saved.size(); ++i) {
                if (QFile::exists(saved[i]))
                    m_argvUrls.append(saved[i]);
            }
            if (!m_argvUrls.isEmpty()) {
                int idx = Config::value("argv_idx").toInt();
                if (idx < 0 || idx >= (int)m_argvUrls.size())
                    idx = 0;
                playArgvAt(idx);
                return;
            }
        }
        return;
    }
    playArgvAt(0);
    startQaTimeline();
}

/* 二期同步:argv 列表切歌(⏮ 到头回绕/播完停止,与 qskvotube 同语义) */
void MainWindow::playArgvAt(int idx)
{
    if (m_argvUrls.isEmpty())
        return;
    if (idx < 0)
        idx = (int)m_argvUrls.size() - 1;   /* 上一首到头回绕 */
    if (idx >= (int)m_argvUrls.size()) {
        /* 列表播完:停止,不循环 */
        if (m_player != 0) m_player->stop();
        m_argvIdx = -1;
        if (playerBar) playerBar->setPlaying(false);
        SharedStatusBar::instance()->showMessage(qFromUtf8("列表播完"), 4000);
        return;
    }
    m_argvIdx = idx;
    /* 候选项 a:argv 播放列表持久化(仅 argv 列表,不做库列表) */
    Config::setValue("argv_playlist", m_argvUrls.join("\n"));
    Config::setValue("argv_idx", QString::number(idx));
    Track t;
    t.id = -1;
    t.title = QFileInfo(m_argvUrls[idx]).fileName();
    t.url = m_argvUrls[idx];
    t.playable = true;
    t.durationMs = 0;
    t.ncmId = 0;
    t.albumId = 0;
    if (playerBar) playerBar->setTrack(t);
    qSetWindowTitle(this, t.title);
    m_curPlaylistId = -1;   /* argv 模式:库列表索引无效 */
    tryPlayTrack(t);
}

void MainWindow::onQaAutoplay()
{
    if (!m_argvUrls.isEmpty())
        return;   /* argv 多文件优先(二期同步) */
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
    startQaTimeline();
}

/* 二期同步:QLVOTUBE_QA=1 六步自检(与 qskvotube src/main.cpp 同格式) */
void MainWindow::startQaTimeline()
{
    if (getenv("QLVOTUBE_QA") == 0)
        return;
    QTimer::singleShot(3000, this, SLOT(onQaStepPause()));
    QTimer::singleShot(5000, this, SLOT(onQaStepPlay()));
    QTimer::singleShot(7000, this, SLOT(onQaStepSeek()));
    QTimer::singleShot(9000, this, SLOT(onQaStepRate()));
    QTimer::singleShot(11000, this, SLOT(onQaStepCc()));
    QTimer::singleShot(13000, this, SLOT(onQaStepEq()));
    QTimer::singleShot(15000, this, SLOT(onQaStepShot()));
}

void MainWindow::onQaStepPause()
{
    if (m_player != 0) m_player->pause();
}

void MainWindow::onQaStepPlay()
{
#if QT_VERSION >= 0x060000
    qDebug() << "qa: step=pause ok="
             << int(m_player != 0 && m_player->state() == CxPaused)
             << "state" << int(m_player != 0 ? m_player->state() : 0);
#else
    qDebug("qa: step=pause ok=%d state=%d",
           (int)(m_player != 0 && m_player->state() == CxPaused),
           (int)(m_player != 0 ? m_player->state() : 0));
#endif
    if (m_player != 0) m_player->play();
}

void MainWindow::onQaStepSeek()
{
    if (m_player == 0) return;
    m_player->seek(5000);
#if QT_VERSION >= 0x060000
    qDebug() << "qa: step=seek issued=1 pos=" << (long)m_player->position();
#else
    qDebug("qa: step=seek issued=1 pos=%ld", (long)m_player->position());
#endif
}

void MainWindow::onQaStepRate()
{
    if (m_player == 0) return;
    m_player->setRate(1.5);
#if QT_VERSION >= 0x060000
    qDebug() << "qa: step=rate ok=1 pos=" << (long)m_player->position();
#else
    qDebug("qa: step=rate ok=1 pos=%ld", (long)m_player->position());
#endif
}

void MainWindow::onQaStepCc()
{
    int n = m_player != 0 ? m_player->subtitleTrackCount() : 0;
    bool ok = (n <= 0) || m_player->setSubtitleTrack(0);
#if QT_VERSION >= 0x060000
    qDebug() << "qa: step=cc tracks=" << n << "ok" << int(ok);
#else
    qDebug("qa: step=cc tracks=%d ok=%d", n, (int)ok);
#endif
}

void MainWindow::onQaStepEq()
{
    if (m_player == 0) return;
    CXVideoEq eq;
    eq.brightness = 60;
    m_player->setEq(eq);
#if QT_VERSION >= 0x060000
    qDebug() << "qa: step=eq ok=1";
#else
    qDebug("qa: step=eq ok=1");
#endif
}

void MainWindow::onQaStepShot()
{
    if (m_player == 0 || m_player->engine() == 0) {
#if QT_VERSION >= 0x060000
        qWarning() << "qa: step=shot ok=0 path=";
#else
        qWarning("qa: step=shot ok=0 path=");
#endif
        return;
    }
    const QString dir = cxShotDir();
    QString path = dir + "/qa_" +
                   QString::number((unsigned long long)
#if defined(QT3_BUILD) || (defined(QT_VERSION) && QT_VERSION < QT_VERSION_CHECK(5, 0, 0))
                       QDateTime::currentDateTime().toTime_t()
#else
                       QDateTime::currentDateTime().toSecsSinceEpoch()
#endif
                       ) +
                   ".png";
#ifdef QT3_BUILD
    const char* pathC = path.latin1();
#else
    QByteArray pathBa = path.toLatin1();
    const char* pathC = pathBa.constData();
#endif
#if QT_VERSION >= 0x060000
    qDebug() << "qa: step=shot ok=" << int(m_player->saveScreenshot(path))
             << "path" << path;
#else
    qDebug("qa: step=shot ok=%d path=%s",
           (int)m_player->saveScreenshot(path), pathC);
#endif
}

void MainWindow::tryPlayTrack(const Track& t)
{
    ensurePlayerReady();
    if (m_player == 0 || m_player->engine() == 0) {
        SharedStatusBar::instance()->showMessage(
            _("player.no_media") + qFromUtf8(" ") + t.title, 4000);
        return;
    }
    m_subIdx = -1; // M5-A3:换源字幕序号归位
    m_player->open(t.url);
    m_curUrl = t.url;
    m_player->play(); // 外观层 play() 为 void;失败路径经 noticedError 走状态栏
    if (getenv("QLVOTUBE_AUTOSUB") != 0)
        QTimer::singleShot(800, this, SLOT(onQaAutoSub()));
}

// ── 视频源(providers)接线 ──
void MainWindow::setupVideoProviders()
{
    dbg::loadFromEnv();   /* QXVOTUBE_DEBUG="模块:级别" 生效 */
    m_providers = qxv::createProviders();
    if (m_providers.isEmpty()) {
        if (getenv("QLVOTUBE_DEBUG") != 0)
#if QT_VERSION >= 0x060000
            qDebug() << "providers: stub build (0 sources)";
#else
            qDebug("providers: stub build (0 sources)");
#endif
        sideNav->setVideoSites(QStringList());   /* 隐藏站点入口 */
        return;
    }
    qxv::ProxyConfig pc;
    const char* px = getenv("QXVOTUBE_PROXY");
    if (px && *px) {                             /* http://host:port */
        const QString s = qFromUtf8(px);
        const int c1 = qIndexOf(s, qFromUtf8("://"));
        const int c2 = qLastIndexOf(s, qFromUtf8(":"));
        if (c1 > 0 && c2 > c1) {
            pc.enabled = true;
            pc.host = s.mid(c1 + 3, c2 - c1 - 3);
            pc.port = s.mid(c2 + 1).toInt();
        }
    }
    QStringList ids;
    for (int i = 0; i < m_providers.size(); ++i) {
        m_providers[i]->setProxy(pc);
        ids.append(m_providers[i]->id());
    }
    if (getenv("QLVOTUBE_DEBUG") != 0)
#if QT_VERSION >= 0x060000
        qDebug() << "providers:" << int(m_providers.size()) << "sources"
                 << ids.join(qFromUtf8(","));
#else
        qDebug("providers: %d sources (%s)", int(m_providers.size()),
               qToUtf8(ids.join(qFromUtf8(","))).data());
#endif
    sideNav->setVideoSites(ids);   /* 触发一次 siteChanged(第一个源) */
}

/* M7:默认播放分辨率(配置 video.quality;env QLVOTUBE_QUALITY 可覆盖)。
 * 返回空 = auto(交由 provider 默认);否则返回 1080P/720P/480P/360P。 */
static QString cxResolveQuality(const QString& cfg)
{
    const char* env = getenv("QLVOTUBE_QUALITY");
    QString v = env != 0 && env[0] != '\0' ? qFromUtf8(env) : cfg;
    if (v.isEmpty())
        v = qFromUtf8("360P");
    if (qToLower(v) == qFromUtf8("auto"))   /* 跨 Qt 版本安全 */
        return QString();
    return v;
}

void MainWindow::onSiteChanged(const QString& providerId)
{
    m_curProv = 0;
    for (int i = 0; i < m_providers.size(); ++i)
        if (m_providers[i]->id() == providerId) {
            m_curProv = m_providers[i];
            break;
        }
    if (!m_curProv) return;
    if (getenv("QLVOTUBE_DEBUG") != 0)
#if QT_VERSION >= 0x060000
        qDebug() << "site ->" << providerId;
#else
        qDebug("site -> %s", qToUtf8(providerId).data());
#endif

    m_curProv->cancel();
    m_videoTitles.clear();
    m_videoCards.clear();
    m_videoIds.clear();
    m_videoQueue.clear();
    m_videoIdx = -1;
    m_videoMode = false;
    m_gridChip = QString();
    m_chipList.clear();
    m_gridPage = 1;
    m_gridHasMore = true;
    m_gridLoading = false;
    m_gridIsSearch = false;
    m_gridSearchKw = QString();
    ++m_gridGen;                       /* 作废在途请求 */
    const QString siteKey = QString::fromUtf8("site.") + providerId;
    QString siteName = _(qToUtf8(siteKey).data());
    if (siteName.isEmpty() || siteName == siteKey) siteName = providerId;
    contentView->showVideoGrid(siteName, std::vector<CategoryChip>(), QString());

    qxv::VideoProvider* p = m_curProv;
    const int gen = m_gridGen;
    p->categories([this, providerId, gen](const qxv::Result& r,
                                     const QList36<qxv::CategoryItem>& cats) {
        VideoUiEvent* ev = new VideoUiEvent(VideoUiEvent::Categories);
        ev->gen = gen;
        ev->text = providerId;
        if (r.code == qxv::Err::Ok) {
            for (int i = 0; i < cats.size(); ++i) {
                ev->catKeys.append(cats[i].key);
                ev->catNames.append(cats[i].name);
            }
        } else {
            ev->text += qFromUtf8("|err|") + r.message;
        }
        QApplication::postEvent(this, ev);
    });
}

void MainWindow::loadVideoPage(int page)
{
    if (!m_curProv || m_gridLoading) return;
    m_gridLoading = true;
    contentView->setVideoGridLoading(true);
    qxv::VideoProvider* p = m_curProv;
    const QString chip = m_gridChip;
    const QString site = p->id();
    const int gen = ++m_gridGen;        /* 最新请求胜出,旧响应必丢弃 */
    const bool isSearch = m_gridIsSearch;
    const QString kw = m_gridSearchKw;
    if (getenv("QLVOTUBE_DEBUG") != 0) {
#if QT_VERSION >= 0x060000
        qDebug() << "grid:" << site << "chip" << chip << "page" << page
                 << "search" << isSearch;
#else
        qDebug("grid: %s chip '%s' page %d search %d", qToUtf8(site).data(),
               qToUtf8(chip).data(), page, int(isSearch));
#endif
    }
    qxv::SearchCb done = [this, page, chip, site, gen](
                        const qxv::Result& r,
                        const QList36<qxv::VideoItem>& items, bool hasMore) {
        VideoUiEvent* ev = new VideoUiEvent(VideoUiEvent::Page);
        ev->gen = gen;
        ev->page = page;
        ev->chipKey = chip;
        ev->hasMore = hasMore;
        ev->text = site;
        if (r.code != qxv::Err::Ok) {
            ev->text += qFromUtf8("|err|") + QString::number((int)r.code)
                        + qFromUtf8("|") + r.message;
            QApplication::postEvent(this, ev);
            return;
        }
        for (int i = 0; i < items.size(); ++i) {
            VideoCardData d;
            d.id = items[i].id;
            d.title = items[i].title;
            d.author = items[i].author;
            d.cover = items[i].cover;
            d.durationMs = items[i].durationMs;
            d.publishMs = items[i].publishMs;   /* M7 */
            d.webUrl = items[i].webUrl;         /* M7 */
            d.playCount =
                qMapValue(items[i].extra, qFromUtf8("view"), QVariant())
                    .toLongLong();
            ev->cards.push_back(d);
        }
        if (getenv("QLVOTUBE_DEBUG") != 0) {
#if QT_VERSION >= 0x060000
            qDebug() << "grid:" << site << "chip" << chip << "p" << page
                     << "->" << int(ev->cards.size()) << "cards, more"
                     << hasMore;
#else
            qDebug("grid: %s chip '%s' p%d -> %d cards (more=%d)",
                   qToUtf8(site).data(), qToUtf8(chip).data(), page,
                   int(ev->cards.size()), int(hasMore));
#endif
        }
        QApplication::postEvent(this, ev);
    };
    if (isSearch) p->search(kw, page, done);
    else p->categoryFeed(chip, page, done);
}

void MainWindow::showVideoError(const QString& where, const QString& serverMsg,
                                qxv::Err code)
{
    /* 前(i18n 文案) + 后(服务端详情)，两者都展示 */
    const QString head = _(qToUtf8(qxv::i18nKeyFor(code)).data());
    QString text = head.isEmpty() ? where : head;
    if (!serverMsg.isEmpty()) text += qFromUtf8(" · ") + serverMsg;
#if QT_VERSION >= 0x060000
    qWarning() << "grid error:" << where << "code" << int(code) << serverMsg;
#else
    qWarning("grid error: %s code %d %s", qToUtf8(where).data(),
             int(code), qToUtf8(serverMsg).data());
#endif
    contentView->setVideoGridError(text);
    SharedStatusBar::instance()->showMessage(text, 6000);
}

void MainWindow::onSearchSubmitted(const QString& kw)
{
    if (!m_curProv) return;
    if (getenv("QLVOTUBE_DEBUG") != 0) {
#if QT_VERSION >= 0x060000
        qDebug() << "search:" << kw << "on" << m_curProv->id();
#else
        qDebug("search: %s on %s", qToUtf8(kw).data(),
               qToUtf8(m_curProv->id()).data());
#endif
    }
    m_curProv->cancel();
    ++m_gridGen;
    m_gridIsSearch = true;
    m_gridSearchKw = kw;
    m_gridChip = QString();
    m_gridPage = 1;
    m_gridHasMore = true;
    m_gridLoading = false;
    m_videoCards.clear();
    m_videoIds.clear();
    m_videoQueue.clear();
    m_videoMode = false;
    m_videoIdx = -1;
    contentView->showVideoGrid(
        _("content.search_result").arg(kw),
        m_chipList, QString());
    loadVideoPage(1);
}

void MainWindow::onChipActivated(const QString& key)
{
    if (!m_curProv || key == m_gridChip) return;
    m_gridChip = key;
    m_gridPage = 1;
    m_gridHasMore = true;
    m_gridIsSearch = false;
    ++m_gridGen;                       /* 作废旧分类在途响应 */
    m_curProv->cancel();
    contentView->setActiveChip(key);   /* 先高亮，旧卡片继续显示 */
    contentView->setVideoGridLoading(true);
    m_gridLoading = false;
    loadVideoPage(1);
}

void MainWindow::onGridLoadMore()
{
    if (!m_curProv || m_gridLoading || !m_gridHasMore) return;
    loadVideoPage(m_gridPage);
}

void MainWindow::onVideoActivated(const QString& id)
{
    if (!m_curProv) return;
    int idx = -1;
    for (int i = 0; i < (int)m_videoCards.size(); ++i)
        if (m_videoCards[i].id == id) { idx = i; break; }
    enterVideoMode(idx < 0 ? 0 : idx);
}

void MainWindow::enterVideoMode(int index)
{
    if (m_videoCards.empty()) return;
    if (index < 0) index = 0;
    if (index >= (int)m_videoCards.size()) index = (int)m_videoCards.size() - 1;

    m_videoQueue.clear();
    m_videoIds.clear();
    for (int i = 0; i < (int)m_videoCards.size(); ++i) {
        const VideoCard& c = m_videoCards[i];
        Track t;
        t.id = i;
        t.title = c.title;
        t.artist = c.author;
        t.album = QString();
        t.durationMs = (int)c.durationMs;
        t.coverSeed = QString();
        t.coverUrl = c.cover;
        t.url = QString();              /* 待 resolve */
        t.ncmId = 0;
        t.albumId = 0;
        t.playable = true;
        m_videoQueue.push_back(t);
        m_videoIds.push_back(c.id);
    }
    m_videoMode = true;
    m_videoIdx = index;
    if (queuePanel) {
        queuePanel->setQueue(m_videoQueue, m_videoIdx);
        queuePanel->setCurrentTrack(m_videoQueue[m_videoIdx]);
    }
    if (getenv("QLVOTUBE_DEBUG") != 0) {
#if QT_VERSION >= 0x060000
        qDebug() << "video queue:" << int(m_videoQueue.size())
                 << "start" << m_videoIdx;
#else
        qDebug("video queue: %d start %d", int(m_videoQueue.size()), m_videoIdx);
#endif
    }
    playVideoAt(m_videoIdx);
}

void MainWindow::playVideoAt(int index)
{
    if (!m_curProv || index < 0 || index >= (int)m_videoIds.size()) return;
    m_videoIdx = index;
    const QString id = m_videoIds[index];
    if (queuePanel) queuePanel->setCurrentTrack(m_videoQueue[index]);
    if (playerBar) playerBar->setTrack(m_videoQueue[index]);
    if (getenv("QLVOTUBE_DEBUG") != 0) {
#if QT_VERSION >= 0x060000
        qDebug() << "video activated" << id << "idx" << index;
#else
        qDebug("video activated %s idx %d", qToUtf8(id).data(), index);
#endif
    }
    SharedStatusBar::instance()->showMessage(_("content.resolving"), 3000);
    qxv::VideoProvider* p = m_curProv;
    /* M7:网页地址(右键"在浏览器打开"用) */
    m_curWebUrl = QString();
    for (size_t k = 0; k < m_videoCards.size(); ++k)
        if (m_videoCards[k].id == id) { m_curWebUrl = m_videoCards[k].webUrl; break; }
    /* M7:默认分辨率(360P;auto=传空走源默认) */
    const QString wantQ = cxResolveQuality(Config::value("video.quality"));
    auto onResolved = [this, index](const qxv::Result& r,
                                    const qxv::PlayResult& pr) {
        VideoUiEvent* ev = new VideoUiEvent(VideoUiEvent::Status);
        ev->gen = m_gridGen;
        ev->page = index;
        ev->chipKey = m_videoIds[index];
        if (r.code != qxv::Err::Ok || pr.streams.isEmpty()) {
            ev->text = qFromUtf8("|err|") + QString::number((int)r.code)
                       + qFromUtf8("|") + r.message;
        } else {
            const qxv::Stream& st = pr.streams.first();
            ev->text = st.url;
            ev->referer = qMapValue(st.extra, qFromUtf8("referer"),
                                    qMapValue(st.extra, qFromUtf8("Referer"),
                                              QVariant())).toString();
            ev->userAgent = qMapValue(st.extra, qFromUtf8("userAgent"),
                                      qMapValue(st.extra, qFromUtf8("User-Agent"),
                                                QVariant())).toString();
            ev->proxy = qMapValue(st.extra, qFromUtf8("proxy"),
                                  QVariant()).toString();
            /* M7:实际/请求档位与降级标记 */
            ev->quality = st.quality;
            ev->requestedQuality =
                qMapValue(st.extra, qFromUtf8("requestedQuality"),
                          QVariant()).toString();
            ev->degraded =
                qMapValue(st.extra, qFromUtf8("degraded"), QVariant()).toBool();
        }
        QApplication::postEvent(this, ev);
    };
    if (wantQ.isEmpty())
        p->resolve(id, onResolved);
    else
        p->resolveQuality(id, wantQ, onResolved);
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

    /* —— 视频输出页(M6):选择播放渲染后端 ——
     * 列表按当前构建实际可用的后端生成(cxAvailableVideoOuts),
     * 选项文本就是后端名,无需翻译键;改动在下次启动生效。 */
    CategoryPage* vpage = new CategoryPage(_("settings.videoout_tab"), dlg);
    SelectConfigItem* voutItem = new SelectConfigItem(
        "video/out", cxResolveVideoOut(Config::value("video.out"), QString()), "",
        cxAvailableVideoOuts(), vpage);
    vpage->addLabeledControl(_("settings.videoout_label"), voutItem->comboBox());
    QLabel* vhint = new QLabel(_("settings.videoout_hint"), vpage);
#if QT_VERSION >= 0x040000
    vhint->setWordWrap(true);
#else
    vhint->setAlignment(Qt::WordBreak | Qt::AlignLeft);   /* Qt3 无 setWordWrap */
    vhint->setMinimumHeight(44);                          /* 换行文本给两行高度 */
#endif
    vpage->addWidget(vhint);
    dlg->addCategory(_("settings.videoout_tab"), vpage);
    dlg->registerConfigItem(voutItem);

    /* —— 播放分辨率页(M7):默认 360P —— */
    CategoryPage* qpage = new CategoryPage(_("settings.quality_tab"), dlg);
    QStringList qOpts;
    qOpts << qFromUtf8("auto") << qFromUtf8("1080P") << qFromUtf8("720P")
          << qFromUtf8("480P") << qFromUtf8("360P");
    SelectConfigItem* qItem = new SelectConfigItem(
        "video/quality", cxResolveQuality(Config::value("video.quality")), "",
        qOpts, qpage);
    qpage->addLabeledControl(_("settings.quality_label"), qItem->comboBox());
    QLabel* qhint = new QLabel(_("settings.quality_hint"), qpage);
#if QT_VERSION >= 0x040000
    qhint->setWordWrap(true);
#else
    qhint->setAlignment(Qt::WordBreak | Qt::AlignLeft);
    qhint->setMinimumHeight(44);
#endif
    qpage->addWidget(qhint);
    dlg->addCategory(_("settings.quality_tab"), qpage);
    dlg->registerConfigItem(qItem);

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
    if (changed.contains("video/quality")) {
        Config::setValue("video.quality", changed["video/quality"].toString());
    }
    if (changed.contains("video/out")) {
        /* 后端在启动时创建(挂到 surface),运行期替换涉及引擎重挂,
         * 统一提示重启生效(与 QXVOTUBE_VO_OUT 的语义一致)。 */
        Config::setValue("video.out", changed["video/out"].toString());
        SharedStatusBar::instance()->showMessage(
            _("settings.videoout_hint"), 3000);
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

/* ── M7:顶栏分辨率/档位 ─────────────────────────────────────────── */
void MainWindow::applyVideoInfo()
{
    if (m_player == 0 || videoPane == 0)
        return;
    CxVideoInfo vi = m_player->videoInfo();
    vi.quality = m_curQuality.isEmpty() ? vi.quality : m_curQuality;
    vi.requestedQuality = m_curRequested;
    vi.degraded = m_curDegraded;
    if (vi.url.isEmpty())
        vi.url = m_curUrl;
    m_videoInfo = vi;

    QString res = qFromUtf8("--");
    if (vi.width > 0 && vi.height > 0)
        res = qFromUtf8("%1×%2").arg(vi.width).arg(vi.height);
    QString text = res;
    if (!vi.quality.isEmpty())
        text += qFromUtf8(" · ") + vi.quality;
    videoPane->setVideoInfoText(text);
}

/* 共用:构造右键菜单(返回选中 id;Qt3=QPopupMenu,Qt4+=QMenu) */
static int cxShowMenu(QWidget* parent, const QPoint& pos,
                      const QStringList& items)
{
#ifdef QT3_BUILD
    QPopupMenu m(parent);
    for (int i = 0; i < items.size(); ++i)
        m.insertItem(items[i], i + 1);
    return m.exec(pos) - 1;      // -1 = 未选中
#else
    QMenu m(parent);
    QList<QAction*> acts;
    for (int i = 0; i < items.size(); ++i)
        acts.append(m.addAction(items[i]));
    QAction* sel = m.exec(pos);
    for (int i = 0; i < acts.size(); ++i)
        if (acts[i] == sel)
            return i;
    return -1;
#endif
}

/* 列表卡片右键:复制网页地址 / 复制直链 / 显示 / 浏览器打开 */
void MainWindow::onVideoContextMenu(const QString& id, const QPoint& globalPos)
{
    QString webUrl;
    for (size_t i = 0; i < m_videoCards.size(); ++i)
        if (m_videoCards[i].id == id) { webUrl = m_videoCards[i].webUrl; break; }
    QStringList items;
    items << qFromUtf8("复制网页地址")
          << qFromUtf8("复制直链地址(会过期)")
          << qFromUtf8("显示地址…")
          << qFromUtf8("在浏览器打开");
    const int sel = cxShowMenu(this, globalPos, items);
    if (sel == 0) {
        QApplication::clipboard()->setText(webUrl.isEmpty() ? id : webUrl);
        SharedStatusBar::instance()->showMessage(
            webUrl.isEmpty() ? qFromUtf8("该源未提供网页地址,已复制 id")
                             : qFromUtf8("已复制网页地址"), 3000);
    } else if (sel == 1) {
        if (m_curUrl.isEmpty() || m_videoInfo.url.isEmpty()) {
            SharedStatusBar::instance()->showMessage(
                qFromUtf8("请先播放该视频,直链需解析后才有"), 4000);
        } else {
            QApplication::clipboard()->setText(m_videoInfo.url);
            SharedStatusBar::instance()->showMessage(
                qFromUtf8("已复制直链(带签名,可能过期)"), 4000);
        }
    } else if (sel == 2) {
        ToastWidget::showCenter(this, webUrl.isEmpty() ? id : webUrl,
                                ToastWidget::Info, 4000);
    } else if (sel == 3) {
        if (!webUrl.isEmpty())
            qOpenUrl(webUrl);
        else
            SharedStatusBar::instance()->showMessage(
                qFromUtf8("该源未提供网页地址"), 3000);
    }
}

/* 播放器右键:视频详情 / 复制直链 / 浏览器打开 */
void MainWindow::onPlayerContextMenu(const QPoint& globalPos)
{
    QStringList items;
    items << qFromUtf8("视频详情…")
          << qFromUtf8("复制直链地址(会过期)")
          << qFromUtf8("在浏览器打开");
    const int sel = cxShowMenu(this, globalPos, items);
    if (sel == 0) {
        applyVideoInfo();
        VideoInfoDialog dlg(m_videoInfo, this);
        dlg.exec();
    } else if (sel == 1) {
        if (!m_videoInfo.url.isEmpty()) {
            QApplication::clipboard()->setText(m_videoInfo.url);
            SharedStatusBar::instance()->showMessage(
                qFromUtf8("已复制直链(带签名,可能过期)"), 4000);
        }
    } else if (sel == 2) {
        if (!m_curWebUrl.isEmpty())
            qOpenUrl(m_curWebUrl);
        else
            SharedStatusBar::instance()->showMessage(
                qFromUtf8("该源未提供网页地址"), 3000);
    }
}

void MainWindow::onPlayerFinished()
{
    if (playerBar) playerBar->setPlaying(false);
    /* 简易连播:顺序换下一首(引擎 finished 后 UI 流迁移) */
    if (!m_argvUrls.isEmpty() && m_curPlaylistId < 0) {
        playArgvAt(m_argvIdx + 1);   /* 到尾自动停("列表播完") */
        return;
    }
    onNext();
}

void MainWindow::onPlayerPosition(long ms)
{
    if (getenv("QLVOTUBE_DEBUG") != 0) {
        static long lastSec = -1;          /* 1 秒一条,避免 33ms 刷屏 */
        const long sec = ms / 1000;
        if (sec != lastSec) {
            lastSec = sec;
#if QT_VERSION >= 0x060000
            qDebug() << "pos=" << ms;
#else
            qDebug("pos=%ld", ms);
#endif
        }
    }
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

void MainWindow::onMuteChanged(bool on)
{
    if (m_player != 0) m_player->setMute(on);
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
    const QString dir = cxShotDir();
#if defined(QT3_BUILD) || (defined(QT_VERSION) && QT_VERSION < QT_VERSION_CHECK(5, 0, 0))
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
#if QT_VERSION >= 0x060000
        qDebug() << "shot:" << path << "ok" << int(ok);
#else
        qDebug("shot: %s ok=%d", pathC, (int)ok);
#endif
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
    m_player->setRenderer(m_videoRenderer);
    videoPane->setEngineLabel(QString::fromLatin1(m_player->engine()->name()));
    if (!m_curUrl.isEmpty()) {
        m_player->open(m_curUrl);
        m_player->play();
        switchToPlayerView(true);
    }
    SharedStatusBar::instance()->showMessage(
        qFromUtf8("当前引擎: ") + QString::fromLatin1(m_player->engine()->name()), 3000);
}

/* M5-A3:字幕轨循环(关 → 1 → 2 … → 关);内核不支持则如实提示 */
void MainWindow::onCycleSubtitle()
{
    if (m_player == 0 || m_player->engine() == 0) {
        SharedStatusBar::instance()->showMessage(qFromUtf8("播放引擎未接入"), 2000);
        return;
    }
    int n = m_player->subtitleTrackCount();
    if (n <= 0) {
        SharedStatusBar::instance()->showMessage(qFromUtf8("当前媒体无字幕轨"), 2500);
        return;
    }
    m_subIdx++;
    if (m_subIdx >= n)
        m_subIdx = -1;
    if (getenv("QLVOTUBE_DEBUG") != 0)
#if QT_VERSION >= 0x060000
        qDebug() << "sub-cycle: n=" << n << "idx" << m_subIdx;
#else
        qDebug("sub-cycle: n=%d idx=%d", n, m_subIdx);
#endif
    m_player->setSubtitleTrack(m_subIdx);
    SharedStatusBar::instance()->showMessage(
        m_subIdx < 0 ? qFromUtf8("字幕: 关")
                     : qFromUtf8("字幕: 轨 ") + QString::number(m_subIdx + 1),
        2000);
}

void MainWindow::onLoadSubtitleFile()
{
    if (m_player == 0 || m_player->engine() == 0) {
        SharedStatusBar::instance()->showMessage(qFromUtf8("播放引擎未接入"), 2000);
        return;
    }
#ifdef QT3_BUILD
    // Qt3 静态签名为 (startWith, filter, parent, name, caption,...)
    QString f = QFileDialog::getOpenFileName(QString::null,
                                             qFromUtf8("字幕 (*.srt *.ass)"),
                                             this, 0,
                                             qFromUtf8("加载外挂字幕"));
#else
    QString f = QFileDialog::getOpenFileName(this, qFromUtf8("加载外挂字幕"),
                                             QString(), qFromUtf8("字幕 (*.srt *.ass)"));
#endif
    if (f.isEmpty())
        return;
    if (m_player->setSubtitleFile(f))
        SharedStatusBar::instance()->showMessage(qFromUtf8("字幕已加载: ") + f, 3000);
    else
        SharedStatusBar::instance()->showMessage(qFromUtf8("字幕加载失败(内核不支持)"), 3000);
}

void MainWindow::onQaAutoSub()
{
    int n = m_player != 0 ? m_player->subtitleTrackCount() : 0;
    if (getenv("QLVOTUBE_DEBUG") != 0)
#if QT_VERSION >= 0x060000
        qDebug() << "autosub: n=" << n;
#else
        qDebug("autosub: n=%d", n);
#endif
    if (n > 0) {
        m_subIdx = 0;
        m_player->setSubtitleTrack(0);
    }
}

void MainWindow::onSubtitleText(const QString& text)
{
#ifdef QT3_BUILD
    const char* textC = text.latin1();
#else
    QByteArray textBa = text.toUtf8();
    const char* textC = textBa.constData();
#endif
    if (getenv("QLVOTUBE_DEBUG") != 0)
#if QT_VERSION >= 0x060000
        qDebug() << "subtitle-text: [" << text << "]";
#else
        qDebug("subtitle-text: [%s]", textC);
#endif
    if (videoPane != 0)
        videoPane->setSubtitleText(text);
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
    if (m_videoMode && !m_videoIds.empty()) {
        const int n = (int)m_videoIds.size();
        playVideoAt((m_videoIdx - 1 + n) % n);   /* 回绕,与音频一致 */
        return;
    }
    if (!m_argvUrls.isEmpty() && m_curPlaylistId < 0) {
        playArgvAt(m_argvIdx - 1);   /* 到头回绕 */
        return;
    }
    const Playlist* pl = findPlaylist(m_library, m_curPlaylistId);
    if (pl == 0 || pl->tracks.empty())
        return;
    /* ⏮ 到头回绕(二期同步 qskvotube) */
    int idx = m_curTrackIndex > 0
                  ? m_curTrackIndex - 1 : (int)pl->tracks.size() - 1;
    showTrackStub(m_curPlaylistId, idx);
}

void MainWindow::onNext()
{
    if (m_videoMode && !m_videoIds.empty()) {
        if (m_videoIdx + 1 >= (int)m_videoIds.size()) {
            if (playerBar) playerBar->setPlaying(false);   /* 播完停 */
            return;
        }
        playVideoAt(m_videoIdx + 1);
        return;
    }
    if (!m_argvUrls.isEmpty() && m_curPlaylistId < 0) {
        playArgvAt(m_argvIdx + 1);
        return;
    }
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

/* 拖拽导入:拖入本地文件 → 并入 argv 播放列表并起播首个新文件。
 * argv 模式未启用时以拖入集合启用之;复用 playArgvAt(回绕/播完停)。 */
void MainWindow::dragEnterEvent(QDragEnterEvent* e)
{
#ifdef QT3_BUILD
    /* Qt3.2 无 mimeData/acceptProposedAction:走 QUriDrag(qdragobject.h) */
    if (QUriDrag::canDecode(e))
        e->accept();
#else
    const QList<QUrl> urls = e->mimeData()->urls();
    for (int i = 0; i < urls.size(); i++) {
        if (urls[i].isLocalFile()) {
            e->acceptProposedAction();   /* 本地文件即接受,后缀宽容 */
            return;
        }
    }
#endif
}

void MainWindow::dropEvent(QDropEvent* e)
{
    QStringList files;
#ifdef QT3_BUILD
    QStrList uris;
    if (QUriDrag::decode(e, uris)) {
        /* Qt3 QStrList 迭代经典式( iterator 无 const char* 隐式转换) */
        QStrListIterator it(uris);
        for (const char* u = it.toFirst(); u != 0; u = it.current(), ++it) {
            const char* lf = QUriDrag::uriToLocalFile(u);
            if (lf != 0) {
                const QString f = QString::fromLocal8Bit(lf);
                if (!f.isEmpty() && QFile::exists(f))
                    files.append(f);   /* Qt3 QStringList 无 <<(QStringList) */
            }
        }
    }
#else
    const QList<QUrl> urls = e->mimeData()->urls();
    for (int i = 0; i < urls.size(); i++) {
        const QString f = urls[i].toLocalFile();
        if (!f.isEmpty() && QFile::exists(f))
            files << f;
    }
#endif
    if (files.isEmpty())
        return;
    /* Qt3 QStringList 无 <<(QStringList);逐个 append 两版通用 */
    for (int i = 0; i < files.size(); ++i)
        m_argvUrls.append(files[i]);
    const int base = m_argvUrls.size() - files.size();
    m_curPlaylistId = -1;
    playArgvAt(base);   /* 从首个新文件起播 */
#ifdef QT3_BUILD
    e->accept();
#else
    e->acceptProposedAction();
#endif
}

void MainWindow::customEvent(CustomEventBase* event)
{
    if (event->type() == VideoUiEventType) {
        VideoUiEvent* e = static_cast<VideoUiEvent*>(event);
        if (e->gen != m_gridGen) {          /* 切源/切分类后旧响应 */
            if (getenv("QLVOTUBE_DEBUG") != 0) {
#if QT_VERSION >= 0x060000
                qDebug() << "stale event dropped: kind" << int(e->kind)
                         << "gen" << e->gen << "!=" << m_gridGen;
#else
                qDebug("stale event dropped: kind %d gen %d != %d",
                       int(e->kind), e->gen, m_gridGen);
#endif
            }
            return;
        }
        if (e->kind == VideoUiEvent::Categories) {
            QString site = e->text;
            const int sep = qIndexOf(site, qFromUtf8("|err|"));
            QString why;
            if (sep >= 0) {
                why = site.mid(sep + 5);
                site = site.left(sep);
#if QT_VERSION >= 0x060000
                qWarning() << "categories failed:" << site << why;
#else
                qWarning("categories failed: %s %s", qToUtf8(site).data(),
                         qToUtf8(why).data());
#endif
            }
            std::vector<CategoryChip> chips;
            for (int i = 0; i < e->catKeys.size(); ++i)
                chips.push_back(CategoryChip(e->catKeys[i], e->catNames[i]));
            if (chips.empty())   /* 无分类能力 → 仅推荐(homeFeed) */
                chips.push_back(CategoryChip(QString(), QString()));
            if (getenv("QLVOTUBE_DEBUG") != 0) {
#if QT_VERSION >= 0x060000
                qDebug() << "chips:" << int(chips.size())
                         << "active" << chips.front().key;
#else
                qDebug("chips: %d active '%s'", int(chips.size()),
                       qToUtf8(chips.front().key).data());
#endif
            }
            m_gridChip = chips.front().key;
            m_chipList = chips;
            contentView->setVideoChips(chips, m_gridChip);
            m_gridPage = 1;
            m_gridLoading = false;
            loadVideoPage(1);
            return;
        }
        if (e->kind == VideoUiEvent::Page) {
            const int sep = qIndexOf(e->text, qFromUtf8("|err|"));
            if (sep >= 0) {
                const QString site = e->text.left(sep);
                const QString rest = e->text.mid(sep + 5);
                const int code = rest.section(QChar('|'), 0, 0).toInt();
                const QString msg = rest.section(QChar('|'), 1);
                m_gridLoading = false;
                showVideoError(site + qFromUtf8(" p")
                                   + QString::number(e->page), msg,
                               (qxv::Err)code);
                return;
            }
            std::vector<VideoCard> cards;
            for (int i = 0; i < (int)e->cards.size(); ++i) {
                VideoCard c;
                c.id = e->cards[i].id;
                c.title = e->cards[i].title;
                c.author = e->cards[i].author;
                c.cover = e->cards[i].cover;
                c.durationMs = e->cards[i].durationMs;
                c.playCount = e->cards[i].playCount;
                c.publishMs = e->cards[i].publishMs;   /* M7 */
                c.webUrl = e->cards[i].webUrl;         /* M7 */
                cards.push_back(c);
                if (!c.id.isEmpty())
                    m_videoTitles.insert(c.id, c.title);
            }
            if (e->page <= 1) {
                /* 第 1 页:整体替换(切分类/搜索首屏),旧卡片保留到此刻 */
                m_videoCards = cards;
                contentView->replaceVideoCards(cards, e->hasMore);
            } else {
                for (size_t i = 0; i < cards.size(); ++i)
                    m_videoCards.push_back(cards[i]);
                contentView->appendVideoCards(cards, e->hasMore);
            }
            m_gridHasMore = e->hasMore;
            m_gridPage = e->page + 1;
            m_gridLoading = false;
            return;
        }
        if (e->kind == VideoUiEvent::Status) {
            if (e->text.startsWith(qFromUtf8("|err|"))) {
                const QString rest = e->text.mid(5);
                const int code = rest.section(QChar('|'), 0, 0).toInt();
                const QString msg = rest.section(QChar('|'), 1);
                const QString head =
                    _(qToUtf8(qxv::i18nKeyFor((qxv::Err)code)).data());
                QString text = head;
                if (!msg.isEmpty()) text += qFromUtf8(" · ") + msg;
                SharedStatusBar::instance()->showMessage(text, 6000);
                return;
            }
            Track t;
            if (m_videoMode && m_videoIdx >= 0
                && m_videoIdx < (int)m_videoQueue.size()) {
                t = m_videoQueue[m_videoIdx];   /* 保留标题/时长/封面/队列语义 */
                t.url = e->text;
                t.playable = true;
            } else {
                t.id = -1;
                t.title = qMapValue(m_videoTitles, e->chipKey, e->chipKey);
                t.url = e->text;
                t.playable = true;
                t.durationMs = 0;
                t.ncmId = 0;
                t.albumId = 0;
            }
            if (m_player) {
                CxNetOptions no;
                no.referer = e->referer;
                no.userAgent = e->userAgent;
                no.proxy = e->proxy;
                if (no.proxy.isEmpty()) no.proxy = qenv("QXVOTUBE_PROXY");
                m_player->setNetOptions(no);
                if (getenv("QLVOTUBE_DEBUG") != 0) {
#if QT_VERSION >= 0x060000
                    /* 字段名与 Qt3 分支保持一致（Qt6 删除 printf 重载,只能流式） */
                    qDebug() << "media net: referer=" << no.referer
                             << "ua=" << (no.userAgent.isEmpty() ? "0" : "1")
                             << "proxy=" << no.proxy;
#else
                    qDebug("media net: referer='%s' ua=%d proxy='%s'",
                           qToUtf8(no.referer).data(),
                           int(!no.userAgent.isEmpty()),
                           qToUtf8(no.proxy).data());
#endif
                }
            }
            m_curQuality = e->quality;              /* M7 */
            m_curRequested = e->requestedQuality;
            m_curDegraded = e->degraded;
            tryPlayTrack(t);
            switchToPlayerView(true);
            if (m_curDegraded && !m_curRequested.isEmpty()) {
                SharedStatusBar::instance()->showMessage(
                    QString::fromUtf8("请求 %1,实际 %2(该源上限)")
                        .arg(m_curRequested, m_curQuality), 4000);
            }
            applyVideoInfo();
            return;
        }
        return;   /* postEvent 投递的事件由事件循环自动删除 */
    }
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

