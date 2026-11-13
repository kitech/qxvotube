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
#include "translator.h"

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

    // ── 多语言(qlcomp Translator,JSON 词表,与 qlvotube 同源)──
    // 构建目录运行 → appdir=build/,appdir/../lang 命中源码 qskvotube/lang/
    {
#ifdef Q_OS_ANDROID
        // Qt Android assets 不支持普通 QFile 路径:首次启动把词表拷到数据目录
        const QString dataLang = QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation) + "/lang";
        QDir().mkpath(dataLang);
        for (const QString& code : { "zh-CN", "zh-TW", "en-US" }) {
            const QString dst = dataLang + "/" + code + ".json";
            if (!QFile::exists(dst))
                QFile::copy("assets:/lang/" + code + ".json", dst);
        }
        Translator::instance().addTranslationPath(dataLang);
#else
        Translator::instance().addTranslationPath(
            app.applicationDirPath() + "/../lang");
        Translator::instance().addTranslationPath(
            app.applicationDirPath() + "/lang");
        Translator::instance().addTranslationPath("lang");
#endif
        // 无 "lang" 键 = 跟随系统(qskmusic 语义);缺省按 QLocale 判定
        const QString langKey = QSettings().value("lang").toString();
        Translator::instance().loadLanguage(
            langKey.isEmpty() ? SettingsPage::cxDefaultLang() : langKey);
    }

    for (const auto& a : app.arguments()) {
        if (a == "-v" || a == "--version") {
            printf("%s %s\n", "qskvotube",
                   app.applicationVersion().toUtf8().constData());
            return 0;
        }
    }

    // 无头环境(QPA offscreen/minimal 等)无法加载图形皮肤插件,
    // QskSkinManager 会断言 → 黑名单退出(qskmusic 思路:不写白名单,
    // 以免 macOS "cocoa"/Windows "windows" 等真实平台被误挡)
    {
        const QString plat = QGuiApplication::platformName();
        const bool headless = plat == "offscreen" || plat == "minimal"
            || plat.isEmpty();
        if (headless) {
            fprintf(stderr, "qskvotube: headless platform (%s), exiting\n",
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

    /* 恢复转场选择（anystik 同口径：0=Slide 1=2D 2=3D 3=Perspective） */
    {
        const int t = QSettings().value("transition", 3).toInt();
        QskStackBoxAnimator* a = nullptr;
        if (t == 0) a = new QskStackBoxAnimator1(stackBox);
        else if (t == 1) a = new QskStackBoxAnimator2(stackBox);
        else if (t == 2) a = new QskStackBoxAnimator3(stackBox);
        if (a) stackBox->setAnimator(a);
    }

    auto* pageManager = new PageManager(stackBox);

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
