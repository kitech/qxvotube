#include "compat34.h"
#include "mainwindow.h"
#include "translator.h"
#include "appsetup.h"
#include "LimeStyle.h"
#include "ThemeManager.h"
#include "globaluiutil.h"
#include "sharedstatusbar.h"
#include "config.h"
#include "version.h"
#include <stdio.h>
#include <string.h>
#include <qfile.h>
#include <qstringlist.h>
#ifdef __linux__
#include <malloc.h>
#endif

static QString loadSavedLanguage() {
    return Config::value("uilang");
}

void stbarShowStatusMessage(const QString &msg, int timeout)
{
    if (!SharedStatusBar::instanceExists()) { return; }
    SharedStatusBar::instance()->showMessage(msg, timeout);
}

void stbarShowStatusMessage(const QString &msg, SticonIcon type, int timeout)
{
    if (!SharedStatusBar::instanceExists()) { return; }
    SharedStatusBar::instance()->showMessageTyped(msg, (StatusMessageType)type, timeout);
}

#include "app_icon.xpm"

int main(int argc, char* argv[]) {
#ifdef __linux__
    mallopt(M_MMAP_THRESHOLD, 32768);
    mallopt(M_ARENA_MAX, 2);
#endif
    setenv("QT_IM_MODULE", "xim", true);
    setenv("XMODIFIERS", "@im=fcitx", true);

    bool showVersion = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            showVersion = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: qlvotube [options]\n"
                   "Options:\n"
                   "  -v, --version    Show version and exit\n"
                   "  -h, --help       Show this help\n");
            return 0;
        }
    }

    QApplication app(argc, argv);
#ifndef QT3_BUILD
    app.setApplicationVersion(APP_VERSION);
#endif

    /* 二期同步:argv 位置参数=多文件导入(跳过选项;文件须存在) */
    QStringList mediaFiles;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' || argv[i][0] == '\0')
            continue;
        if (QFile::exists(QString::fromLocal8Bit(argv[i])))
            mediaFiles << QString::fromLocal8Bit(argv[i]);
    }

    if (showVersion) {
        printf("qlvotube %s\n", APP_VERSION);
        return 0;
    }

    QtappSetup::setup(app);
    app.setStyle(new LimeStyle);
    ThemeManager::setStyle("qtFusion", true);

    QString savedLang = loadSavedLanguage();
    if (savedLang.isEmpty()) savedLang = "zh-CN";

    Translator::instance().addTranslationPath(app.applicationDirPath() + "/../lang");
    Translator::instance().addTranslationPath(app.applicationDirPath() + "/lang");
    Translator::instance().loadLanguage(savedLang);
    QtappSetup::installQtTranslations(savedLang);

    QtappSetup::setQuitOnExit(false);

    MainWindow window;
    if (!mediaFiles.isEmpty())
        window.setAutoplayFiles(mediaFiles);
    qSetAppIcon(app_icon);
    qSetWindowTitle(&window, _("app_title"));
    window.show();
    qSetAppIcon(app_icon);
    if (!mediaFiles.isEmpty())
        window.startAutoplayFiles();

    QtappSetup::setQuitOnExit(true);

    return app.exec();
}
