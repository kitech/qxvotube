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
#include "filecache.h"
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
#include "downloader.h"
#include "providers/common/fingerprint.h"
#include "queue_store.h"
#include "providers/common/debuglog.h"   /* QXVOTUBE_DEBUG → providers 分级日志 */
#include "videoui.h"
#include "videothumbcache.h"
#include "tvlist.h"
#include "translator.h"

#include "qlcompat34.h"
#include "cxvideoplayer.h"
#include "cxengine.h"
#include "renderer_gl.h"     /* M5-A:ffmpeg 路 GL 渲染器 */
#include "cxdpr.h"            /* 视频详情:设备像素比 dpr */
#include "videorendererfactory.h"   /* M6:输出后端可配置(cxvideo-vo.md) */
#include "videoinfodialog.h"        /* M7:视频详情弹窗 */
#include "commentdialog.h"
#include "subtitledialog.h"          /* 字幕列表窗口 */
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
#include <qcombobox.h>
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
#include <cmath>
#include <stdint.h>
#include <qcolor.h>

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

/* ===== 视频输出页播放测试:内嵌显示 + 跟随所选后端 ===== */
/* 测试媒体目录(与截图共用 ~/.cache/qlvotube) */
static QString cxTestMediaDir()
{
    QString dir = qGetHomePath() + "/.cache/qlvotube";
#ifdef QT3_BUILD
    QDir d; d.mkdir(dir);
#else
    QDir().mkpath(dir);
#endif
    return dir;
}

/* 2s 440Hz 正弦 S16 双声道 WAV(移植 qlmusic writeTestWav) */
static bool writeTestWav(const QString& path, int seconds)
{
    const int sr = 44100, ch = 2;
    const int dataBytes = sr * ch * 2 * seconds;
    std::ofstream f(qToUtf8(path).data(), std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        return false;
    }
    uint32_t sampleRate = sr, byteRate = sr * ch * 2;
    uint16_t blockAlign = ch * 2, bits = 16, fmtTag = 1;
    f.write("RIFF", 4);
    { uint32_t v = 36 + dataBytes; f.write((char*)&v, 4); }
    f.write("WAVEfmt ", 8);
    { uint32_t v = 16; f.write((char*)&v, 4); }
    f.write((char*)&fmtTag, 2);
    f.write((char*)&ch, 2);
    f.write((char*)&sampleRate, 4);
    f.write((char*)&byteRate, 4);
    f.write((char*)&blockAlign, 2);
    f.write((char*)&bits, 2);
    f.write("data", 4);
    { uint32_t v = dataBytes; f.write((char*)&v, 4); }
    const double pi = 3.14159265358979323846;
    for (int i = 0; i < sr * seconds; i++) {
        int16_t s = (int16_t)(sin(2.0 * pi * 440.0 * i / sr) * 0.25 * 32767.0);
        f.write((char*)&s, 2);
        f.write((char*)&s, 2);
    }
    return f.good();
}

/* 2s 彩条 + 440Hz mp4(复用 cxvideo selftest 的 lavfi 命令) */
static bool writeTestVideo(const QString& path)
{
    const QString cmd = QString("ffmpeg -f lavfi -i testsrc=duration=2:size=640x360:rate=25 "
                                "-f lavfi -i sine=frequency=440:duration=2 "
                                "-map 0:v -map 1:a -pix_fmt yuv420p -shortest \"%1\" -y "
                                "2>/dev/null </dev/null").arg(path);
    return system(qToUtf8(cmd).data()) == 0;
}

/* 精简视频 surface:黑底,resize 同步 renderer 几何,切换时清理旧件 */
class TestVideoSurface : public QWidget {
public:
    explicit TestVideoSurface(QWidget* parent = 0) : QWidget(parent), m_renderer(0)
    {
#ifdef QT3_BUILD
        setBackgroundColor(QColor(0, 0, 0));
#else
        setAutoFillBackground(true);
        QPalette p = palette();
        p.setColor(QPalette::Window, QColor(0, 0, 0));
        setPalette(p);
#endif
        setMinimumSize(320, 180);
    }
    void setRendererWidget(QWidget* w)
    {
        if (m_renderer != 0 && m_renderer != w) {
            delete m_renderer;
        }
        m_renderer = w;
        if (w != 0) {
            w->setGeometry(0, 0, width(), height());
            w->show();
            w->raise();
        }
    }
    void clearRenderer() { setRendererWidget(0); }
protected:
    void resizeEvent(QResizeEvent* e)
    {
        QWidget::resizeEvent(e);
        if (m_renderer != 0) {
            m_renderer->setGeometry(0, 0, width(), height());
        }
    }
private:
    QWidget* m_renderer;
};

/* 测试控件:内嵌 surface + 独立 player,后端源 = 传入的 combo(即 voutItem) */
class TestPlaybackWidget : public QWidget {
public:
    TestPlaybackWidget(QComboBox* backendCombo, QWidget* parent = 0)
        : QWidget(parent), m_combo(backendCombo), m_player(0), m_renderer(0)
    {
        QBoxLayout* lay = qNewBoxLayout(this, QBoxLayout::TopToBottom, 4, 4);
        m_surface = new TestVideoSurface(this);
        lay->addWidget(m_surface, 1);
        QPushButton* audioBtn = new QPushButton(_("settings.test_audio_btn"), this);
        LambdaSlot* audioSlot = new LambdaSlot(this, [this]() { playTest(true); });
        connect(audioBtn, SIGNAL(clicked()), audioSlot, SLOT(call()));
        lay->addWidget(audioBtn);
        QPushButton* videoBtn = new QPushButton(_("settings.test_video_btn"), this);
        LambdaSlot* videoSlot = new LambdaSlot(this, [this]() { playTest(false); });
        connect(videoBtn, SIGNAL(clicked()), videoSlot, SLOT(call()));
        lay->addWidget(videoBtn);
    }
    ~TestPlaybackWidget()
    {
        if (m_player != 0) {
            m_player->stop();
            delete m_player;
        }
    }
private:
    /* 按对话框当前选中后端确保就绪;out=实际生效后端,why=回退/失败原因 */
    bool ensureReady(QString* out, QString* why)
    {
        const QString want = (m_combo != 0) ? m_combo->currentText() : qFromUtf8("auto");
        if (m_player != 0 && m_player->engine() != 0 && want == m_backend) {
            *out = m_backend;
            *why = m_why;
            return true;
        }
        if (m_player != 0) {
            m_player->stop();
            delete m_player;
            m_player = 0;
        }
        m_surface->clearRenderer();
        m_renderer = 0;
        CxVideoOut vo = cxCreateVideoOut(m_surface, want, QString());
        if (vo.widget == 0) {
            *why = vo.why;
            return false;
        }
        m_surface->setRendererWidget(vo.widget);
        m_renderer = vo.renderer;
        m_backend = want;
        m_why = vo.why;
        m_player = new CXVideoPlayer(this);
        if (!m_player->setEngine(CxEngineFfmpeg)) {
            *why = qFromUtf8("ffmpeg 引擎不可用");
            m_surface->clearRenderer();
            delete m_player;
            m_player = 0;
            return false;
        }
        m_player->setHostWidget(m_surface);
        m_player->setRenderer(m_renderer);
        *out = m_backend;
        *why = m_why;
        return true;
    }
    void playTest(bool audio)
    {
        QString out, why;
        if (!ensureReady(&out, &why)) {
            SharedStatusBar::instance()->showMessage(_("settings.test_unsupported").arg(why), 4000);
            return;
        }
        const QString p = cxTestMediaDir() + (audio ? "/audio_test.wav" : "/video_test.mp4");
        const bool ok = audio ? writeTestWav(p, 2) : writeTestVideo(p);
        if (!ok) {
            SharedStatusBar::instance()->showMessage(
                audio ? _("settings.test_gen_fail") : _("settings.test_ffmpeg_fail"), 4000);
            return;
        }
        m_player->open(p);
        m_player->play();
        SharedStatusBar::instance()->showMessage(
            (audio ? _("settings.test_audio_playing") : _("settings.test_video_playing")).arg(out), 3000);
    }
    QComboBox* m_combo;
    TestVideoSurface* m_surface;
    CXVideoPlayer* m_player;
    CXVideoRenderer* m_renderer;
    QString m_backend;
    QString m_why;
};

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
    , m_commentDlg(0)
    , m_commentProv(0)
    , m_subDlg(0)
    , m_subCueIdx(-1)
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

    /* 封面磁盘缓存:初始化 + 启动后异步 trim(不阻塞启动) */
    filecache::init(dataDir);
    thumbcache::refreshImageLimit();   /* GUI 线程读配置,泵线程只读原子值 */
    filecache::requestTrim(filecache::coversDir(), thumbcache::imageLimitBytes());

    setGeometry(100, 50, 980, 640);
    /* mac/Qt4:布局最小宽可能把窗口顶到数屏宽 → 解除布局尺寸约束 */
#ifndef QT3_BUILD
    if (layout()) layout()->setSizeConstraint(QLayout::SetNoConstraint);
#endif
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
    filecache::shutdown();        // 停后台 trim 线程(有界 join)
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
    QObject::connect(sideNav, SIGNAL(videoCategorySelected(const QString&)),
                     this, SLOT(onVideoCategorySelected(const QString&)));
    qApp->installEventFilter(this);   /* 全局快捷键(搜索框聚焦时让行) */
    QObject::connect(contentView, SIGNAL(videoActivated(const QString&)),
                     this, SLOT(onVideoActivated(const QString&)));
    QObject::connect(contentView, SIGNAL(commentBadgeClicked(const QString&)),
                     this, SLOT(openComments(const QString&)));
    QObject::connect(contentView, SIGNAL(loadMoreRequested()),
                     this, SLOT(onGridLoadMore()));
    QObject::connect(contentView, SIGNAL(refreshRequested()),
                     this, SLOT(onListRefresh()));
    QObject::connect(sideNav, SIGNAL(searchSubmitted(const QString&)),
                     this, SLOT(onSearchSubmitted(const QString&)));

    /* 侧栏左上用户区（登录占位） */
    QObject::connect(sideNav, SIGNAL(loginRequested()),
                     this, SLOT(onUserLoginPlaceholder()));
    QObject::connect(sideNav, SIGNAL(actionTriggered(int)),
                     this, SLOT(onNavAction(int)));
    QObject::connect(sideNav, SIGNAL(netTvRegionSelected(const QString&)),
                     this, SLOT(onNetTvRegion(const QString&)));

    setupVideoProviders();

    /* 播放模式/音量恢复(qlmusic:config.json 持久化) */
    m_dl = new Downloader(this);
    /* 缓冲进度独立于播放进度刷新(播放/暂停都继续更新) */
    m_bufTimer = new QTimer(this);
    QObject::connect((QTimer*)m_bufTimer, SIGNAL(timeout()),
                     this, SLOT(onRefreshBuffered()));
    m_repeatMode = Config::value("player.repeat").toInt();
    m_shuffle = Config::value("player.shuffle") == qFromUtf8("1");
    {
        const QString as = Config::value("player.autoskip");
        m_autoSkip = as.isEmpty() ? true : (as != qFromUtf8("0"));
    }
    {
        const QString vol = Config::value("player.volume");
        const int v = vol.isEmpty() ? 100 : vol.toInt();
        if (playerBar) playerBar->setVolume(v);
        if (m_player) m_player->setVolume(v);
    }
    if (playerBar) {
        playerBar->setRepeatMode(m_repeatMode);
        playerBar->setShuffle(m_shuffle);
        playerBar->setAutoSkip(m_autoSkip);
    }
    restoreQueueSnapshot();

    if (playerBar) {
        QObject::connect(playerBar, SIGNAL(playPauseRequested()),
                         this, SLOT(onPlayPause()));
        QObject::connect(playerBar, SIGNAL(prevRequested()),
                         this, SLOT(onPrev()));
        QObject::connect(playerBar, SIGNAL(nextRequested()),
                         this, SLOT(onNext()));
        QObject::connect(playerBar, SIGNAL(seekRequested(int)),
                         this, SLOT(onSeekRequested(int)));
        QObject::connect(playerBar, SIGNAL(videoSurfaceRequested()),
                         this, SLOT(onVideoSurfaceRequested()));
        QObject::connect(playerBar, SIGNAL(volumeChanged(int)),
                         this, SLOT(onVolumeChanged(int)));
        QObject::connect(playerBar, SIGNAL(muteChanged(bool)),
                         this, SLOT(onMuteChanged(bool)));
        QObject::connect(playerBar, SIGNAL(repeatClicked()),
                         this, SLOT(onRepeatClicked()));
        QObject::connect(playerBar, SIGNAL(shuffleClicked()),
                         this, SLOT(onShuffleClicked()));
        QObject::connect(playerBar, SIGNAL(autoSkipClicked()),
                         this, SLOT(onAutoSkipClicked()));
        QObject::connect(playerBar, SIGNAL(rateChanged(double)),
                         this, SLOT(onRateChanged(double)));
        QObject::connect(playerBar, SIGNAL(rateMenuRequested(const QPoint&)),
                         this, SLOT(onRateMenuRequested(const QPoint&)));
        QObject::connect(playerBar, SIGNAL(rotateMenuRequested(const QPoint&)),
                         this, SLOT(onRotateMenuRequested(const QPoint&)));
        QObject::connect(playerBar, SIGNAL(eqRequested()),
                         this, SLOT(onOpenEqDialog()));
        QObject::connect(playerBar, SIGNAL(shotRequested()),
                         this, SLOT(onTakeScreenshot()));
        QObject::connect(playerBar, SIGNAL(subtitleRequested()),
                         this, SLOT(onCycleSubtitle()));
        QObject::connect(playerBar, SIGNAL(subtitleListRequested()),
                         this, SLOT(openSubtitleList()));
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
    /* 画面区交互:单击暂停/恢复,双击=F,中键/滚轮提示未实现 */
    QObject::connect(videoPane, SIGNAL(togglePlayPauseRequested()),
                     this, SLOT(onPlayPause()));
    QObject::connect(videoPane, SIGNAL(toggleFullscreenRequested()),
                     this, SLOT(onVideoSurfaceToggleFullscreen()));
    QObject::connect(videoPane, SIGNAL(surfaceHint(int)),
                     this, SLOT(onVideoSurfaceHint(int)));
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
    addMenuItem(tool, this, SLOT(onDownloadCurrent()), "menu.download");
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
    saveQueueSnapshot();   /* 兜底:队列快照 */
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
    m_player->openAsync(t.url);   /* 异步加载:慢网不阻塞 GUI;完成经 started/noticedError 回报 */
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
    // 全局代理来源优先级：QXVOTUBE_PROXY 环境变量(最高、普遍适用) > 设置页 proxy.enabled/url
    QString proxySpec = qFromUtf8(getenv("QXVOTUBE_PROXY") ? getenv("QXVOTUBE_PROXY") : "");
    if (proxySpec.isEmpty()
        && Config::value(qFromUtf8("proxy.enabled")) == qFromUtf8("1"))
        proxySpec = Config::value(qFromUtf8("proxy.url"));
    if (!proxySpec.isEmpty()) {                  /* [scheme://][user:pass@]host:port */
        const QString s = proxySpec;
        QString rest = s;
        const int sep = qIndexOf(s, qFromUtf8("://"));
        if (sep > 0) {
            const QString scheme = s.left(sep);
            if (scheme == qFromUtf8("socks5") || scheme == qFromUtf8("socks5h"))
                pc.type = qxv::ProxyConfig::Socks5;
            rest = s.mid(sep + 3);
        }
        const int at = qLastIndexOf(rest, qFromUtf8("@"));   // user:pass@
        if (at > 0) {
            const QString cred = rest.left(at);
            rest = rest.mid(at + 1);
            const int colon = qIndexOf(cred, qFromUtf8(":"));
            if (colon > 0) { pc.user = cred.left(colon); pc.pass = cred.mid(colon + 1); }
            else           { pc.user = cred; }
        }
        const int c2 = qLastIndexOf(rest, qFromUtf8(":"));   // host:port
        if (c2 > 0) {
            pc.enabled = true;
            pc.host = rest.left(c2);
            pc.port = rest.mid(c2 + 1).toInt();
        }
    }
    m_proxyCfg = pc;   /* 供网络电视频道 mustProxy 解析(env > 设置页,同 provider) */
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
    m_netTvRegion = QString();          /* 离开网络电视,回到站点浏览 */
    sideNav->setCurrentNetTvRegion(QString());
    if (contentView) contentView->setPlayingVideoId(QString());
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
    sideNav->clearVideoCategories();
    m_catLoadSite = providerId;            /* 旧站点在途回调不再关当前 loading */
    m_catLoadPending = 2;                  /* categories() + categoriesRefresh() 各一次 */
    sideNav->setVideoCategoriesLoading(true);
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
    m_gridSiteName = siteName;         /* 视频列表页标题:站点部分 */
    contentView->showVideoGrid(siteName);

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
    // 分类秒显后,后台拉在线分类做增量合并(在线优先去重),回来就地刷新 chips。
    // 该事件不携带分页数据:loadVideoPage 会递增 m_gridGen,若沿用同一 gen 校验
    // 会被误判为陈旧而丢弃,故跳过 gen 校验、在 customEvent 改用站点 id 校验。
    p->categoriesRefresh([this, providerId, gen](const qxv::Result& r,
                                     const QList36<qxv::CategoryItem>& cats) {
        // 失败/空也投递:UI 用它递减 m_catLoadPending 并关闭分类加载标识;
        // 空列表在 customEvent 里被忽略,chips 保持本地分类不变。
        VideoUiEvent* ev = new VideoUiEvent(VideoUiEvent::CategoriesRefresh);
        ev->gen = gen;
        ev->text = providerId;
        if (r.code == qxv::Err::Ok) {
            for (int i = 0; i < cats.size(); ++i) {
                ev->catKeys.append(cats[i].key);
                ev->catNames.append(cats[i].name);
            }
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
            if (d.playCount <= 0) d.playCount = items[i].stats.viewCount;
            d.replyCount = items[i].stats.replyCount;
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
    if (contentView) contentView->setPlayingVideoId(QString());
    m_videoMode = false;
    m_videoIdx = -1;
    contentView->showVideoGrid(_("content.search_result").arg(kw));
    loadVideoPage(1);
}

QString MainWindow::videoGridTitle() const
{
    if (m_chipList.empty())
        return m_gridSiteName;                 /* 分类未到:先只显示站点 */
    QString cat;
    for (int i = 0; i < (int)m_chipList.size(); ++i)
        if (m_chipList[i].key == m_gridChip) { cat = m_chipList[i].name; break; }
    if (cat.isEmpty())
        cat = _("content.cat_recommend");      /* 空分类回退“推荐” */
    if (m_gridSiteName.isEmpty())
        return cat;
    return m_gridSiteName + qFromUtf8(" · ") + cat;   /* 中点 U+00B7 */
}

void MainWindow::onVideoCategorySelected(const QString& key)
{
    if (!m_curProv || key == m_gridChip) return;
    m_gridChip = key;
    m_gridPage = 1;
    m_gridHasMore = true;
    m_gridIsSearch = false;
    ++m_gridGen;                       /* 作废旧分类在途响应 */
    m_curProv->cancel();
    sideNav->setCurrentVideoCategory(key);   /* 左栏高亮 */
    if (contentView) contentView->setVideoGridTitle(videoGridTitle());
    contentView->setVideoGridLoading(true);
    m_gridLoading = false;
    loadVideoPage(1);
}

/* 网络电视:按地区把内置频道装入网格。
 * id = "livetv:< stations()索引 >" 为旁路标记,playVideoAt 跳过 provider resolve。
 * 副标题(国家·分辨率·码流)拼进 VideoCard.author,封面进 VideoCard.cover,复用现有网格渲染。 */
void MainWindow::onNetTvRegion(const QString& region)
{
    m_netTvRegion = region;
    sideNav->setCurrentNetTvRegion(region);

    /* 只清动态分类与网格状态,保留站点下拉 */
    sideNav->clearVideoCategories();
    if (m_curProv) { m_curProv->cancel(); m_curProv = 0; }
    if (contentView) contentView->setPlayingVideoId(QString());
    m_videoTitles.clear();
    m_videoCards.clear();
    m_videoIds.clear();
    m_videoQueue.clear();
    m_videoIdx = -1;
    m_videoMode = false;
    m_gridChip = QString();
    m_chipList.clear();
    m_gridPage = 1;
    m_gridHasMore = false;
    m_gridLoading = false;
    m_gridIsSearch = false;
    m_gridSearchKw = QString();
    ++m_gridGen;                       /* 作废在途站点响应 */

    /* replaceVideoCards 不切 m_mode,必须先切到视频网格模式(同站点流程) */
    QString regionName = _(qToUtf8(tvlist::regionTitleKey(region)).data());
    if (regionName.isEmpty() || regionName == tvlist::regionTitleKey(region))
        regionName = region;
    m_gridSiteName = regionName;
    if (contentView) contentView->showVideoGrid(regionName);

    const std::vector<TvChannel>& all = tvlist::stations();
    for (size_t i = 0; i < all.size(); ++i) {
        const TvChannel& ch = all[i];
        if (!region.isEmpty() && qFromUtf8(ch.region) != region) continue;
        VideoCard c;
        c.id = QString::fromUtf8("livetv:%1").arg((int)i);
        c.title = qFromUtf8(ch.name);
        c.author = qFromUtf8(ch.country) + qFromUtf8(" · ")
                 + qFromUtf8(ch.resolution) + qFromUtf8(" · ")
                 + qFromUtf8(ch.bitrate);
        c.cover = qFromUtf8(ch.cover ? ch.cover : "");
        m_videoCards.push_back(c);
        m_videoIds.push_back(c.id);
        m_videoTitles[c.id] = c.title;
    }
    if (contentView) contentView->replaceVideoCards(m_videoCards, false);

    SharedStatusBar::instance()->showMessage(
        _("tv.loaded").arg(regionName).arg((int)m_videoCards.size()), 3000);
}

void MainWindow::onGridLoadMore()
{
    if (!m_curProv || m_gridLoading || !m_gridHasMore) return;
    loadVideoPage(m_gridPage);
}

void MainWindow::onVideoActivated(const QString& id)
{
    if (!m_curProv && m_netTvRegion.isEmpty()) return;   /* 网络电视为内置旁路,无需 provider */
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
        if (c.id.startsWith(qFromUtf8("livetv:"))) {
            /* 网络电视:直接带 m3u8 直链,playVideoAt 跳过 provider resolve */
            bool ok = false;
            int ci = c.id.mid(7).toInt(&ok);
            if (ok && ci >= 0 && ci < (int)tvlist::stations().size())
                t.url = qFromUtf8(tvlist::stations()[ci].url);
        } else {
            t.url = QString();          /* 待 resolve */
        }
        t.ncmId = 0;
        t.albumId = 0;
        t.playable = true;
        m_videoQueue.push_back(t);
        m_videoIds.push_back(c.id);
    }
    m_videoMode = true;
    m_videoIdx = index;
    saveQueueSnapshot();
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
    if (index < 0 || index >= (int)m_videoIds.size()) return;
    const QString id = m_videoIds[index];

    /* 网络电视旁路:跳过 provider resolve,直接把 m3u8 直链交播放器。
     * proxy 用 qxv::effectiveProxy(m_proxyCfg, mustProxy, "tvlist"),
     * mustProxy=false 恒直连(Status 回调跳过 env 兜底注入)。 */
    if (id.startsWith(qFromUtf8("livetv:"))) {
        m_videoIdx = index;
        saveQueueSnapshot();
        if (queuePanel) queuePanel->setCurrentTrack(m_videoQueue[index]);
        if (queuePanel) queuePanel->setCurrentIndex(index);
        if (playerBar) playerBar->setTrack(m_videoQueue[index]);
        if (contentView) contentView->setPlayingVideoId(id);
        if (videoPane) videoPane->setVideoTitle(m_videoQueue[index].title);
        SharedStatusBar::instance()->showMessage(_("content.resolving"), 3000);
        VideoUiEvent* ev = new VideoUiEvent(VideoUiEvent::Status);
        ev->gen = m_gridGen;             /* 与 customEvent 代际校验一致,否则被当陈旧丢弃 */
        ev->text = m_videoQueue[index].url;
        ev->chipKey = id;
        bool mustProxy = false;
        bool ok = false;
        int ci = id.mid(7).toInt(&ok);
        if (ok && ci >= 0 && ci < (int)tvlist::stations().size())
            mustProxy = tvlist::stations()[ci].mustProxy;
        ev->proxy = qxv::effectiveProxy(m_proxyCfg, mustProxy, "tvlist");
        QApplication::postEvent(this, ev);
        return;
    }

    if (!m_curProv) return;
    m_videoIdx = index;
    saveQueueSnapshot();
    if (queuePanel) queuePanel->setCurrentTrack(m_videoQueue[index]);
    if (queuePanel) queuePanel->setCurrentIndex(index);   /* 高亮/三角跟随当前行 */
    if (playerBar) playerBar->setTrack(m_videoQueue[index]);
    if (contentView) contentView->setPlayingVideoId(id);   /* 列表 ▶ 标识 */
    if (videoPane) videoPane->setVideoTitle(m_videoQueue[index].title);   /* 视频页顶栏标题 */
    /* 字幕:播放时自动拉(仅 YouTube;其它源空壳显式 Unsupported) */
    requestSubtitles(id);
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
            skipOnError(r.message);   /* autoskip:解析失败换下一项 */
        } else {
            m_errSkipRun = 0;         /* 成功即清零连续失败计数 */
            m_skipGuardId = QString();
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

    // 全局代理（墙外站点用；QXVOTUBE_PROXY 环境变量优先于此处）
    BoolConfigItem* proxyOnItem = new BoolConfigItem(
        "proxy/enabled", false, _("settings.proxy_label"), page);
    page->addLabeledControl(_("settings.proxy_label"), proxyOnItem->checkBox());
    StringConfigItem* proxyUrlItem = new StringConfigItem(
        "proxy/url", "", _("settings.proxyurl_label"), page);
    page->addLabeledControl(_("settings.proxyurl_label"), proxyUrlItem->lineEdit());

    /* 无视频播放模式(仅持久化,暂不接播放逻辑) */
    BoolConfigItem* noVideoItem = new BoolConfigItem(
        "player/novideo", Config::value("player.novideo") == qFromUtf8("1"),
        _("settings.novideo_label"), page);
    page->addLabeledControl(_("settings.novideo_label"), noVideoItem->checkBox());

    dlg->addCategory(_("settings.general_tab"), page);
    dlg->registerConfigItem(langItem);
    dlg->registerConfigItem(darkItem);
    dlg->registerConfigItem(proxyOnItem);
    dlg->registerConfigItem(proxyUrlItem);
    dlg->registerConfigItem(noVideoItem);

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

    /* 视频输出页播放测试:内嵌显示 + 两测试按钮,后端跟随本页 voutItem */
    TestPlaybackWidget* tpw = new TestPlaybackWidget(voutItem->comboBox(), vpage);
    vpage->addWidget(tpw, 1);

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

    /* —— 缓存页:图片上限生效;视频上限本次仅持久化(视频缓存后续接入) —— */
    CategoryPage* cpage = new CategoryPage(_("settings.cache_tab"), dlg);
    int imgMb = Config::value(qFromUtf8("cache.image_max_mb")).toInt();
    if (imgMb <= 0) imgMb = 500;
    IntConfigItem* imgItem = new IntConfigItem(
        "cache/image_max_mb", imgMb, _("settings.image_cache_label"),
        50, 5000, cpage);
    cpage->addLabeledControl(_("settings.image_cache_label"), imgItem->spinBox());
    int vidMb = Config::value(qFromUtf8("cache.video_max_mb")).toInt();
    if (vidMb <= 0) vidMb = 1000;
    IntConfigItem* vidItem = new IntConfigItem(
        "cache/video_max_mb", vidMb, _("settings.video_cache_label"),
        100, 20000, cpage);
    cpage->addLabeledControl(_("settings.video_cache_label"), vidItem->spinBox());
    QLabel* chint = new QLabel(_("settings.cache_hint"), cpage);
#if QT_VERSION >= 0x040000
    chint->setWordWrap(true);
#else
    chint->setAlignment(Qt::WordBreak | Qt::AlignLeft);
    chint->setMinimumHeight(44);
#endif
    cpage->addWidget(chint);
    dlg->addCategory(_("settings.cache_tab"), cpage);
    dlg->registerConfigItem(imgItem);
    dlg->registerConfigItem(vidItem);

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
    /* 迁移:旧版 IntConfigItem 未应用范围,500/1000 被 QSpinBox 夹成 99 并可能已落盘 */
    if (imgItem->spinBox()->value() == 99) imgItem->spinBox()->setValue(500);
    if (vidItem->spinBox()->value() == 99) vidItem->spinBox()->setValue(1000);
    dlg->exec();
    delete dlg;
}

void MainWindow::onSettingsSaved(const SettingsChangedMap& changed)
{
    if (changed.contains("cache/image_max_mb")) {
        int mb = changed["cache/image_max_mb"].toInt();
        Config::setValue("cache.image_max_mb", QString::number(mb));
        thumbcache::refreshImageLimit();
        filecache::requestTrim(filecache::coversDir(), thumbcache::imageLimitBytes());
    }
    if (changed.contains("cache/video_max_mb")) {
        /* 视频缓存后续接入,本次仅持久化 */
        Config::setValue("cache.video_max_mb",
                         QString::number(changed["cache/video_max_mb"].toInt()));
    }
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
    if (changed.contains("proxy/enabled")) {
        Config::setValue("proxy.enabled",
            changed["proxy/enabled"].toBool() ? qFromUtf8("1") : qFromUtf8("0"));
    }
    if (changed.contains("proxy/url")) {
        Config::setValue("proxy.url", changed["proxy/url"].toString());
    }
    if (changed.contains("player/novideo")) {
        Config::setValue("player.novideo",
            changed["player/novideo"].toBool() ? qFromUtf8("1") : qFromUtf8("0"));
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
    if (action == 2) {
        SharedStatusBar::instance()->showMessage(_("nav.local_video_coming"), 3000);
        return;
    }
    if (action == 3) {
        SharedStatusBar::instance()->showMessage(_("nav.site_live_coming"), 3000);
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
    else if (m_playFinished) {
        /* 自然播完停在末帧:从头重播当前项,不再是空按无反应 */
        m_playFinished = false;
        replayCurrent();
    }
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
    m_playFinished = false;   /* 新一段播放开始,清掉播完标记 */
    m_lastPosMs = -1;
    m_lastPosWallMs = wallMs();
    m_stallSec = -1;
    if (playerBar) playerBar->setPlaying(true);
    if (m_bufTimer != 0 && !((QTimer*)m_bufTimer)->isActive())
        ((QTimer*)m_bufTimer)->start(250);   // Qt3/Qt4 同签名
    switchToPlayerView(true);
    if (m_player != 0) m_player->setRotation(m_rotation);   /* 换视频沿用当前旋转 */
    if (m_pendingSeekMs >= 0) {
        const int target = m_pendingSeekMs;
        m_pendingSeekMs = -1;
        if (m_player != 0) {
            m_player->seek(target);   /* 重播 started 后跳到拖动位置 */
        }
    }
}

void MainWindow::onPlayerPaused()
{
    if (playerBar) playerBar->setPlaying(false);
}

void MainWindow::onPlayerStopped()
{
    m_stallSec = -1;
    m_lastPosMs = -1;
    if (playerBar) playerBar->setPlaying(false);
    if (m_bufTimer != 0) ((QTimer*)m_bufTimer)->stop();
    if (playerBar) playerBar->setBufferedMs(0);
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

/* 列表页/播放页判据(Qt3 QWidgetStack / Qt4 QStackedWidget) */
bool MainWindow::isPlayerViewActive() const
{
    /* 视频全屏时 videoPane 已脱离中栏,仍属播放页 */
    if (m_videoFullscreen)
        return true;
    if (centerStack == 0 || videoPane == 0)
        return false;
#ifdef QT3_BUILD
    return centerStack->visibleWidget() == (QWidget*)videoPane;
#else
    return centerStack->currentWidget() == (QWidget*)videoPane;
#endif
}

void MainWindow::onVideoBackRequested()
{
    switchToPlayerView(false);
}

void MainWindow::onVideoSurfaceRequested()
{
    switchToPlayerView(true);
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
    vi.dpr = cxDevicePixelRatio(videoPane->surface());
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

/* 播放器右键菜单:主项 + “旋转”子菜单。
 * 返回:0 视频详情,1 复制直链,2 浏览器打开,10..13 旋转子项,-1 取消 */
static int cxShowPlayerMenu(QWidget* parent, const QPoint& pos)
{
#ifdef QT3_BUILD
    QPopupMenu m(parent);
    m.insertItem(qFromUtf8("视频详情…"), 1);
    m.insertItem(qFromUtf8("复制直链地址(会过期)"), 2);
    m.insertItem(qFromUtf8("在浏览器打开"), 3);
    m.insertSeparator();
    QPopupMenu* sub = new QPopupMenu(&m);   /* 归 m 所有,exec 后随 m 析构 */
    sub->insertItem(qFromUtf8("顺时针旋转 90°"), 11);
    sub->insertItem(qFromUtf8("逆时针旋转 90°"), 12);
    sub->insertItem(qFromUtf8("旋转 180°"), 13);
    sub->insertItem(qFromUtf8("重置旋转"), 14);
    m.insertItem(qFromUtf8("旋转"), sub);
    const int id = m.exec(pos);             /* 子菜单项也返回其 id */
    if (id < 0) return -1;
    if (id >= 11) return 10 + (id - 11);    /* 10..13 */
    return id - 1;                          /* 0..2 */
#else
    QMenu m(parent);
    QAction* a0 = m.addAction(qFromUtf8("视频详情…"));
    QAction* a1 = m.addAction(qFromUtf8("复制直链地址(会过期)"));
    QAction* a2 = m.addAction(qFromUtf8("在浏览器打开"));
    m.addSeparator();
    QMenu* sub = m.addMenu(qFromUtf8("旋转"));
    QAction* r0 = sub->addAction(qFromUtf8("顺时针旋转 90°"));
    QAction* r1 = sub->addAction(qFromUtf8("逆时针旋转 90°"));
    QAction* r2 = sub->addAction(qFromUtf8("旋转 180°"));
    QAction* r3 = sub->addAction(qFromUtf8("重置旋转"));
    QAction* sel = m.exec(pos);
    if (sel == 0) return -1;                /* 取消 */
    if (sel == a0) return 0;
    if (sel == a1) return 1;
    if (sel == a2) return 2;
    if (sel == r0) return 10;
    if (sel == r1) return 11;
    if (sel == r2) return 12;
    if (sel == r3) return 13;
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
          << qFromUtf8("在浏览器打开")
          << qFromUtf8("查看评论…");
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
    } else if (sel == 4) {
        openComments(id);
    }
}

/* 打开评论窗口:目前 B站可拉,其余源显式提示不支持(不静默) */
void MainWindow::openComments(const QString& id) {
    if (m_curProv == 0) return;
    if (m_curProv->id() != qFromUtf8("bilibili")) {
        SharedStatusBar::instance()->showMessage(
            qFromUtf8("该源暂不支持评论拉取"), 3000);
        return;
    }
    if (m_commentDlg == 0) {
        m_commentDlg = new CommentDialog(this);
        QObject::connect(m_commentDlg, SIGNAL(loadMoreRequested()),
                         this, SLOT(onCommentLoadMore()));
    }
    m_commentProv = m_curProv;
    m_commentId = id;
    m_commentCursor = QString();   /* Qt3 QString 无 clear() */
    QString title;
    for (size_t i = 0; i < m_videoCards.size(); ++i)
        if (m_videoCards[i].id == id) { title = m_videoCards[i].title; break; }
    m_commentDlg->setTitle(title);
    m_commentDlg->clear();
    m_commentDlg->show();
    requestComments(QString());
}

void MainWindow::requestComments(const QString& cursor) {
    if (m_commentProv == 0) return;
    m_commentProv->comments(m_commentId, cursor,
        [this](const qxv::Result& r,
               const QList36<qxv::CommentItem>& items,
               const QString& next) {
            CommentUiEvent* ev = new CommentUiEvent;
            ev->result = r;
            ev->comments = items;
            ev->cursor = next;
            QApplication::postEvent(this, ev);
        });
}

void MainWindow::onCommentLoadMore() {
    if (m_commentCursor.isEmpty()) return;   // 无更多游标,不请求
    requestComments(m_commentCursor);
}

/* 字幕:播放时自动拉(仅 YouTube 经 yt-dlp;其它源空壳返回 Unsupported)。
 * 失败/无字幕静默:底部行清空,不弹错、不造假。 */
void MainWindow::requestSubtitles(const QString& id)
{
    if (m_curProv == 0) return;
    m_subtitleId = id;                 /* 代际:id 变则旧结果丢弃 */
    m_subtitleTracks.clear();
    m_subCueIdx = -1;
    if (playerBar) playerBar->setSubtitleLine(QString());
    qxv::VideoProvider* p = m_curProv;
    p->subtitles(id, [this, id](const qxv::Result& r,
                               const QList36<qxv::SubtitleTrack>& tracks) {
        SubtitleUiEvent* ev = new SubtitleUiEvent;
        ev->result = r;
        ev->tracks = tracks;
        ev->id = id;
        QApplication::postEvent(this, ev);
    });
}

/* 点击底部字幕行 → 打开字幕列表 */
void MainWindow::openSubtitleList()
{
    if (m_subtitleTracks.isEmpty()) {
        SharedStatusBar::instance()->showMessage(
            qFromUtf8("当前视频无字幕"), 2500);
        return;
    }
    if (m_subDlg == 0) m_subDlg = new SubtitleDialog(this);
    const qxv::SubtitleTrack& t = m_subtitleTracks[0];
    const QString name = t.name.isEmpty() ? t.lang : t.name;
    m_subDlg->setTitle(name.isEmpty() ? QString() : name);
    m_subDlg->setCues(t.cues, m_subCueIdx);
    m_subDlg->show();
    m_subDlg->raise();
}

/* 播放器右键:视频详情 / 复制直链 / 浏览器打开 */
void MainWindow::onPlayerContextMenu(const QPoint& globalPos)
{
    const int sel = cxShowPlayerMenu(this, globalPos);
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
    } else if (sel == 10) {
        applyRotation((m_rotation + 90) % 360);
    } else if (sel == 11) {
        applyRotation((m_rotation + 270) % 360);
    } else if (sel == 12) {
        applyRotation((m_rotation + 180) % 360);
    } else if (sel == 13) {
        applyRotation(0);
    }
}

void MainWindow::onRepeatClicked()
{
    m_repeatMode = (m_repeatMode + 1) % 3;
    if (playerBar) playerBar->setRepeatMode(m_repeatMode);
    Config::setValue("player.repeat", QString::number(m_repeatMode));
}

void MainWindow::onShuffleClicked()
{
    m_shuffle = !m_shuffle;
    if (playerBar) playerBar->setShuffle(m_shuffle);
    Config::setValue("player.shuffle", m_shuffle ? qFromUtf8("1")
                                                  : qFromUtf8("0"));
}

void MainWindow::onAutoSkipClicked()
{
    m_autoSkip = !m_autoSkip;
    if (playerBar) playerBar->setAutoSkip(m_autoSkip);
    Config::setValue("player.autoskip", m_autoSkip ? qFromUtf8("1")
                                                   : qFromUtf8("0"));
    SharedStatusBar::instance()->showMessage(
        m_autoSkip ? qFromUtf8("播放失败自动跳过:开")
                   : qFromUtf8("播放失败自动跳过:关"), 2500);
}

/* 失败自动跳过(qlmusic skipOnError):同曲只跳一次;连续失败到阈值停播 */
bool MainWindow::skipOnError(const QString& why)
{
    if (!m_autoSkip) return false;
    bool hasNext = false;
    QString gid;
    if (m_videoMode && !m_videoIds.empty()) {
        const int n = (int)m_videoIds.size();
        hasNext = (m_videoIdx + 1 < n) || m_repeatMode == 1
                  || (m_shuffle && n > 1);
        if (m_videoIdx >= 0 && m_videoIdx < (int)m_videoIds.size())
            gid = m_videoIds[m_videoIdx];
    } else {
        const Playlist* pl = findPlaylist(m_library, m_curPlaylistId);
        if (pl && !pl->tracks.empty()) {
            const int n = (int)pl->tracks.size();
            hasNext = (m_curTrackIndex + 1 < n) || m_repeatMode == 1
                      || (m_shuffle && n > 1);
            gid = qFromUtf8("lib:%1:%2").arg(m_curPlaylistId)
                      .arg(m_curTrackIndex);
        }
    }
    if (!hasNext) {
        if (playerBar) playerBar->setPlaying(false);
        SharedStatusBar::instance()->showMessageTyped(
            qFromUtf8("播放失败且无下一项: %1").arg(why), StatusError, 5000);
        return false;
    }
    if (!gid.isEmpty() && gid == m_skipGuardId) return true;  /* 同曲双通道只跳一次 */
    m_skipGuardId = gid;
    if (++m_errSkipRun >= kErrSkipStopAt) {
        m_errSkipRun = 0;
        if (playerBar) playerBar->setPlaying(false);
        SharedStatusBar::instance()->showMessageTyped(
            qFromUtf8("连续 %1 次失败,已停止自动跳过").arg(kErrSkipStopAt),
            StatusError, 5000);
        return false;
    }
    SharedStatusBar::instance()->showMessage(
        qFromUtf8("播放失败,自动跳过: %1").arg(why), 4000);
    advanceNext(true);
    return true;
}

/* 统一推进(qlmusic 语义):fromAuto=false 手动 next 恒环绕;
 * fromAuto=true 自然播完仅 repeat=all 环绕;shuffle 随机≠当前。 */
void MainWindow::advanceNext(bool fromAuto)
{
    if (m_videoMode && !m_videoIds.empty()) {
        const int n = (int)m_videoIds.size();
        if (m_shuffle && n > 1) {
            int nx = m_videoIdx;
            while (nx == m_videoIdx) nx = std::rand() % n;
            playVideoAt(nx);
            return;
        }
        if (m_repeatMode == 2 && m_videoIdx >= 0) {
            playVideoAt(m_videoIdx);     /* 单曲重播 */
            return;
        }
        int nx = m_videoIdx + 1;
        if (nx >= n) {
            if (!fromAuto || m_repeatMode == 1) nx = 0;
            else {
                if (playerBar) playerBar->setPlaying(false);
                return;
            }
        }
        playVideoAt(nx);
        return;
    }
    if (!m_argvUrls.isEmpty() && m_curPlaylistId < 0) {
        const int n = (int)m_argvUrls.size();
        if (m_repeatMode == 2 && m_argvIdx >= 0) { playArgvAt(m_argvIdx); return; }
        int nx = m_argvIdx + 1;
        if (nx >= n) {
            if (!fromAuto || m_repeatMode == 1) nx = 0;
            else return;                 /* 到尾停 */
        }
        playArgvAt(nx);
        return;
    }
    const Playlist* pl = findPlaylist(m_library, m_curPlaylistId);
    if (!pl || pl->tracks.empty()) return;
    const int n = (int)pl->tracks.size();
    if (m_shuffle && n > 1) {
        int nx = m_curTrackIndex;
        while (nx == m_curTrackIndex) nx = std::rand() % n;
        showTrackStub(m_curPlaylistId, nx);
        return;
    }
    if (m_repeatMode == 2 && m_curTrackIndex >= 0) {
        showTrackStub(m_curPlaylistId, m_curTrackIndex);
        return;
    }
    int nx = m_curTrackIndex + 1;
    if (nx >= n) {
        if (!fromAuto || m_repeatMode == 1) nx = 0;
        else return;
    }
    showTrackStub(m_curPlaylistId, nx);
}

void MainWindow::onDownloadCurrent()
{
    if (!m_dl || m_dl->busy()) {
        SharedStatusBar::instance()->showMessage(
            qFromUtf8("已有下载任务在进行"), 3000);
        return;
    }
    if (!m_videoMode || m_videoIdx < 0
        || m_videoIdx >= (int)m_videoIds.size() || !m_curProv) {
        SharedStatusBar::instance()->showMessage(
            qFromUtf8("当前没有可下载的视频"), 3000);
        return;
    }
    const QString id = m_videoIds[m_videoIdx];
    QString title = m_videoQueue[m_videoIdx].title;
    if (title.isEmpty()) title = id;
    const long dur = (long)m_videoQueue[m_videoIdx].durationMs;
    SharedStatusBar::instance()->showMessage(qFromUtf8("解析下载地址…"), 4000);
    qxv::VideoProvider* p = m_curProv;
    p->resolve(id, [this, id, title, dur, p](const qxv::Result& r,
                                             const qxv::PlayResult& pr) {
        if (r.code != qxv::Err::Ok || pr.streams.isEmpty()) {
            SharedStatusBar::instance()->showMessageTyped(
                r.message.isEmpty() ? qFromUtf8("下载失败:无法解析地址")
                                    : r.message, StatusError, 5000);
            return;
        }
        QString url = pr.streams.first().url;
        const QString pid = p->id();
        QString referer = qFromUtf8("https://www.douyin.com/");
        if (pid == qFromUtf8("bilibili"))
            referer = qFromUtf8("https://www.bilibili.com/");
        else if (pid == qFromUtf8("youtube"))
            referer = qFromUtf8("https://www.youtube.com/");
        else if (pid == qFromUtf8("xiaohongshu"))
            referer = qFromUtf8("https://www.xiaohongshu.com/");
        const QString ua =
            qxv::Fingerprint::chromeWeb(qFromUtf8("zh-CN,zh;q=0.9")).userAgent;
        const char* px = getenv("QXVOTUBE_PROXY");
        const QString proxy = px != 0 ? qFromUtf8(px) : QString();
        if (playerBar) playerBar->setDownloadState(0);
        const bool ok = m_dl->start(url, title, dur,
            [this](int pct) {
                if (playerBar) playerBar->setDownloadState(pct);
                if (pct >= 0) {
                    SharedStatusBar::instance()->showMessage(
                        qFromUtf8("下载中 %1%").arg(pct), 1200);
                } else if (pct == -1) {
                    SharedStatusBar::instance()->showMessage(
                        qFromUtf8("下载中…"), 1200);
                }
            },
            [this](bool ok2, const QString& path, const QString& err) {
                if (playerBar) playerBar->setDownloadState(ok2 ? 100 : -2);
                if (ok2)
                    SharedStatusBar::instance()->showMessage(
                        qFromUtf8("下载完成: %1").arg(path), 5000);
                else
                    SharedStatusBar::instance()->showMessageTyped(
                        qFromUtf8("下载失败: %1").arg(err), StatusError, 6000);
            }, QString(), referer, ua, proxy);
        if (!ok)
            SharedStatusBar::instance()->showMessageTyped(
                qFromUtf8("下载启动失败(ffmpeg 不可用?)"), StatusError, 5000);
    });
}

void MainWindow::onPlayerFinished()
{
    m_playFinished = true;    /* 记录自然播完,供 onPlayPause 从头重播 */
    m_stallSec = -1;
    m_lastPosMs = -1;
    if (playerBar) playerBar->setPlaying(false);
    if (m_bufTimer != 0) ((QTimer*)m_bufTimer)->stop();
    if (playerBar) playerBar->setBufferedMs(0);
    advanceNext(true);   /* 自然播完:仅 repeat=all 环绕(qlmusic 语义) */
}

void MainWindow::onRefreshBuffered()
{
    /* 播放停滞看门狗:仍在播放但位置长时间不动 → 状态栏显示已停滞秒数 */
    if (m_player != 0 && m_player->state() == CxPlaying && !m_playFinished
        && m_lastPosWallMs > 0) {
        const long long idle = wallMs() - m_lastPosWallMs;
        if (idle >= 1500) {                     /* 1.5s 起提示,避免误报 */
            const int sec = (int)(idle / 1000);
            if (sec != m_stallSec) {
                m_stallSec = sec;
                SharedStatusBar::instance()->showMessage(
                    _("player.stalled_secs").arg(sec), 1200);
            }
        }
    }
    if (playerBar == 0)
        return;
    CxBufferInfo info;
    if (m_player != 0)
        info = m_player->bufferInfo();
    playerBar->setBufferInfo(info.bufferedMs, info.bufferedBytes,
                             info.maxBytes, info.bytesPerSec);
}

void MainWindow::onListRefresh()
{
    if (contentView == 0)
        return;
    if (contentView->isVideoGridMode()) {
        if (m_curProv == 0) return;
        m_curProv->cancel();
        m_gridPage = 1;
        m_gridLoading = false;
        ++m_gridGen;                      /* 作废旧在途响应 */
        contentView->setVideoGridLoading(true);
        loadVideoPage(1);                 /* 保留当前分类/搜索词,重拉首页 */
    } else if (m_curProv != 0) {
        onSiteChanged(m_curProv->id());   /* 歌单/分类宫格:重载当前站点 */
    } else {
        contentView->setLibrary(m_library); /* 本地库:重读 */
    }
}

void MainWindow::replayCurrent()
{
    if (m_videoMode && !m_videoIds.empty() && m_videoIdx >= 0
        && m_videoIdx < (int)m_videoIds.size()) {
        playVideoAt(m_videoIdx);
    } else if (!m_argvUrls.isEmpty() && m_curPlaylistId < 0
               && m_argvIdx >= 0 && m_argvIdx < (int)m_argvUrls.size()) {
        playArgvAt(m_argvIdx);
    } else if (m_curPlaylistId >= 0 && m_curTrackIndex >= 0) {
        showTrackStub(m_curPlaylistId, m_curTrackIndex);
    }
}

void MainWindow::onPlayerPosition(long ms)
{
    if (ms != m_lastPosMs) {       /* 有推进:刷新时间戳并清停滞态 */
        m_lastPosMs = ms;
        m_lastPosWallMs = wallMs();
        m_stallSec = -1;
    }
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
    /* 底部单行字幕:按播放进度找当前 cue;无则在 cue 间留白 */
    if (!m_subtitleTracks.isEmpty()) {
        const QList36<qxv::SubtitleCue>& cues = m_subtitleTracks[0].cues;
        int idx = m_subCueIdx;
        if (idx < 0 || idx >= cues.size()
            || ms < cues[idx].startMs || ms >= cues[idx].endMs) {
            idx = -1;
            for (int i = 0; i < cues.size(); ++i) {
                if (ms >= cues[i].startMs && ms < cues[i].endMs) { idx = i; break; }
                if (cues[i].startMs > ms) break;   /* 有序,提前退 */
            }
        }
        if (idx != m_subCueIdx) {
            m_subCueIdx = idx;
            if (playerBar)
                playerBar->setSubtitleLine(idx >= 0 ? cues[idx].text : QString());
            if (m_subDlg != 0 && m_subDlg->isVisible())
                m_subDlg->setCues(cues, idx);
        }
    }
}

void MainWindow::onPlayerDuration(long ms)
{
    m_durMs = ms;
    if (playerBar) playerBar->setDuration((int)ms);
}

void MainWindow::onVolumeChanged(int pct)
{
    if (m_player != 0) m_player->setVolume(pct);
    Config::setValue("player.volume", QString::number(pct));
}

void MainWindow::onRateChanged(double r)
{
    if (m_player != 0) m_player->setRate(r);
}

void MainWindow::onRateMenuRequested(const QPoint& globalPos)
{
    static const double kRates[] = { 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0 };
    const int n = (int)(sizeof(kRates) / sizeof(kRates[0]));
    const double cur = (playerBar != 0) ? playerBar->rate()
                                        : (m_player ? m_player->rate() : 1.0);
    QStringList items;
    for (int i = 0; i < n; ++i) {
        QString label = QString::number(kRates[i], 'g', 3) + "x";
        const double d = (kRates[i] > cur) ? (kRates[i] - cur) : (cur - kRates[i]);
        if (d < 0.001) label = qFromUtf8("● ") + label;   /* 标记当前倍速 */
        items << label;
    }
    const int sel = cxShowMenu(playerBar, globalPos, items);
    if (sel < 0) return;                                  /* 取消 */
    const double r = kRates[sel];
    if (r == cur) return;                                 /* 未变化,不提示 */
    if (playerBar) playerBar->setRate(r);
    onRateChanged(r);                                     /* 应用到引擎 */
    SharedStatusBar::instance()->showMessage(
        qFromUtf8("倍速 %1x → %2x")
            .arg(cur, 0, 'f', 2).arg(r, 0, 'f', 2), 1500);
}

void MainWindow::onRotateMenuRequested(const QPoint& globalPos)
{
    QStringList items;
    items << qFromUtf8("顺时针旋转 90°")
          << qFromUtf8("逆时针旋转 90°")
          << qFromUtf8("旋转 180°")
          << qFromUtf8("重置旋转");
    const int sel = cxShowMenu(playerBar, globalPos, items);
    if (sel == 0) applyRotation((m_rotation + 90) % 360);
    else if (sel == 1) applyRotation((m_rotation + 270) % 360);
    else if (sel == 2) applyRotation((m_rotation + 180) % 360);
    else if (sel == 3) applyRotation(0);
}

void MainWindow::applyRotation(int deg)
{
    deg = ((deg % 360) + 360) % 360;
    m_rotation = deg;
    if (m_player != 0) m_player->setRotation(deg);
    SharedStatusBar::instance()->showMessage(
        deg == 0 ? qFromUtf8("旋转已重置")
                 : qFromUtf8("旋转 %1°（顺时针）").arg(deg), 1500);
}

void MainWindow::onMuteChanged(bool on)
{
    if (m_player != 0) m_player->setMute(on);
    Config::setValue("player.mute", on ? qFromUtf8("1") : qFromUtf8("0"));
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
    /* 全屏时 videoPane 是顶层窗口,toast 挂它上面才可见 */
    QWidget* toastParent = (m_videoFullscreen && videoPane != 0)
                               ? (QWidget*)videoPane : (QWidget*)this;
    if (ok) {
        SharedStatusBar::instance()->showMessage(qFromUtf8("截图: ") + path, 4000);
        ToastWidget::showCenter(
            toastParent,
            qFromUtf8("截图已保存：%1").arg(QFileInfo(path).fileName()),
            ToastWidget::Info, 2000);
    } else {
        SharedStatusBar::instance()->showMessage(qFromUtf8("截图失败(内核不支持)"), 3000);
        ToastWidget::showCenter(toastParent, qFromUtf8("截图失败：当前输出后端不支持"),
                                ToastWidget::Warning, 2500);
    }
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
    if (playerBar) playerBar->setPlaying(false);
    SharedStatusBar::instance()->showMessageTyped(msg, StatusError, 4000);
    if (code == (int)CxErrAudio || code == (int)CxErrSeek)
        return;   /* 非致命:音频回退 silent / seek 超界,不自动跳集 */
    skipOnError(msg);   /* autoskip 开:换下一项;关闭则停在错误上 */
}

void MainWindow::onSeekRequested(int ms)
{
    if (m_playFinished) {
        /* 列表页拖动:仅忽略,不重播跳页 */
        if (!isPlayerViewActive())
            return;
        /* 播完态拖进度:先从头重播当前项,started 后跳到目标位置并继续播放 */
        m_pendingSeekMs = ms;
        m_playFinished = false;
        replayCurrent();
        return;
    }
    /* cmdmpv 进程型 M1 暂不支持外控(M2 接 IPC);libmpv 内核可精细 seek */
    if (m_player != 0 && m_player->engine() != 0) {
        m_player->seek(ms);
    }
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
    advanceNext(false);   /* 手动 next:尾部恒环绕 */
}

void MainWindow::onVideoSurfaceToggleFullscreen()
{
    toggleVideoFullscreen();   /* 与 F 键同一实现 */
}

void MainWindow::onVideoSurfaceHint(int hint)
{
    const char* key = "player.mid_click_ni";
    if (hint == VideoPane::HintWheelUp)        key = "player.wheel_up_ni";
    else if (hint == VideoPane::HintWheelDown) key = "player.wheel_down_ni";
    SharedStatusBar::instance()->showMessage(_(key), 2000);
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
    if (event->type() == SubtitleUiEventType) {
        SubtitleUiEvent* e = static_cast<SubtitleUiEvent*>(event);
        if (e->id != m_subtitleId) return;   /* 切视频后旧回调,丢弃 */
        if (e->result.code == qxv::Err::Ok && !e->tracks.isEmpty()) {
            m_subtitleTracks = e->tracks;
            m_subCueIdx = -1;
        } else {
            /* 无字幕/失败:静默清空,不显示假数据 */
            m_subtitleTracks.clear();
            m_subCueIdx = -1;
            if (playerBar) playerBar->setSubtitleLine(QString());
        }
        return;
    }
    if (event->type() == CommentUiEventType) {
        CommentUiEvent* e = static_cast<CommentUiEvent*>(event);
        if (m_commentDlg != 0) {
            if (e->result.code != qxv::Err::Ok) {
                const QString head =
                    _(qToUtf8(qxv::i18nKeyFor(e->result.code)).data());
                QString text = head;
                if (!e->result.message.isEmpty())
                    text += qFromUtf8(" · ") + e->result.message;
                m_commentDlg->setError(text);
            } else {
                m_commentCursor = e->cursor;      /* 回填真实游标供翻页 */
                m_commentDlg->appendComments(e->comments, !e->cursor.isEmpty());
            }
        }
        return;
    }
    if (event->type() == VideoUiEventType) {
        VideoUiEvent* e = static_cast<VideoUiEvent*>(event);
        // CategoriesRefresh 只就地刷新分类显示、不携带分页数据:首次 Categories
        // 分支里的 loadVideoPage 会递增 m_gridGen,若用同一 gen 校验会被误丢。
        // 故跳过 gen 校验,分支内改用站点 id 校验(切源后丢弃旧事件)。
        if (e->kind != VideoUiEvent::CategoriesRefresh && e->gen != m_gridGen) {
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
        if (e->kind == VideoUiEvent::CategoriesRefresh) {
            if (!m_curProv || e->text != m_catLoadSite) return;
            if (--m_catLoadPending <= 0)
                sideNav->setVideoCategoriesLoading(false);
            if (e->catKeys.isEmpty()) return;  /* 失败/无能力:仅结束 loading,不动 chips */
            std::vector<CategoryChip> chips;
            for (int i = 0; i < e->catKeys.size(); ++i)
                chips.push_back(CategoryChip(e->catKeys[i], e->catNames[i]));
            // 按 key 保留当前选中:m_gridChip 不变,sideNav 依 key 重定位高亮;
            // 不重置分页、不调 loadVideoPage,用户当前浏览内容不打断。
            m_chipList = chips;
            QStringList names, keys;
            for (int i = 0; i < (int)chips.size(); ++i) {
                names.append(chips[i].name.isEmpty()
                                 ? _("content.cat_recommend")
                                 : chips[i].name);
                keys.append(chips[i].key);
            }
            sideNav->setVideoCategories(names, keys, m_gridChip);
            if (contentView) contentView->setVideoGridTitle(videoGridTitle());
            if (getenv("QLVOTUBE_DEBUG") != 0)
                qDebug("categories refreshed in place: %d chips, active '%s'",
                       int(chips.size()), qToUtf8(m_gridChip).data());
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
            if (site == m_catLoadSite) {
                if (--m_catLoadPending <= 0)
                    sideNav->setVideoCategoriesLoading(false);
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
            /* 动态分类 → 左侧栏(「推荐」+各分类),高亮首个 */
            QStringList names, keys;
            for (int i = 0; i < (int)chips.size(); ++i) {
                names.append(chips[i].name.isEmpty()
                                 ? _("content.cat_recommend")
                                 : chips[i].name);
                keys.append(chips[i].key);
            }
            sideNav->setVideoCategories(names, keys, m_gridChip);
            if (contentView) contentView->setVideoGridTitle(videoGridTitle());
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
                c.replyCount = e->cards[i].replyCount;
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
            if (contentView && e->page > 0) contentView->setVideoGridPage(e->page);
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
                /* 网络电视频道:proxy 已由 effectiveProxy 决定(mustProxy=false 恒直连),
                 * 不再用 env 兜底,否则国内频道会被误注入代理 IP。 */
                if (no.proxy.isEmpty() && !e->chipKey.startsWith(qFromUtf8("livetv:")))
                    no.proxy = qenv("QXVOTUBE_PROXY");
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


// ── 播放器快捷键(2026-11-18) ────────────────────────────────────────
// 惯例参考:YouTube 官方(空格/K 播放暂停、←→ ±5s、J/L ±10s、↑↓ 音量、
// M 静音、F 全屏、0-9 百分比、Shift+,/. 倍速、C 字幕、? 帮助)。
// 统一装在 qApp 事件过滤层:焦点在文本输入时让行(搜索框可正常输入空格)。
namespace {

bool isTextInputWidget(QWidget* w)
{
    if (w == 0) return false;
    if (w->inherits("QLineEdit") || w->inherits("QTextEdit")) return true;
    /* 只有可编辑下拉框才算文本输入(站点下拉获焦时快捷键仍生效) */
    if (w->inherits("QComboBox")) {
#if QT_VERSION >= 0x040000
        return ((QComboBox*)w)->isEditable();
#else
        return ((QComboBox*)w)->editable();   /* Qt3 命名 */
#endif
    }
    return false;
}

} // namespace

bool MainWindow::shortcutsDisabledByFocus() const
{
#if QT_VERSION >= 0x040000
    QWidget* w = QApplication::focusWidget();
#else
    QWidget* w = qApp != 0 ? qApp->focusWidget() : 0;   /* Qt3 为实例方法 */
#endif
    return isTextInputWidget(w);
}

void MainWindow::seekBy(long deltaMs)
{
    if (m_player == 0 || m_player->engine() == 0) return;
    const long dur = m_durMs > 0 ? m_durMs : m_player->duration();
    long pos = m_player->position() + deltaMs;
    if (pos < 0) pos = 0;
    if (dur > 1000 && pos > dur - 1000) pos = dur - 1000;
    m_player->seek(pos);
    SharedStatusBar::instance()->showMessage(
        qFromUtf8("%1s / %2s").arg(pos / 1000).arg(dur / 1000), 1200);
}

void MainWindow::adjustVolume(int deltaPct)
{
    if (m_player == 0) return;
    int v = m_player->volume() + deltaPct;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    m_player->setVolume(v);
    if (playerBar) playerBar->setVolume(v);   /* 静默更新,无信号回环 */
    SharedStatusBar::instance()->showMessage(
        qFromUtf8("音量 %1%").arg(v), 1200);
}

void MainWindow::toggleMuteShortcut()
{
    if (m_player == 0) return;
    const bool on = !m_player->isMuted();
    m_player->setMute(on);
    if (playerBar) playerBar->setMuted(on);
    SharedStatusBar::instance()->showMessage(
        on ? qFromUtf8("静音") : qFromUtf8("取消静音"), 1200);
}

void MainWindow::adjustRate(double delta)
{
    if (m_player == 0) return;
    const double old = m_player->rate();
    double r = old + delta;
    if (r < 0.25) r = 0.25;
    if (r > 2.0) r = 2.0;
    if (r == old) return;
    m_player->setRate(r);
    if (playerBar) playerBar->setRate(r);
    SharedStatusBar::instance()->showMessage(
        qFromUtf8("倍速 %1x → %2x")
            .arg(old, 0, 'f', 2).arg(r, 0, 'f', 2), 1500);
}

void MainWindow::toggleFullscreen()
{
    m_fullscreen = !m_fullscreen;
    if (m_fullscreen) {
        m_normGeom = geometry();
        showFullScreen();
    } else {
        showNormal();
        if (!m_normGeom.isEmpty()) setGeometry(m_normGeom);
    }
}

void MainWindow::toggleVideoFullscreen()
{
    if (videoPane == 0 || centerStack == 0) {
        return;
    }
    /* 列表页按 F:不进入视频全屏(否则会随退出全屏切到播放页) */
    if (!m_videoFullscreen && !isPlayerViewActive())
        return;
    if (!m_videoFullscreen) {
        m_videoFullscreen = true;
#ifdef QT3_BUILD
        videoPane->reparent(0, QPoint(0, 0), false);
#else
        videoPane->setParent(0, Qt::Window);
#endif
        videoPane->showFullScreen();
    } else {
        m_videoFullscreen = false;
#ifdef QT3_BUILD
        videoPane->reparent(centerStack, QPoint(0, 0), false);
        centerStack->addWidget(videoPane, 1);   /* 原 index:contentView=0,videoPane=1 */
#else
        videoPane->setParent(centerStack);
        centerStack->insertWidget(1, videoPane);
#endif
        qStackSetCurrent(centerStack, videoPane);
        videoPane->showNormal();
    }
}

void MainWindow::showShortcutHelp()
{
    QMessageBox::information(this, _("shortcut.title"), _("shortcut.help"));
}

bool MainWindow::eventFilter(QObject* o, QEvent* e)
{
    if (e->type() != QEvent::KeyPress)
        return QMainWindow::eventFilter(o, e);
    /* 视频全屏时 videoPane 提升为独立顶层窗,MainWindow 非 active;
     * 此时仍要收 F/ESC 才能退出全屏、恢复原布局 */
    if (!isActiveWindow() && !m_videoFullscreen)
        return QMainWindow::eventFilter(o, e);
    if (shortcutsDisabledByFocus())
        return QMainWindow::eventFilter(o, e);
    QKeyEvent* ke = (QKeyEvent*)e;
    const int k = ke->key();
#if QT_VERSION >= 0x040000
    const bool shift = (ke->modifiers() & Qt::ShiftModifier) != 0;
    const bool ctrl  = (ke->modifiers() & Qt::ControlModifier) != 0;
    const bool alt   = (ke->modifiers() & Qt::AltModifier) != 0;
#else
    const bool shift = (ke->state() & Qt::ShiftButton) != 0;
    const bool ctrl  = (ke->state() & Qt::ControlButton) != 0;
    const bool alt   = (ke->state() & Qt::AltButton) != 0;
#endif
    /* Ctrl/Alt 组合让给系统/浏览器(尤其 Ctrl+F),不吞掉 */
    if (ctrl || alt) {
        return QMainWindow::eventFilter(o, e);
    }
    switch (k) {
    case Qt::Key_Space:
    case Qt::Key_K:
        /* 列表页播完态:不重播跳页(后台播放中的暂停/继续仍生效) */
        if (!isPlayerViewActive() && m_playFinished)
            return true;
        onPlayPause();
        return true;
    case Qt::Key_Left:
        seekBy(shift ? -1000 : -5000);
        return true;
    case Qt::Key_Right:
        seekBy(shift ? 1000 : 5000);
        return true;
    case Qt::Key_J:
        seekBy(-10000);
        return true;
    case Qt::Key_L:
        seekBy(10000);
        return true;
    case Qt::Key_Up:
        adjustVolume(5);
        return true;
    case Qt::Key_Down:
        adjustVolume(-5);
        return true;
    case Qt::Key_M:
        toggleMuteShortcut();
        return true;
    case Qt::Key_F:
        if (!isPlayerViewActive())
            return true;   /* 列表页:F 不响应,已消费 */
        if (m_fullscreen) { toggleFullscreen(); }   /* 应用全屏 → 视频全屏,状态互斥 */
        toggleVideoFullscreen();
        return true;
    case Qt::Key_F11:
        if (m_videoFullscreen) { toggleVideoFullscreen(); }
        toggleFullscreen();
        return true;
    case Qt::Key_F12:
        SharedStatusBar::instance()->showMessage(
            _("shortcut.log_todo"), 2000);
        return true;
    case Qt::Key_Escape:
        if (m_videoFullscreen) { toggleVideoFullscreen(); return true; }
        if (m_fullscreen) { toggleFullscreen(); return true; }
        break;
    case Qt::Key_H:
    case Qt::Key_F1:
        showShortcutHelp();
        return true;
    case Qt::Key_Home:
        if (m_player && m_player->engine()) { m_player->seek(0); return true; }
        break;
    case Qt::Key_End:
        if (m_player && m_player->engine() && m_durMs > 1000) {
            m_player->seek(m_durMs - 1000);
            return true;
        }
        break;
    case Qt::Key_C:
        onCycleSubtitle();
        return true;
    case Qt::Key_R:
        if (!isPlayerViewActive())
            return true;   /* 列表页无画面:不旋转、不提示,也不影响下一部 */
        if (shift) applyRotation(0);                       /* Shift+R 重置 */
        else       applyRotation((m_rotation + 90) % 360); /* R 顺时针 90 */
        return true;
    default:
        break;
    }
    if (k >= Qt::Key_0 && k <= Qt::Key_9 && m_player != 0
        && m_player->engine() != 0 && m_durMs > 0) {
        const int d = k - Qt::Key_0;
        m_player->seek(m_durMs * d / 10);
        return true;
    }
    /* Qt3 会把 Shift+,/. 直接报成 '<'/'>'(Key_Less/Key_Greater) */
    if (k == Qt::Key_Less || k == '<')
        { adjustRate(-0.25); return true; }
    if (k == Qt::Key_Greater || k == '>')
        { adjustRate(0.25); return true; }
    if (k == Qt::Key_Question) { showShortcutHelp(); return true; }
    return QMainWindow::eventFilter(o, e);
}


// ── 播放队列快照(qlmusic queue.json 模型;仅视频队列) ─────────────
void MainWindow::saveQueueSnapshot()
{
    if (!m_videoMode || m_videoIds.empty()) return;
    QString provider = m_curProv ? m_curProv->id() : QString();
    std::vector<VideoCard> cards;
    /* 网格卡片与播放队列仅在"刚由网格进入播放"时同序;恢复后的队列
     * 与网格可能不一致 → 用 id 全量校验,不一致走队列兜底。 */
    bool aligned = m_videoCards.size() == m_videoIds.size();
    for (size_t i = 0; aligned && i < m_videoIds.size(); ++i)
        if (m_videoCards[i].id != m_videoIds[i]) aligned = false;
    if (aligned) {
        for (size_t i = 0; i < m_videoCards.size(); ++i)
            cards.push_back(m_videoCards[i]);
    } else {
        cards.clear();
        for (int i = 0; i < (int)m_videoQueue.size(); ++i) {
            VideoCard c;
            c.id = m_videoIds[i];
            c.title = m_videoQueue[i].title;
            c.author = m_videoQueue[i].artist;
            c.cover = m_videoQueue[i].coverUrl;
            c.durationMs = m_videoQueue[i].durationMs;
            cards.push_back(c);
        }
    }
    queue_store::save(provider, m_gridChip, m_videoIdx, cards);
}

void MainWindow::restoreQueueSnapshot()
{
    QString provider, chip;
    int current = -1;
    std::vector<VideoCard> cards;
    if (!queue_store::load(&provider, &chip, &current, &cards)) return;
    if (!m_curProv || m_curProv->id() != provider) return;  /* 不匹配丢弃 */
    if (cards.empty()) return;

    m_videoQueue.clear();
    m_videoIds.clear();
    for (size_t i = 0; i < cards.size(); ++i) {
        Track t;
        t.id = (int)i;
        t.title = cards[i].title;
        t.artist = cards[i].author;
        t.coverUrl = cards[i].cover;
        t.durationMs = (int)cards[i].durationMs;
        t.playable = true;
        m_videoQueue.push_back(t);
        m_videoIds.push_back(cards[i].id);
    }
    m_videoMode = true;
    m_gridChip = chip;
    if (current < 0 || current >= (int)m_videoQueue.size()) current = 0;
    m_videoIdx = current;
    if (queuePanel) {
        queuePanel->setQueue(m_videoQueue, m_videoIdx);
        queuePanel->setCurrentTrack(m_videoQueue[m_videoIdx]);
    }
    if (playerBar) {
        playerBar->setTrack(m_videoQueue[m_videoIdx]);
        playerBar->setPlaying(false);      /* 摆好但不播(qlmusic 一致) */
    }
    m_curWebUrl = cards[current].webUrl;
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    /* 首次显示:按屏幕可用区夹取(mac 上布局最小宽可能把窗口顶到数屏宽) */
    static bool clamped = false;
    if (clamped) return;
    clamped = true;
    const QRect avail = qAvailableGeometryFor(this);
    int w = avail.width() * 9 / 10;
    int h = avail.height() * 9 / 10;
    if (w > 980) w = 980;
    if (h > 640) h = 640;
    resize(w, h);
    move(avail.x() + (avail.width() - w) / 2,
         avail.y() + (avail.height() - h) / 2);
    if (avail.width() < minimumSizeHint().width()) {
        qWarning("[win] avail=%dx%d winMin=%d side=%d content=%d queue=%d "
                 "player=%d", avail.width(), avail.height(),
                 minimumSizeHint().width(),
                 sideNav ? sideNav->minimumSizeHint().width() : -1,
                 contentView ? contentView->minimumSizeHint().width() : -1,
                 queuePanel ? queuePanel->minimumSizeHint().width() : -1,
                 playerBar ? playerBar->minimumSizeHint().width() : -1);
    }
}
