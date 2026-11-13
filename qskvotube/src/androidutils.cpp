#include "androidutils.h"
#include <QCoreApplication>
#include <QDebug>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <android/log.h>
#include <cstdio>

static void logHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    const char *file = ctx.file ? ctx.file : "unknown";
    int line = ctx.line;
    int prio = ANDROID_LOG_DEBUG;
    switch (type) {
        case QtDebugMsg:    prio = ANDROID_LOG_DEBUG; break;
        case QtInfoMsg:     prio = ANDROID_LOG_INFO; break;
        case QtWarningMsg:  prio = ANDROID_LOG_WARN; break;
        case QtCriticalMsg: prio = ANDROID_LOG_ERROR; break;
        case QtFatalMsg:    prio = ANDROID_LOG_FATAL; break;
    }
    __android_log_print(prio, "qskvotube", "%s:%d %s", file, line, msg.toUtf8().constData());
}
#else
#include <cstdio>

static void logHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    const char *file = ctx.file ? ctx.file : "unknown";
    int line = ctx.line;
    FILE *out = (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
                ? stderr : stdout;
    fprintf(out, "%s:%d %s\n", file, line, msg.toUtf8().constData());
    fflush(out);
    if (type == QtFatalMsg) abort();
}
#endif

void installLogHandler()
{
    qInstallMessageHandler(logHandler);
}

#ifdef Q_OS_ANDROID

void showAndroidToast(const QString& message) {
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([message]() {
        QJniObject context = QNativeInterface::QAndroidApplication::context();
        if (!context.isValid()) return;
        QJniObject jmsg = QJniObject::fromString(message);
        QJniObject toast = QJniObject::callStaticObjectMethod(
            "android/widget/Toast",
            "makeText",
            "(Landroid/content/Context;Ljava/lang/CharSequence;I)Landroid/widget/Toast;",
            context.object(), jmsg.object(), 1);
        if (toast.isValid()) {
            toast.callMethod<void>("show", "()V");
        }
    });
}

#else

void showAndroidToast(const QString&) { }

#endif

void setKeepScreenOn(bool on)
{
#ifdef Q_OS_ANDROID
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([on]() {
        QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (!activity.isValid()) return;
        QJniObject window = activity.callObjectMethod(
            "getWindow", "()Landroid/view/Window;");
        if (!window.isValid()) return;
        constexpr int FLAG_KEEP_SCREEN_ON = 0x80;
        if (on)
            window.callMethod<void>("addFlags", "(I)V", FLAG_KEEP_SCREEN_ON);
        else
            window.callMethod<void>("clearFlags", "(I)V", FLAG_KEEP_SCREEN_ON);
    }).waitForFinished();
#else
    Q_UNUSED(on)
#endif
}
