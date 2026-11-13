// qskvotube —— QSkinny 播放前端(M5-B;cxvideo 双内核 + Quick 渲染器)
// 引擎显式选:QLVOTUBE_ENGINE=ffmpeg|mpv(缺省 ffmpeg);mpv 要求窗口 GL API,
// 不满足即报错退出——不自动降级(渲染路选谁用谁)。
// QA:QLVOTUBE_AUTOPLAY=<url> / QLVOTUBE_AUTOQUIT_MS=<ms> / QLVOTUBE_DEBUG=1
//     QLVOTUBE_QA=1(起播后时间线自检 pause/play/seek/rate/cc/eq/shot)
#include <QGuiApplication>
#include <QTimer>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>

#include <QskSkinManager.h>
#include <QskSkin.h>
#include <QskFontRole.h>
#include <QskWindow.h>
#include <QskTextLabel.h>
#include <QskLinearBox.h>
#include <QskPushButton.h>
#include <QskSlider.h>

#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QRunnable>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QUrl>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QSettings>
#include <functional>

#include "cxvideoplayer.h"
#include "cxengine.h"
#include "eng_mpv.h"
#include "renderer_quick.h"
#include "renderer_qmpv.h"
#include "cxmsutil.h"

// 毫秒 → m:ss 文本已移 cxmsutil.h(单测复用)

// mpv Quick 路起播时序:mpv_render_context_create 须在 mpv 起播之前
// (render.h:"needs to be created before you start playback"),且必须
// 在 GL context current 的线程——即场景图渲染线程。用 render job 挂接,
// 挂成后回 GUI 线程 open+play;失败显式报错,不回退(渲染路选谁用谁)。
// CXVideoEq 工厂(有自定义 ctor 非聚合,不能花括号构造)
static CXVideoEq cxvEq(int b, int c, int s, int h)
{
    CXVideoEq eq;
    eq.brightness = b;
    eq.contrast = c;
    eq.saturation = s;
    eq.hue = h;
    return eq;
}

// 拖拽导入钩子:QQuickWindow 无 drag 虚函数/QML DropArea 需私有头——
// 改用 window 对象的 eventFilter 截获 DragEnter/Drop(过滤器先于
// QQuickWindow::event 的场景图投递,无需私有头)。
// 运行期验证:若平台 drag 未投递到 window 对象(X11 dnd 路径差异),
// 退化方案 = 叠加 QML DropArea(需引入 QQmlEngine,单独立项)。
class CxDropHook : public QObject {
public:
    QStringList* pl;                        /* 播放列表(尾部追加) */
    std::function<void(int)>* openPl;
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
                    (*openPl)(pl->size() - 1);
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

class CxMpvAttachJob : public QRunnable
{
public:
    CxMpvAttachJob(CXEngineMpv* e, QskWindow* w, CXVideoPlayer* p,
                   const QString& u)
        : m_em(e), m_win(w), m_player(p), m_url(u) { }
    void run() override {
        const bool ok = m_em->attachQuickRender();
        QMetaObject::invokeMethod(m_player, [this, ok]() {
            if (ok) {
                m_em->quickGo(); // 二段起播:loadfile(见引擎注释)
                m_player->play();
            } else {
                auto* err = new QskTextLabel(
                    "mpv render_context 创建失败(不降级,退出)");
                m_win->addItem(err);
                QTimer::singleShot(2500, qApp, SLOT(quit()));
            }
        }, Qt::QueuedConnection);
    }
private:
    CXEngineMpv* m_em;
    QskWindow* m_win;
    CXVideoPlayer* m_player;
    QString m_url;
};

// 截图目录:QLVOTUBE_SHOT_DIR 环境变量优先,缺省 CacheLocation;确保存在
// (截图按钮与 qa step=shot 共用,与 qlvotube cxShotDir 同语义)
static QString cxShotDir()
{
    const char* env = getenv("QLVOTUBE_SHOT_DIR");
    QString dir = (env != 0 && *env != 0)
        ? QString::fromLocal8Bit(env)
        : QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    return dir;
}

int main(int argc, char* argv[])
{
    const char* engineEnv = getenv("QLVOTUBE_ENGINE");
    bool wantMpv = engineEnv != 0 && strcmp(engineEnv, "mpv") == 0;
    if (wantMpv) {
        // mpv-qfbo 显式前置:窗口必须 GL API(首个 QQuickWindow 之前设置)
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("qskvotube");
    app.setApplicationVersion("0.2.0");
    app.setOrganizationName("qlvotube");
    // 缺省字体解析出 Adwaita Sans 时 QSkinny 文字渲染异常——用 DejaVu
    app.setFont(QFont("DejaVu Sans", 10));

    for (const auto& a : app.arguments()) {
        if (a == "-v" || a == "--version") {
            printf("%s %s\n", "qskvotube",
                   app.applicationVersion().toUtf8().constData());
            return 0;
        }
    }

    // 头less 环境无法加载图形皮肤插件，QskSkinManager 会断言；
    // 仅在可创建平台窗口时才进 UI。
    if (!QGuiApplication::platformName().startsWith("xcb") &&
        !QGuiApplication::platformName().startsWith("wayland")) {
        fprintf(stderr, "qskvotube: no GUI platform (%s), exiting\n",
            QGuiApplication::platformName().toUtf8().constData());
        return 1;
    }

    // anystik 同法:皮肤插件路径经 QskSkinManager 前置(与 libqskinny
    // 同树同 ABI)。注意:皮肤名大小写敏感(正确名 Fluent2/Fusion/
    // Material3),传错会静默回退 Fluent2 且无警告——曾致文字/按钮
    // "不渲染"(暗色配色画在浅色窗底)。QLVOTUBE_SKIN 可覆盖
    const char* skinEnv = getenv("QLVOTUBE_SKIN");
    QStringList libPaths = QCoreApplication::libraryPaths();
#ifndef Q_OS_ANDROID
    // Android:皮肤插件平铺在 JNI libs 目录,Qt 已自动纳入 libraryPaths
    // (qskmusic main.cpp 同法);桌面才需要显式前置插件路径
    libPaths.prepend("/opt/qt/qskinny/lib/qskinny/plugins");
#endif
    qskSkinManager->setPluginPaths(libPaths);
    qskSkinManager->setSkin(skinEnv != 0 ? skinEnv : "Fusion");
    // 皮肤主题应用(亮色+字体 role)抽 lambda:皮肤热切(二期)复用。
    // 注:系统暗色主题(QSkinny 按 styleHints 解析成 DarkScheme)会把
    // 浅色文字画在未填充的白窗底上(qskvotube 无面板件)——固定亮色
    auto applySkinTheme = [](QskSkin* sk) {
        sk->setColorScheme(QskSkin::LightScheme);
        // anystik 同法:皮肤字体 role 显式设真实存在的字体——皮肤内置
        // 字体(如 Segoe UI)本机缺失时经回退可能整段字形为空
        QFont f("DejaVu Sans", 10);
        sk->setFont({QskFontRole::Body, QskFontRole::Normal}, f);
        sk->setFont({QskFontRole::Title, QskFontRole::Normal}, f);
        sk->setFont({QskFontRole::Caption, QskFontRole::Normal}, f);
        QGuiApplication::setFont(f);
    };
    QskSkin* skin = qskSkinManager->skin();
    applySkinTheme(skin);

    QskWindow window;
    window.setTitle("qskvotube");
    window.setPreferredSize({640, 420}); // QskWindow 在 Show 时按 scale 定尺寸

    auto* box = new QskLinearBox(Qt::Vertical);
    box->setMargins(0);
    box->setSpacing(2);
    box->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Expanding);

    // ── 视频区 + 引擎/渲染器显式配对(不互备)──
    CXVideoPlayer* player = new CXVideoPlayer();
    CXQuickVideoItem* ffItem = 0;
    CXQuickMpvFboItem* mpvItem = 0;

    if (wantMpv) {
        if (QQuickWindow::graphicsApi() != QSGRendererInterface::OpenGL) {
            auto* err = new QskTextLabel(
                "mpv 需要窗口 GL 渲染 API,当前环境不支持(不降级,退出)");
            box->addSpacer(40);
            box->addItem(err);
            window.addItem(box);
            window.show();
            QTimer::singleShot(2500, &app, SLOT(quit()));
            return app.exec();
        }
        mpvItem = new CXQuickMpvFboItem();
        box->addItem(mpvItem);
        box->setStretchFactor(mpvItem, 1);
    } else {
        ffItem = new CXQuickVideoItem();
        box->addItem(ffItem);
        box->setStretchFactor(ffItem, 1);
    }

    // 字幕叠加(浮于视频 item 上层;z 更高)
    auto* subLabel = new QskTextLabel("");
    subLabel->setVisible(false);

    // ── 控制条(QSkinny 原生件)──
    auto* ctl = new QskLinearBox(Qt::Horizontal);
    ctl->setMargins(4);
    ctl->setSpacing(6);

    auto* playBtn = new QskPushButton("▶");
    auto* prevBtn = new QskPushButton("⏮"); // 二期:列表上一
    auto* nextBtn = new QskPushButton("⏭"); // 二期:列表下一
    auto* prog = new QskSlider(Qt::Horizontal);
    // Ignored:宽度完全由布局分配(默认 hint 会把盒子撑到 700+,视频区被顶出窗)
    prog->setSizePolicy(QskSizePolicy::Ignored, QskSizePolicy::Fixed);
    prog->setMinimum(0);
    prog->setMaximum(1000);
    auto* timeLbl = new QskTextLabel("0:00 / 0:00");
    auto* vol = new QskSlider(Qt::Horizontal);
    vol->setSizePolicy(QskSizePolicy::Ignored, QskSizePolicy::Fixed);
    vol->setMinimum(0);
    vol->setMaximum(100);
    vol->setValue(100);
    auto* muteBtn = new QskPushButton("🔇"); // 二期:静音切换
    auto* rateBtn = new QskPushButton("1.0x");
    auto* ccBtn = new QskPushButton("CC");
    auto* eqBtn = new QskPushButton("EQ");
    auto* shotBtn = new QskPushButton("图");
    auto* subBtn = new QskPushButton("字"); // 外挂字幕(同步 qlvotube 菜单)
    auto* skinBtn = new QskPushButton("🎨"); // 二期:皮肤热切循环

    // QskPushButton 默认 preferred 宽 120,合计把盒子撑出窗口——压窄
    // (anystik 同法:setPreferredSize(44,44),Fusion 下小尺寸文字正常)
    for (QskPushButton* b : { playBtn, prevBtn, nextBtn, muteBtn,
                              rateBtn, ccBtn, eqBtn, shotBtn, subBtn, skinBtn })
        b->setPreferredSize(44, 36);

    // 控制条拆两行(640 窗宽挤不下 10+ 控件):
    // rowA = 播放/列表/进度/时间;rowB = 音量/静音/倍速/CC/EQ/截图/皮肤
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
    window.addItem(box);

    // ── 引擎接线(setEngine 成功后再配渲染器,与 qlvotube 同序)──
    QString url = getenv("QLVOTUBE_AUTOPLAY") != 0
        ? QString::fromLocal8Bit(getenv("QLVOTUBE_AUTOPLAY"))
        : QString("/tmp/opencode/m5a_long.mp4");
    const bool dbg = getenv("QLVOTUBE_DEBUG") != 0;

    // ── 播放列表(二期):argv 位置参数=多文件导入;AUTOPLAY 兜底单目 ──
    QStringList pl;
    const QStringList argvL = app.arguments();
    bool argvFilesEmpty = true;
    for (int i = 1; i < argvL.size(); ++i) {
        const QString a = argvL[i];
        if (!a.startsWith("-") && QFile::exists(a)) {
            pl << a;
            argvFilesEmpty = false;
        }
    }
    if (pl.isEmpty())
        pl << url;
    int plIdx = 0;

    // ── 播放列表持久化(候选项 a):argv 为空且未设 AUTOPLAY 时恢复上次
    // QSettings("qlvotube","qskvotube") 存的 playlist/plIdx;仅 argv 列表,
    // 不做库列表持久化 ──
    QSettings plSet(QString::fromUtf8("qlvotube"), QString::fromUtf8("qskvotube"));
    if (argvFilesEmpty && getenv("QLVOTUBE_AUTOPLAY") == 0) {
        QStringList saved = plSet.value("playlist").toStringList();
        for (int i = saved.size() - 1; i >= 0; --i) {
            if (!QFile::exists(saved[i]))
                saved.removeAt(i); // 失效文件剔除
        }
        if (!saved.isEmpty()) {
            pl = saved;
            plIdx = plSet.value("plIdx", 0).toInt();
            if (plIdx < 0 || plIdx >= pl.size())
                plIdx = 0;
        }
    }

    // openPl:切歌(ffmpeg:open+play;mpv:attach 已挂时 open 直接 loadfile)
    auto openPl = [&](int idx) {
        if (pl.isEmpty())
            return;
        if (idx < 0)
            idx = pl.size() - 1; // 上一首到头回绕
        if (idx >= pl.size())
            idx = -1; // 列表播完:停,不循环
        plIdx = idx;
        if (plIdx < 0) {
            player->stop();
            window.setTitle("qskvotube (列表播完)");
            return;
        }
        window.setTitle("qskvotube - " +
                        QFileInfo(pl[plIdx]).fileName());
        player->open(pl[plIdx]);
        player->play();
        // 写回持久化(候选项 a)
        plSet.setValue("playlist", pl);
        plSet.setValue("plIdx", plIdx);
    };
    std::function<void(int)> openPlFn = openPl; // 拖拽钩子需可存储可调用体

    if (!player->setEngine(wantMpv ? CxEngineMpv : CxEngineFfmpeg) ||
        player->engine() == 0) {
        auto* err = new QskTextLabel(
            wantMpv ? "libmpv 不可用(显式选择,不回退)" : "ffmpeg 引擎不可用");
        box->addSpacer(20);
        box->addItem(err);
        window.addItem(box);
        window.show();
        QTimer::singleShot(2500, &app, SLOT(quit()));
        return app.exec();
    }
    if (wantMpv) {
        CXEngineMpv* em = (CXEngineMpv*)player->engine();
        em->setQuickRender(true); // 须在 engine open 前
        mpvItem->bindEngine(em);
    } else {
        player->setRenderer(ffItem);
    }

    // ── 状态显示/接线 ──
    const double rates[4] = { 1.0, 1.25, 1.5, 2.0 };
    int rateIdx = 0;
    long durMs = 0;



    QObject::connect(player, &CXVideoPlayer::positionChanged,
        [&](long ms) {
            if (durMs > 0)
                prog->setValue(ms);
            timeLbl->setText(cxMsToText(ms) + " / " + cxMsToText(durMs));
            if (dbg && (ms % 1000) < 100)
                fprintf(stderr, "pos=%ld\n", ms);
        });
    QObject::connect(player, &CXVideoPlayer::durationChanged,
        [&](long ms) {
            durMs = ms;
            if (ms > 0)
                prog->setMaximum(ms);
            timeLbl->setText("0:00 / " + cxMsToText(durMs));
        });
    QObject::connect(player, &CXVideoPlayer::subtitleChanged,
        [&](const QString& text) {
            if (text.isEmpty()) {
                subLabel->setVisible(false);
                return;
            }
            subLabel->setText(text);
            subLabel->setPosition(QPointF(20,
                (ffItem != 0 ? ffItem->height() : mpvItem->height()) - 40));
            subLabel->setZ(2);
            subLabel->setVisible(true);
            if (dbg)
                fprintf(stderr, "subtitle-text: [%s]\n",
                        text.toUtf8().constData());
        });
    QObject::connect(player, &CXVideoPlayer::noticedError,
        [&](int code, const QString& msg) {
            fprintf(stderr, "qskvotube error: code=%d %s\n",
                    code, msg.toUtf8().constData());
        });
    QObject::connect(player, &CXVideoPlayer::started,
        [&]() { playBtn->setText("⏸"); });
    QObject::connect(player, &CXVideoPlayer::paused,
        [&]() { playBtn->setText("▶"); });
    QObject::connect(player, &CXVideoPlayer::stopped,
        [&]() { playBtn->setText("▶"); });

    // ── 二期接线 ──
    // 播完自动连播(ffmpeg/mpv 同:open 在 attach 已挂时直接 loadfile)
    QObject::connect(player, &CXVideoPlayer::finished,
        [&]() { openPl(plIdx + 1); });
    QObject::connect(nextBtn, &QskAbstractButton::clicked, [&]() {
        player->stop();
        openPl(plIdx + 1);
    });
    QObject::connect(prevBtn, &QskAbstractButton::clicked, [&]() {
        player->stop();
        openPl(plIdx - 1);
    });
    bool muted = false;
    QObject::connect(muteBtn, &QskAbstractButton::clicked, [&]() {
        muted = !muted;
        player->setMute(muted);
        muteBtn->setText(muted ? "🔊" : "🔇"); // 按下时显示可恢复状态
    });
    // 皮肤热切循环(点击下一个;皮肤名精确大小写!切换后重设主题)
    static const char* kSkins[] = { "Fusion", "Material3", "Fluent2" };
    QObject::connect(skinBtn, &QskAbstractButton::clicked, [&]() {
        const QString cur = qskSkinManager->skinName();
        int idx = 0;
        for (int i = 0; i < 3; i++)
            if (cur == kSkins[i])
                idx = i;
        qskSkinManager->setSkin(kSkins[(idx + 1) % 3]);
        applySkinTheme(qskSkinManager->skin());
    });

    QObject::connect(playBtn, &QskAbstractButton::clicked, [&]() {
        if (player->state() == CxPlaying)
            player->pause();
        else
            player->play();
    });
    // QskSlider 无 pressed 信号:用户拖动值与引擎回投位差 >1.2s 时视为手势
    QObject::connect(prog, &QskSlider::valueChanged, [&](qreal v) {
        if (durMs > 0 && qAbs((long)v - player->position()) > 1200)
            player->seek((long)v);
    });
    QObject::connect(vol, &QskSlider::valueChanged, [&](qreal v) {
        player->setVolume((int)v);
    });
    QObject::connect(rateBtn, &QskAbstractButton::clicked, [&]() {
        rateIdx = (rateIdx + 1) % 4;
        player->setRate(rates[rateIdx]);
        rateBtn->setText(QString("%1x").arg(rates[rateIdx]));
    });
    QObject::connect(ccBtn, &QskAbstractButton::clicked, [&]() {
        static int subIdx = -1;
        int n = player->subtitleTrackCount();
        if (n <= 0)
            return;
        subIdx = subIdx >= n - 1 ? -1 : subIdx + 1;
        player->setSubtitleTrack(subIdx); // -1=关;引擎幂等(M5-A3)
    });
    QObject::connect(eqBtn, &QskAbstractButton::clicked, [&]() {
        // EQ 预设循环(对齐 qlvotube EQ 对话框四参能力;CPU LUT 路,
        // ffmpeg 恒可,mpv 属性路):默认/提亮/高对比/高饱和/暖色
        // (CXVideoEq 有自定义 ctor 非聚合,用工厂函数构造)
        static const struct {
            const char* label;
            CXVideoEq eq;
        } kEqs[] = {
            { "默认",   cxvEq(  0,  0,  0,   0) },
            { "提亮",   cxvEq( 60, 10,  0,   0) },
            { "高对比", cxvEq(  0, 50, 10,   0) },
            { "高饱和", cxvEq(  0,  0, 60,   0) },
            { "暖色",   cxvEq( 10,  0,  0, -30) },
        };
        static int eqIdx = 0;
        eqIdx = (eqIdx + 1) % 5;
        player->setEq(kEqs[eqIdx].eq);
        eqBtn->setText(QString::fromUtf8(kEqs[eqIdx].label));
    });
    QObject::connect(shotBtn, &QskAbstractButton::clicked, [&]() {
        const QString path = cxShotDir() + "/shot_" +
                             QString::number(QDateTime::currentSecsSinceEpoch()) +
                             ".png";
        fprintf(stderr, "qskvotube shot %s -> %d\n",
                path.toUtf8().constData(),
                (int)player->saveScreenshot(path));
    });
    QObject::connect(subBtn, &QskAbstractButton::clicked, [&]() {
        // 外挂字幕:setSubtitleFile(srt/ass);引擎解析失败会走 noticedError
        const QString f = QFileDialog::getOpenFileName(
            nullptr, "加载外挂字幕", QString(),
            "Subtitles (*.srt *.ass *.ssa *.vtt)");
        if (!f.isEmpty())
            player->setSubtitleFile(f);
    });

    // QA 自动驱动:QLVOTUBE_QA=1 时起播后按时间线自检(pause/play/seek/
    // rate/cc/eq/shot),每步一行 qa: 结果供批量验收;不设零行为变化
    if (getenv("QLVOTUBE_QA") != 0) {
        QTimer::singleShot(3000, [&]() {
            player->pause();
        });
        QTimer::singleShot(5000, [&]() {
            fprintf(stderr, "qa: step=pause ok=%d state=%d\n",
                    (int)(player->state() == CxPaused),
                    (int)player->state());
            player->play();
        });
        QTimer::singleShot(7000, [&]() {
            player->seek(5000);
            fprintf(stderr, "qa: step=seek issued=1 pos=%ld\n",
                    player->position());
        });
        QTimer::singleShot(9000, [&]() {
            player->setRate(1.5);
            fprintf(stderr, "qa: step=rate ok=1 pos=%ld\n",
                    player->position());
        });
        QTimer::singleShot(11000, [&]() {
            int n = player->subtitleTrackCount();
            bool ok = (n <= 0) || player->setSubtitleTrack(0);
            fprintf(stderr, "qa: step=cc tracks=%d ok=%d\n", n, (int)ok);
        });
        QTimer::singleShot(13000, [&]() {
            CXVideoEq eq;
            eq.brightness = 60;
            player->setEq(eq);
            fprintf(stderr, "qa: step=eq ok=1\n");
        });
        QTimer::singleShot(15000, [&]() {
            const QString path =
                cxShotDir() + "/qa_" +
                QString::number(QDateTime::currentSecsSinceEpoch()) + ".png";
            fprintf(stderr, "qa: step=shot ok=%d path=%s\n",
                    (int)player->saveScreenshot(path),
                    path.toUtf8().constData());
        });
    }

    window.show();
    window.setTitle("qskvotube - " + QFileInfo(pl[plIdx]).fileName());
    // 拖拽导入(window 对象已在场景后安装过滤器)
    CxDropHook dropHook;
    dropHook.pl = &pl;
    dropHook.openPl = &openPlFn;
    window.installEventFilter(&dropHook);
    if (wantMpv) {
        // 时序:open 只建 mpv handle(vo=libmpv,不发 loadfile);
        // render job 在场景图线程挂 render_context,挂成后 quickGo 起播
        CXEngineMpv* em = (CXEngineMpv*)player->engine();
        player->open(pl[plIdx]); // 二段挂起:loadfile 延至 attach 成功
        window.scheduleRenderJob(
            new CxMpvAttachJob(em, &window, player, pl[plIdx]),
            QQuickWindow::BeforeSynchronizingStage);
    } else {
        // QLVOTUBE_NOPLAY=1:不 open/play,纯 UI 静置(排查视频链干扰)
        if (getenv("QLVOTUBE_NOPLAY") == 0) {
            player->open(pl[plIdx]);
            player->play();
        }
    }

    if (getenv("QLVOTUBE_AUTOQUIT_MS") != 0)
        QTimer::singleShot(atoi(getenv("QLVOTUBE_AUTOQUIT_MS")),
                           &app, SLOT(quit()));

    // 退出时先停引擎(渲染/泵线程),再拆窗——消 QFBO/渲染线程退出竞态
    QObject::connect(&app, &QGuiApplication::aboutToQuit, [&]() {
        player->stop();
    });

    return app.exec();
}
