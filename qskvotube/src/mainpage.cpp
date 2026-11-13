#include "mainpage.h"

#include <QGuiApplication>
#include <QTimer>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QUrl>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QRunnable>
#include <QPointer>

#include <QskWindow.h>
#include <QskTextLabel.h>
#include <QskLinearBox.h>
#include <QskPushButton.h>
#include <QskSlider.h>
#include <QskMenu.h>
#include <QskLabelData.h>
#include <QskPopup.h>
#include <QskSkinManager.h>
#include <QskSkin.h>
#include <QskFontRole.h>
#include <private/qquicktaphandler_p.h>

#include "menuoverlay.h"
#include "pagemanager.h"

#include "cxvideoplayer.h"
#include "cxengine.h"
#include "eng_mpv.h"
#include "renderer_quick.h"
#include "renderer_qmpv.h"
#include "cxmsutil.h"
#include "uistyle.h"
#include "androidutils.h"
#include "useravatar.h"
#include "translator.h"

// 词表取值便捷封装(qlcomp Translator)
static QString trKey(const char* key)
{
    return Translator::instance().t(QString::fromLatin1(key));
}

// CXVideoEq 工厂(有自定义 ctor 非聚合,不能花括号构造)
static CXVideoEq cxvEq(int b, int c, int s, int h){
    CXVideoEq eq;
    eq.brightness = b;
    eq.contrast = c;
    eq.saturation = s;
    eq.hue = h;
    return eq;
}

// 拖拽导入钩子:QQuickWindow 无 drag 虚函数/QML DropArea 需私有头——
// 改用 window 对象的 eventFilter 截获 DragEnter/Drop。
class CxDropHook : public QObject {
public:
    explicit CxDropHook(QObject* parent = nullptr) : QObject(parent) {}
    QStringList* pl = nullptr;                 /* 播放列表(尾部追加) */
    std::function<void(int)> openPl;           /* 起播指定项 */
    bool eventFilter(QObject* obj, QEvent* ev) override {
        switch (ev->type()) {
        case QEvent::DragEnter:
            ((QDragEnterEvent*)ev)->acceptProposedAction();
            return true;
        case QEvent::DragMove:
            ((QDragMoveEvent*)ev)->acceptProposedAction();
            return true;
        case QEvent::Drop: {
            QDropEvent* de = (QDropEvent*)ev;
            const QList<QUrl> urls = de->mimeData()->urls();
            for (const QUrl& u : urls) {
                const QString f = u.toLocalFile();
                if (!f.isEmpty() && QFile::exists(f)) {
                    *pl << f;
                    if (openPl)
                        openPl(pl->size() - 1);
                }
            }
            de->acceptProposedAction();
            return true;
        }
        default:
            return QObject::eventFilter(obj, ev);
        }
    }
};

// mpv 二段起播:render_context 须在场景图线程(有 GL context)创建
class CxMpvAttachJob : public QRunnable
{
public:
    CxMpvAttachJob(CXEngineMpv* e, CXVideoPlayer* p, const QString& u,
                   MainPage* page)
        : m_em(e), m_player(p), m_url(u), m_page(page) { }
    void run() override {
        const bool ok = m_em->attachQuickRender();
        QMetaObject::invokeMethod(m_player, [this, ok]() {
            if (ok) {
                m_em->quickGo(); // 二段起播:loadfile(见引擎注释)
                m_player->play();
            } else if (m_page) {
                m_page->showEngineError(
                    trKey("engine.mpv_render_failed"));
                QTimer::singleShot(2500, qApp, SLOT(quit()));
            }
        }, Qt::QueuedConnection);
    }
private:
    CXEngineMpv* m_em;
    CXVideoPlayer* m_player;
    QString m_url;
    QPointer<MainPage> m_page;
};

// 截图目录:QLVOTUBE_SHOT_DIR 环境变量优先,缺省 CacheLocation;确保存在
static QString cxShotDir()
{
    const char* env = getenv("QLVOTUBE_SHOT_DIR");
    QString dir = (env != 0 && *env != 0)
        ? QString::fromLocal8Bit(env)
        : QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    return dir;
}

MainPage::MainPage(QQuickItem* parent)
    : Page(parent)
{
    m_dbg = getenv("QLVOTUBE_DEBUG") != 0;
    const char* engineEnv = getenv("QLVOTUBE_ENGINE");
    m_wantMpv = (engineEnv != 0 && strcmp(engineEnv, "mpv") == 0);
}

MainPage::~MainPage()
{
    if (m_player != 0) {
        m_player->stop();
        delete m_player;
        m_player = nullptr;
    }
}

void MainPage::setLoggedIn(const QString& nickname, const QString& avatarSeed)
{
    const bool loggedIn = !nickname.isEmpty();
    if (m_userAvatar != 0)
        m_userAvatar->setSeed(loggedIn || !avatarSeed.isEmpty()
                              ? (avatarSeed.isEmpty() ? nickname : avatarSeed)
                              : QStringLiteral("anonymous"));
    if (m_userName != 0)
        m_userName->setText(loggedIn ? nickname : trKey("user.not_login"));
    if (m_userStatus != 0)
        m_userStatus->setText(loggedIn ? trKey("user.logged_in")
                                       : trKey("user.click_to_login"));
}

void MainPage::retranslate()
{
    // 用户区(按登录态重算)/ EQ 档位名 / 窗口标题
    setLoggedIn(QSettings().value("userName").toString(),
                QSettings().value("userAvatarSeed").toString());
    static const char* const kEqKeys[5] = {
        "player.eq_default", "player.eq_bright", "player.eq_contrast",
        "player.eq_saturation", "player.eq_warm"
    };
    if (m_eqBtn != nullptr && m_eqIdx >= 0 && m_eqIdx < 5)
        m_eqBtn->setText(trKey(kEqKeys[m_eqIdx]));
    if (QskWindow* w = qobject_cast<QskWindow*>(window())) {
        if (m_pl.isEmpty() || m_plIdx < 0 || m_plIdx >= m_pl.size())
            return;
        w->setTitle(m_pl[m_plIdx].isEmpty()
                    ? ("qskvotube (" + trKey("player.no_media") + ")")
                    : ("qskvotube - " + QFileInfo(m_pl[m_plIdx]).fileName()));
    }
}

void MainPage::stopPlayback(){
    if (m_player != 0)
        m_player->stop();
}

void MainPage::showEngineError(const QString& text)
{
    if (m_box == 0)
        return;
    m_box->addSpacer(20);
    m_box->addItem(new QskTextLabel(text));
}

void MainPage::applyKeepScreenOn(bool on)
{
    m_keepScreenOn = on;
    setKeepScreenOn(on);   // Android: FLAG_KEEP_SCREEN_ON;桌面空操作
}

void MainPage::onResume()
{
    applyKeepScreenOn(m_keepScreenOn);   // 从设置页返回时重申(Android)
}

void MainPage::onCreate(const QVariantMap&, const QVariantMap&)
{
    setAutoLayoutChildren(true);
    m_box = new QskLinearBox(Qt::Vertical, this);
    // 深色方案下需要皮肤填充背景,否则浅字画在窗口底(此前固定亮色的原因)
    m_box->setPanel(true);
    m_box->setMargins(0);
    m_box->setSpacing(2);
    m_box->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Expanding);

    // ── TopBar:左用户区 | 中标题 | 右 ⋮ 主菜单 ──
    {
        auto* topBar = new QskLinearBox(Qt::Horizontal, m_box);
        topBar->setPanel(true);
        topBar->setPreferredHeight(48);
        topBar->setSpacing(6);

        // 左:identicon 头像 28dp + 两行(昵称/状态);整块点击=登录占位(空操作)
        auto* userBox = new QskLinearBox(Qt::Horizontal, topBar);
        userBox->setSpacing(8);
        m_userAvatar = new CxUserAvatar(userBox);
        m_userAvatar->setPreferredSize(28, 28);
        auto* userCol = new QskLinearBox(Qt::Vertical, userBox);
        userCol->setSpacing(0);
        userCol->setPreferredWidth(96);
        m_userName = new QskTextLabel(trKey("user.not_login"), userCol);
        m_userName->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        m_userStatus = new QskTextLabel(trKey("user.click_to_login"), userCol);
        m_userStatus->setFontRole(
            QskFontRole(QskFontRole::Caption, QskFontRole::Normal));
        m_userStatus->setTextColor(QColor(0x9a, 0x9a, 0x9a));
        m_userStatus->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);

        // 登录占位:空操作(Android toast;桌面仅 debug 日志)
        auto* userTap = new QQuickTapHandler(userBox);
        connect(userTap, &QQuickTapHandler::singleTapped, this,
            [this](QEventPoint, Qt::MouseButton) {
                showAndroidToast(trKey("user.login_developing"));
                if (m_dbg)
                    fprintf(stderr, "user: %s\n",
                            trKey("user.login_developing").toUtf8().constData());
            });

        auto* title = new QskTextLabel("qskvotube", topBar);
        title->setAlignment(Qt::AlignCenter);
        title->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
        auto* optionsBtn = new QskPushButton(QString::fromUtf8("⋮"), topBar);
        optionsBtn->setPreferredSize(44, 44);
        connect(optionsBtn, &QskAbstractButton::clicked,
            this, [this, optionsBtn]() {
                const QPointF p = optionsBtn->mapToItem(this, QPointF(0, 0));
                // 菜单位置在按钮下方、右缘对齐
                showOptionsMenu(QPointF(
                    p.x() + optionsBtn->width(),
                    p.y() + optionsBtn->height()));
            });

        // 登录态(两版统一 key:userName / userAvatarSeed;空=未登录)
        setLoggedIn(QSettings().value("userName").toString(),
                    QSettings().value("userAvatarSeed").toString());
    }

    // ── 视频区 + 引擎/渲染器显式配对(不互备)──
    m_player = new CXVideoPlayer();

    // 引擎:env 优先(QA),其次设置页持久化值(重启生效),缺省 ffmpeg
    if (qgetenv("QLVOTUBE_ENGINE").isEmpty()) {
        m_wantMpv = QSettings().value("engine", QString("ffmpeg")).toString()
            == QLatin1String("mpv");
    }

    if (m_wantMpv) {
        if (QQuickWindow::graphicsApi() != QSGRendererInterface::OpenGL) {
            showEngineError(
                trKey("engine.mpv_needs_gl"));
            QTimer::singleShot(2500, qApp, SLOT(quit()));
            return;
        }
        m_mpvItem = new CXQuickMpvFboItem();
        m_box->addItem(m_mpvItem);
        m_box->setStretchFactor(m_mpvItem, 1);
    } else {
        m_ffItem = new CXQuickVideoItem();
        m_box->addItem(m_ffItem);
        m_box->setStretchFactor(m_ffItem, 1);
    }

    // 字幕叠加(浮于视频 item 上层;z 更高;无 layout 父项,绝对定位)
    m_subLabel = new QskTextLabel("");
    m_subLabel->setVisible(false);

    // ── 控制条 ──
    buildControls(m_box);

    // ── 播放列表:argv 位置参数 → AUTOPLAY → QSettings 恢复 ──
    QString url = QString::fromLocal8Bit(qgetenv("QLVOTUBE_AUTOPLAY"));
#ifndef Q_OS_ANDROID
    if (url.isEmpty() && qgetenv("QLVOTUBE_NOPLAY").isEmpty())
        url = "/tmp/opencode/m5a_long.mp4";   // 桌面开发缺省媒体
#endif

    bool argvFilesEmpty = true;
    const QStringList argvL = qApp->arguments();
    for (int i = 1; i < argvL.size(); ++i) {
        const QString a = argvL[i];
        if (!a.startsWith("-") && QFile::exists(a)) {
            m_pl << a;
            argvFilesEmpty = false;
        }
    }
    if (m_pl.isEmpty() && !url.isEmpty())
        m_pl << url;
    m_plSet = new QSettings(QString::fromUtf8("qlvotube"),
                            QString::fromUtf8("qskvotube"), this);
    if (argvFilesEmpty && qgetenv("QLVOTUBE_AUTOPLAY").isEmpty()) {
        QStringList saved = m_plSet->value("playlist").toStringList();
        for (int i = saved.size() - 1; i >= 0; --i) {
            if (!QFile::exists(saved[i]))
                saved.removeAt(i);   // 失效文件剔除
        }
        if (!saved.isEmpty()) {
            m_pl = saved;
            m_plIdx = m_plSet->value("plIdx", 0).toInt();
            if (m_plIdx < 0 || m_plIdx >= m_pl.size())
                m_plIdx = 0;
        }
    }
    if (m_pl.isEmpty())
        m_pl << QString();   // 占位(无媒体):不越界,open 走空串守卫

    // ── 引擎 ──
    if (!m_player->setEngine(m_wantMpv ? CxEngineMpv : CxEngineFfmpeg) ||
        m_player->engine() == 0) {
        showEngineError(m_wantMpv ? trKey("engine.mpv_unavailable")
                                  : trKey("engine.ffmpeg_unavailable"));
        QTimer::singleShot(2500, qApp, SLOT(quit()));
        return;
    }
    if (m_wantMpv) {
        CXEngineMpv* em = (CXEngineMpv*)m_player->engine();
        em->setQuickRender(true);   // 须在 engine open 前
        m_mpvItem->bindEngine(em);
    } else {
        m_player->setRenderer(m_ffItem);
    }

    // ── 状态/信号接线(成员状态,lambda 不捕局部引用)──
    connect(m_player, &CXVideoPlayer::positionChanged, this, [this](long ms) {
        if (m_durMs > 0)
            m_prog->setValue(ms);
        m_timeLbl->setText(cxMsToText(ms) + " / " + cxMsToText(m_durMs));
        if (m_dbg && (ms % 1000) < 100)
            fprintf(stderr, "pos=%ld\n", ms);
    });
    connect(m_player, &CXVideoPlayer::durationChanged, this, [this](long ms) {
        m_durMs = ms;
        if (ms > 0)
            m_prog->setMaximum(ms);
        m_timeLbl->setText("0:00 / " + cxMsToText(m_durMs));
    });
    connect(m_player, &CXVideoPlayer::subtitleChanged, this,
        [this](const QString& text) {
            if (text.isEmpty()) {
                m_subLabel->setVisible(false);
                return;
            }
            m_subLabel->setText(text);
            const qreal h = (m_ffItem != 0) ? m_ffItem->height()
                                            : (m_mpvItem != 0 ? m_mpvItem->height() : 0);
            m_subLabel->setPosition(QPointF(20, h - 40));
            m_subLabel->setZ(2);
            m_subLabel->setVisible(true);
            if (m_dbg)
                fprintf(stderr, "subtitle-text: [%s]\n",
                        text.toUtf8().constData());
        });
    connect(m_player, &CXVideoPlayer::noticedError, this,
        [](int code, const QString& msg) {
            fprintf(stderr, "qskvotube error: code=%d %s\n",
                    code, msg.toUtf8().constData());
        });
    connect(m_player, &CXVideoPlayer::started,
        this, [this]() { m_playBtn->setText("⏸"); });
    connect(m_player, &CXVideoPlayer::paused,
        this, [this]() { m_playBtn->setText("▶"); });
    connect(m_player, &CXVideoPlayer::stopped,
        this, [this]() { m_playBtn->setText("▶"); });
    connect(m_player, &CXVideoPlayer::finished,
        this, [this]() { openPl(m_plIdx + 1); });   // 播完自动连播

    // ── 拖拽导入 ──
    m_dropHook = new CxDropHook(this);
    m_dropHook->pl = &m_pl;
    m_dropHook->openPl = [this](int idx) { openPl(idx); };
    if (window() != 0)
        window()->installEventFilter(m_dropHook);

    // ── 屏幕常亮(延迟 50ms:窗口未就绪时 Android Window 无效)──
    m_keepScreenOn = QSettings().value("keepScreenOn", true).toBool();
    QTimer::singleShot(50, this, [this]() { applyKeepScreenOn(m_keepScreenOn); });

    // ── QA 自动驱动(与 qlvotube 同格式)──
    if (getenv("QLVOTUBE_QA") != 0) {
        QTimer::singleShot(3000, this, [this]() { m_player->pause(); });
        QTimer::singleShot(5000, this, [this]() {
            fprintf(stderr, "qa: step=pause ok=%d state=%d\n",
                    (int)(m_player->state() == CxPaused),
                    (int)m_player->state());
            m_player->play();
        });
        QTimer::singleShot(7000, this, [this]() {
            m_player->seek(5000);
            fprintf(stderr, "qa: step=seek issued=1 pos=%ld\n",
                    m_player->position());
        });
        QTimer::singleShot(9000, this, [this]() {
            m_player->setRate(1.5);
            fprintf(stderr, "qa: step=rate ok=1 pos=%ld\n",
                    m_player->position());
        });
        QTimer::singleShot(11000, this, [this]() {
            int n = m_player->subtitleTrackCount();
            bool ok = (n <= 0) || m_player->setSubtitleTrack(0);
            fprintf(stderr, "qa: step=cc tracks=%d ok=%d\n", n, (int)ok);
        });
        QTimer::singleShot(13000, this, [this]() {
            CXVideoEq eq;
            eq.brightness = 60;
            m_player->setEq(eq);
            fprintf(stderr, "qa: step=eq ok=1\n");
        });
        QTimer::singleShot(15000, this, [this]() {
            const QString path = cxShotDir() + "/qa_" +
                QString::number(QDateTime::currentSecsSinceEpoch()) + ".png";
            fprintf(stderr, "qa: step=shot ok=%d path=%s\n",
                    (int)m_player->saveScreenshot(path),
                    path.toUtf8().constData());
        });
    }

    // ── 多语言热切:用户区/标题/EQ 档位名重译 ──
    connect(&Translator::instance(), &Translator::languageChanged,
            this, [this]() { retranslate(); });

    // ── 起播 ──
    if (QskWindow* w = qobject_cast<QskWindow*>(window())) {
        w->setTitle(m_pl[m_plIdx].isEmpty()
                    ? ("qskvotube (" + trKey("player.no_media") + ")")
                    : ("qskvotube - " + QFileInfo(m_pl[m_plIdx]).fileName()));
    }
    if (!m_pl[m_plIdx].isEmpty()) {
        if (m_wantMpv) {
            CXEngineMpv* em = (CXEngineMpv*)m_player->engine();
            m_player->open(m_pl[m_plIdx]);   // 二段挂起:loadfile 延至 attach
            if (QskWindow* w = qobject_cast<QskWindow*>(window())) {
                w->scheduleRenderJob(
                    new CxMpvAttachJob(em, m_player, m_pl[m_plIdx], this),
                    QQuickWindow::BeforeSynchronizingStage);
            }
        } else if (getenv("QLVOTUBE_NOPLAY") == 0) {
            m_player->open(m_pl[m_plIdx]);
            m_player->play();
        }
    }

    if (getenv("QLVOTUBE_AUTOQUIT_MS") != 0)
        QTimer::singleShot(atoi(getenv("QLVOTUBE_AUTOQUIT_MS")),
                           qApp, SLOT(quit()));
}

void MainPage::openPl(int idx)
{
    if (m_pl.isEmpty() || m_player == 0)
        return;
    if (idx < 0)
        idx = m_pl.size() - 1;    // 上一首到头回绕
    if (idx >= m_pl.size())
        idx = -1;                 // 列表播完:停,不循环
    m_plIdx = idx;
    QskWindow* w = qobject_cast<QskWindow*>(window());
    if (m_plIdx < 0) {
        m_player->stop();
        if (w != 0)
            w->setTitle("qskvotube (" + trKey("player.list_finished") + ")");
        return;
    }
    if (m_pl[m_plIdx].isEmpty()) {
        if (w != 0)
            w->setTitle("qskvotube (" + trKey("player.no_media") + ")");
        return;                   // 无媒体:不 open/play
    }
    if (w != 0)
        w->setTitle("qskvotube - " + QFileInfo(m_pl[m_plIdx]).fileName());
    m_player->open(m_pl[m_plIdx]);
    m_player->play();
    if (m_plSet != 0) {           // 持久化
        m_plSet->setValue("playlist", m_pl);
        m_plSet->setValue("plIdx", m_plIdx);
    }
}

void MainPage::buildControls(QskLinearBox* box)
{
    auto* playBtn = new QskPushButton("▶");
    auto* prevBtn = new QskPushButton("⏮");   // 列表上一
    auto* nextBtn = new QskPushButton("⏭");   // 列表下一
    auto* prog = new QskSlider(Qt::Horizontal);
    prog->setSizePolicy(QskSizePolicy::Ignored, QskSizePolicy::Fixed);
    prog->setMinimum(0);
    prog->setMaximum(1000);
    auto* timeLbl = new QskTextLabel("0:00 / 0:00");
    auto* vol = new QskSlider(Qt::Horizontal);
    vol->setSizePolicy(QskSizePolicy::Ignored, QskSizePolicy::Fixed);
    vol->setMinimum(0);
    vol->setMaximum(100);
    vol->setValue(100);
    auto* muteBtn = new QskPushButton("🔇");
    auto* rateBtn = new QskPushButton("1.0x");
    auto* ccBtn = new QskPushButton("CC");
    auto* eqBtn = new QskPushButton("EQ");
    auto* shotBtn = new QskPushButton("图");
    auto* subBtn = new QskPushButton("字");      // 外挂字幕
    auto* skinBtn = new QskPushButton("🎨");     // 皮肤热切循环

    // 默认 preferred 宽 120 会撑出窗口——压窄(anystik 同法)
    for (QskPushButton* b : { playBtn, prevBtn, nextBtn, muteBtn,
                              rateBtn, ccBtn, eqBtn, shotBtn, subBtn, skinBtn })
        b->setPreferredSize(44, 36);

    m_playBtn = playBtn;
    m_prog = prog;
    m_timeLbl = timeLbl;

    auto* ctlA = new QskLinearBox(Qt::Horizontal);
    ctlA->setMargins(4);
    ctlA->setSpacing(6);
    auto* ctlB = new QskLinearBox(Qt::Horizontal);
    ctlB->setMargins(4);
    ctlB->setSpacing(6);

    ctlA->addItem(playBtn);
    ctlA->addItem(prevBtn);
    ctlA->addItem(nextBtn);
    ctlA->addItem(prog);
    ctlA->setStretchFactor(prog, 1);
    ctlA->addItem(timeLbl);
    ctlB->addItem(vol);
    ctlB->setStretchFactor(vol, 1);
    ctlB->addItem(muteBtn);
    ctlB->addItem(rateBtn);
    ctlB->addItem(ccBtn);
    ctlB->addItem(eqBtn);
    ctlB->addItem(shotBtn);
    ctlB->addItem(subBtn);
    ctlB->addItem(skinBtn);
    box->addItem(ctlA);
    box->addItem(ctlB);

    connect(playBtn, &QskAbstractButton::clicked, this, [this]() {
        if (m_player->state() == CxPlaying)
            m_player->pause();
        else
            m_player->play();
    });
    connect(nextBtn, &QskAbstractButton::clicked, this, [this]() {
        m_player->stop();
        openPl(m_plIdx + 1);
    });
    connect(prevBtn, &QskAbstractButton::clicked, this, [this]() {
        m_player->stop();
        openPl(m_plIdx - 1);
    });
    connect(muteBtn, &QskAbstractButton::clicked, this, [this, muteBtn]() {
        m_muted = !m_muted;
        m_player->setMute(m_muted);
        muteBtn->setText(m_muted ? "🔊" : "🔇");
    });
    // 皮肤热切循环(皮肤名大小写敏感!切换后重设主题)
    connect(skinBtn, &QskAbstractButton::clicked, this, []() {
        static const char* kSkins[] = { "Fusion", "Material3", "Fluent2" };
        const QString cur = qskSkinManager->skinName();
        int idx = 0;
        for (int i = 0; i < 3; i++)
            if (cur == kSkins[i])
                idx = i;
        qskSkinManager->setSkin(kSkins[(idx + 1) % 3]);
        cxApplySkinTheme(qskSkinManager->skin());
    });
    // QskSlider 无 pressed 信号:用户拖动值与引擎回投位差 >1.2s 视为手势
    connect(prog, &QskSlider::valueChanged, this, [this](qreal v) {
        if (m_durMs > 0 && qAbs((long)v - m_player->position()) > 1200)
            m_player->seek((long)v);
    });
    connect(vol, &QskSlider::valueChanged, this, [this](qreal v) {
        m_player->setVolume((int)v);
    });
    connect(rateBtn, &QskAbstractButton::clicked, this, [this, rateBtn]() {
        m_rateIdx = (m_rateIdx + 1) % 4;
        m_player->setRate(m_rates[m_rateIdx]);
        rateBtn->setText(QString("%1x").arg(m_rates[m_rateIdx]));
    });
    connect(ccBtn, &QskAbstractButton::clicked, this, [this]() {
        const int n = m_player->subtitleTrackCount();
        if (n <= 0)
            return;
        m_subIdx = m_subIdx >= n - 1 ? -1 : m_subIdx + 1;
        m_player->setSubtitleTrack(m_subIdx);   // -1=关;引擎幂等
    });
    connect(eqBtn, &QskAbstractButton::clicked, this, [this, eqBtn]() {
        static const struct {
            const char* key;
            CXVideoEq eq;
        } kEqs[] = {
            { "player.eq_default",    cxvEq(  0,  0,  0,   0) },
            { "player.eq_bright",     cxvEq( 60, 10,  0,   0) },
            { "player.eq_contrast",   cxvEq(  0, 50, 10,   0) },
            { "player.eq_saturation", cxvEq(  0,  0, 60,   0) },
            { "player.eq_warm",       cxvEq( 10,  0,  0, -30) },
        };
        m_eqIdx = (m_eqIdx + 1) % 5;
        m_eqBtn = eqBtn;
        m_player->setEq(kEqs[m_eqIdx].eq);
        eqBtn->setText(trKey(kEqs[m_eqIdx].key));
    });
    connect(shotBtn, &QskAbstractButton::clicked, this, [this]() {
        const QString path = cxShotDir() + "/shot_" +
            QString::number(QDateTime::currentSecsSinceEpoch()) + ".png";
        fprintf(stderr, "qskvotube shot %s -> %d\n",
                path.toUtf8().constData(),
                (int)m_player->saveScreenshot(path));
    });
    connect(subBtn, &QskAbstractButton::clicked, this, [this]() {
        // 外挂字幕:setSubtitleFile(srt/ass);解析失败走 noticedError
        const QString f = QFileDialog::getOpenFileName(
            nullptr, trKey("player.subtitle_open"), QString(),
            trKey("player.subtitle_filter"));
        if (!f.isEmpty())
            m_player->setSubtitleFile(f);
    });
}

void MainPage::showOptionsMenu(const QPointF& origin)
{
    // 旧菜单/遮罩先清(重复点击不叠加)
    for (auto* old : findChildren<QskMenu*>())
        old->deleteLater();
    for (auto* old : findChildren<MenuOverlay*>())
        old->deleteLater();

    auto* menu = new QskMenu(this);
    menu->setModal(true);
    menu->setPopupFlag(QskPopup::DeleteOnClose, false);   // 防与 overlay 双删

    const int idxKeepOn = menu->addOption(QskLabelData(
        m_keepScreenOn ? ("✓ " + trKey("menu.keep_screen_on"))
                       : ("  " + trKey("menu.keep_screen_on"))));
    menu->addSeparator();
    const int idxSettings = menu->addOption(QskLabelData(trKey("menu.settings")));
    const int idxAbout = menu->addOption(QskLabelData(trKey("menu.about")));

    qreal menuW = menu->implicitWidth();
    if (menuW <= 0)
        menuW = 200;
    menu->setOrigin(QPointF(origin.x() - menuW, origin.y()));

    connect(menu, &QskMenu::triggered, this,
        [this, idxKeepOn, idxSettings, idxAbout](int index) {
            if (auto* pm = pageManager()) {
                if (index == idxSettings)
                    pm->open("settings");
                else if (index == idxAbout)
                    pm->open("about");
            }
            if (index == idxKeepOn) {
                applyKeepScreenOn(!m_keepScreenOn);
                QSettings().setValue("keepScreenOn", m_keepScreenOn);
            }
            if (auto* m = qobject_cast<QskMenu*>(sender()))
                m->close();
        });

    auto* overlay = new MenuOverlay(menu);
    connect(menu, &QObject::destroyed, overlay, &QObject::deleteLater);
    connect(menu, &QskPopup::closed, overlay, &QObject::deleteLater);

    menu->open();
}
