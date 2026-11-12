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

#include "cxvideoplayer.h"
#include "cxengine.h"
#include "eng_mpv.h"
#include "renderer_quick.h"
#include "renderer_qmpv.h"

static QString cxMsToText(long ms)
{
    long s = ms / 1000;
    return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

// mpv Quick 路起播时序:mpv_render_context_create 须在 mpv 起播之前
// (render.h:"needs to be created before you start playback"),且必须
// 在 GL context current 的线程——即场景图渲染线程。用 render job 挂接,
// 挂成后回 GUI 线程 open+play;失败显式报错,不回退(渲染路选谁用谁)。
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
    libPaths.prepend("/opt/qt/qskinny/lib/qskinny/plugins");
    qskSkinManager->setPluginPaths(libPaths);
    qskSkinManager->setSkin(skinEnv != 0 ? skinEnv : "Fusion");
    // anystik 同法:皮肤字体 role 显式设真实存在的字体——皮肤内置
    // 字体(如 Segoe UI)本机缺失时经回退可能整段字形为空
    QskSkin* skin = qskSkinManager->skin();
    // 系统暗色主题(QSkinny 按 styleHints 解析成 DarkScheme)会把浅色
    // 文字画在未填充的白窗底上(qskvotube 无面板件,不像 anystik 用
    // rootBox setPanel(true) 提供暗底)——显式固定亮色配色
    skin->setColorScheme(QskSkin::LightScheme);
    QFont f("DejaVu Sans", 10);
    skin->setFont({QskFontRole::Body, QskFontRole::Normal}, f);
    skin->setFont({QskFontRole::Title, QskFontRole::Normal}, f);
    skin->setFont({QskFontRole::Caption, QskFontRole::Normal}, f);

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
    auto* rateBtn = new QskPushButton("1.0x");
    auto* ccBtn = new QskPushButton("CC");
    auto* eqBtn = new QskPushButton("EQ");
    auto* shotBtn = new QskPushButton("图");

    // QskPushButton 默认 preferred 宽 120,合计把盒子撑出窗口——压窄
    // (anystik 同法:setPreferredSize(44,44),Fusion 下小尺寸文字正常)
    for (QskPushButton* b : { playBtn, rateBtn, ccBtn, eqBtn, shotBtn })
        b->setPreferredSize(44, 36);

    ctl->addItem(playBtn);
    ctl->addItem(prog);
    ctl->setStretchFactor(prog, 1);
    ctl->addItem(timeLbl);
    ctl->addItem(vol);
    ctl->addItem(rateBtn);
    ctl->addItem(ccBtn);
    ctl->addItem(eqBtn);
    ctl->addItem(shotBtn);
    box->addItem(ctl);
    window.addItem(box);

    // ── 引擎接线(setEngine 成功后再配渲染器,与 qlvotube 同序)──
    QString url = getenv("QLVOTUBE_AUTOPLAY") != 0
        ? QString::fromLocal8Bit(getenv("QLVOTUBE_AUTOPLAY"))
        : QString("/tmp/opencode/m5a_long.mp4");
    const bool dbg = getenv("QLVOTUBE_DEBUG") != 0;

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
        // EQ 三键循环:brightness -60→0→+60(CPU LUT 路;ffmpeg 恒可,mpv 属性路)
        static int eqIdx = 0;
        CXVideoEq eq;
        eq.brightness = (eqIdx == 1) ? 60 : (eqIdx == 2) ? -60 : 0;
        eqIdx = (eqIdx + 1) % 3;
        player->setEq(eq);
    });
    QObject::connect(shotBtn, &QskAbstractButton::clicked, [&]() {
        QDir().mkpath(QStandardPaths::writableLocation(
            QStandardPaths::CacheLocation));
        const QString path = QStandardPaths::writableLocation(
                                 QStandardPaths::CacheLocation) +
                             "/shot_" +
                             QString::number(QDateTime::currentSecsSinceEpoch()) +
                             ".png";
        fprintf(stderr, "qskvotube shot %s -> %d\n",
                path.toUtf8().constData(),
                (int)player->saveScreenshot(path));
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
            QDir().mkpath(QStandardPaths::writableLocation(
                QStandardPaths::CacheLocation));
            const QString path =
                QStandardPaths::writableLocation(
                    QStandardPaths::CacheLocation) +
                "/qa_" +
                QString::number(QDateTime::currentSecsSinceEpoch()) + ".png";
            fprintf(stderr, "qa: step=shot ok=%d path=%s\n",
                    (int)player->saveScreenshot(path),
                    path.toUtf8().constData());
        });
    }

    window.show();
    if (wantMpv) {
        // 时序:open 只建 mpv handle(vo=libmpv,不发 loadfile);
        // render job 在场景图线程挂 render_context,挂成后 quickGo 起播
        CXEngineMpv* em = (CXEngineMpv*)player->engine();
        player->open(url); // 二段挂起:loadfile 延至 attach 成功
        window.scheduleRenderJob(
            new CxMpvAttachJob(em, &window, player, url),
            QQuickWindow::BeforeSynchronizingStage);
    } else {
        // QLVOTUBE_NOPLAY=1:不 open/play,纯 UI 静置(排查视频链干扰)
        if (getenv("QLVOTUBE_NOPLAY") == 0) {
            player->open(url);
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
