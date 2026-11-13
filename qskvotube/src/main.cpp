// qskvotube —— QSkinny 播放前端(M5-B;cxvideo 双内核 + Quick 渲染器)
// 引擎显式选:QLVOTUBE_ENGINE=ffmpeg|mpv(缺省 ffmpeg,设置页可持久化;
// mpv 要求窗口 GL API,不满足即报错退出——不自动降级)。
// QA:QLVOTUBE_AUTOPLAY=<url> / QLVOTUBE_AUTOQUIT_MS=<ms> / QLVOTUBE_DEBUG=1
//     QLVOTUBE_QA=1(起播后时间线自检 pause/play/seek/rate/cc/eq/shot)
// 页面:QskStackBox + PageManager(qsktox 机制);主页 ⋮ 菜单:常亮/设置/关于
#include <QGuiApplication>
#include <QTimer>
#include <QSettings>
#include <QElapsedTimer>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QKeyEvent>

#include <QskSkinManager.h>
#include <QskSkin.h>
#include <QskWindow.h>
#include <QskLinearBox.h>
#include <QskStackBox.h>
#include <QskStackBoxAnimator.h>

#include <QQuickWindow>
#include <QSGRendererInterface>

#include "pagemanager.h"
#include "mainpage.h"
#include "settingspage.h"
#include "aboutpage.h"
#include "keepalive.h"
#include "androidutils.h"
#include "uistyle.h"

namespace {

// Android 返回键(qskmusic GlobalKeyFilter 精简):栈深>1 回退页面栈;
// 栈底 2s 内双击才退出(防误退)
class BackButtonFilter : public QObject
{
public:
    explicit BackButtonFilter(PageManager* pm, QObject* parent = nullptr)
        : QObject(parent), m_pm(pm) {}

protected:
    bool eventFilter(QObject*, QEvent* ev) override
    {
        if (ev->type() != QEvent::KeyPress)
            return false;
        auto* ke = static_cast<QKeyEvent*>(ev);
        if (ke->key() != Qt::Key_Back)
            return false;
        ev->accept();
        if (m_pm->depth() > 1) {
            m_pm->back();
        } else if (m_backTimer.isValid() && m_backTimer.elapsed() < 2000) {
            QCoreApplication::quit();
        } else {
            m_backTimer.start();
            showAndroidToast(QString::fromUtf8("再按一次退出"));
        }
        return true;
    }

private:
    PageManager* m_pm;
    QElapsedTimer m_backTimer;
};

// 引擎决策须在首个 QQuickWindow 创建前完成(mpv 需要显式 GL API):
// env 优先,其次设置页持久化值,缺省 ffmpeg
bool wantMpvEngine()
{
    const QByteArray env = qgetenv("QLVOTUBE_ENGINE");
    if (!env.isEmpty())
        return env == QByteArray("mpv");
    return QSettings().value("engine", QString("ffmpeg")).toString()
        == QLatin1String("mpv");
}

} // namespace

int main(int argc, char* argv[])
{
    const bool wantMpv = wantMpvEngine();
    if (wantMpv) {
        // mpv-qfbo 显式前置:窗口必须 GL API(首个 QQuickWindow 之前设置)
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("qskvotube");
    app.setApplicationVersion("0.2.0");
    app.setOrganizationName("qlvotube");
    app.setFont(cxBaseFont());

    for (const auto& a : app.arguments()) {
        if (a == "-v" || a == "--version") {
            printf("%s %s\n", "qskvotube",
                   app.applicationVersion().toUtf8().constData());
            return 0;
        }
    }

    // 无头环境无法加载图形皮肤插件，QskSkinManager 会断言；
    // 仅在可创建平台窗口时才进 UI。Android 平台名即 "android"，必须放行
    // (曾致 APK 启动即闪退: logcat "no GUI platform (android), exiting")
    {
        const QString plat = QGuiApplication::platformName();
        const bool guiCapable = plat.startsWith("xcb")
            || plat.startsWith("wayland") || plat == "android";
        if (!guiCapable) {
            fprintf(stderr, "qskvotube: no GUI platform (%s), exiting\n",
                    plat.toUtf8().constData());
            return 1;
        }
    }

#ifdef Q_OS_ANDROID
    // JNI lib 平铺目录无 skins/ 子目录,QskSkinManager 按 <path>/skins/ 找插件;
    // 拷到 AppLocalDataLocation/skins 再 addLibraryPath(qskmusic/anystik 同法),
    // 否则 setSkin 失败 → skin() 为 null → 后续解引用崩溃
    {
        const QString dataDir = QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation);
        const QStringList libPaths0 = QCoreApplication::libraryPaths();
        for (const QString& libPath : libPaths0) {
            QDir srcDir(libPath);
            if (!srcDir.exists("libqskinny_arm64-v8a.so"))
                continue;
            QDir().mkpath(dataDir + "/skins");
            const char* names[] = { "libfusionskin", "libfluent2skin",
                                    "libmaterial3skin" };
            for (const char* name : names) {
                const QString fileName = QString::fromLatin1(name)
                    + "_arm64-v8a.so";
                const QString dst = dataDir + "/skins/" + fileName;
                if (QFile::exists(dst))
                    continue;
                if (QFile::copy(srcDir.absoluteFilePath(fileName), dst))
                    QFile(dst).setPermissions(QFile::ReadOwner | QFile::ExeOwner
                                             | QFile::ReadUser | QFile::ExeUser);
            }
            break;
        }
        QCoreApplication::addLibraryPath(dataDir);
    }
#endif

    // 皮肤插件路径:桌面显式前置 qskinny 同树插件目录;Android 已由上面提取
    {
        const char* skinEnv = getenv("QLVOTUBE_SKIN");
        QStringList libPaths = QCoreApplication::libraryPaths();
#ifndef Q_OS_ANDROID
        libPaths.prepend("/opt/qt/qskinny/lib/qskinny/plugins");
#endif
        qskSkinManager->setPluginPaths(libPaths);
        qskSkinManager->setSkin(skinEnv != 0 ? skinEnv : "Fusion");
        QskSkin* skin = qskSkinManager->skin();
        if (skin != 0)
            cxApplySkinTheme(skin);
        else
            fprintf(stderr, "qskvotube: skin 加载失败(插件路径?)\n");
    }

    // ── Root:StackBox + PageManager(qsktox 结构;动画器默认 Perspective)──
    auto* rootBox = new QskLinearBox(Qt::Vertical);
    rootBox->setPanel(true);
    auto* stackBox = new QskStackBox(rootBox);
    stackBox->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Expanding);
    stackBox->setAnimator(new QskStackBoxAnimator4(stackBox));
    auto* pageManager = new PageManager(stackBox);

    // 持久化的切换动画(与设置页同键)
    {
        const int idx = QSettings().value("transition", 3).toInt();
        QskStackBoxAnimator* a = nullptr;
        switch (idx) {
            case 0: a = new QskStackBoxAnimator1(stackBox); break;
            case 1: a = new QskStackBoxAnimator2(stackBox); break;
            case 2: a = new QskStackBoxAnimator3(stackBox); break;
            default: break;   // 3 = 默认 Perspective,已设置
        }
        if (a != nullptr)
            stackBox->setAnimator(a);
    }

    pageManager->registerPage("main", []() -> Page* {
        return new MainPage();
    }, { CachePolicy::Permanent, LaunchMode::Standard });
    pageManager->registerPage("settings", []() -> Page* {
        return new SettingsPage();
    }, { CachePolicy::Permanent, LaunchMode::Standard });
    pageManager->registerPage("about", []() -> Page* {
        return new AboutPage();
    }, { CachePolicy::Transient, LaunchMode::Standard });

    QskWindow window;
    window.setTitle("qskvotube");
    window.setPreferredSize({640, 420});
    window.addItem(rootBox);
    window.show();

    pageManager->open("main");
    app.installEventFilter(new BackButtonFilter(pageManager, &app));

#ifdef Q_OS_ANDROID
    KeepAlive::start();   // qsktox 同:启动即无条件保活(通知权限+白名单+服务)
#endif

    // 退出时先停引擎(渲染/泵线程),再拆窗——消 QFBO/渲染线程退出竞态
    QObject::connect(&app, &QGuiApplication::aboutToQuit, [&]() {
        if (MainPage* mp = qobject_cast<MainPage*>(pageManager->findPage("main")))
            mp->stopPlayback();
    });

    return app.exec();
}
