#include "keepalive.h"
#include <QDebug>
#include <QCoreApplication>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

void KeepAlive::start()
{
#ifdef Q_OS_ANDROID
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]() {
        auto ctx = QNativeInterface::QAndroidApplication::context();

        QJniObject::callStaticMethod<void>(
            "io/fedlet/qskvotube/PermissionHelper",
            "requestNotificationPermission",
            "(Landroid/app/Activity;)V",
            ctx.object());

        // 电池白名单一次性引导（dataSync 6h 跨版本解法）：已白名单或
        // 已提示过 → Java 侧直接返回（SharedPreferences 记忆）
        QJniObject::callStaticMethod<void>(
            "io/fedlet/qskvotube/PermissionHelper",
            "requestBatteryWhitelist",
            "(Landroid/app/Activity;)V",
            ctx.object());

        QJniObject::callStaticMethod<void>(
            "io/fedlet/qskvotube/KeepAliveService",
            "startService",
            "(Landroid/content/Context;)V",
            ctx.object());
    });
#endif
}

void KeepAlive::stop()
{
#ifdef Q_OS_ANDROID
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]() {
        auto ctx = QNativeInterface::QAndroidApplication::context();
        QJniObject::callStaticMethod<void>(
            "io/fedlet/qskvotube/KeepAliveService",
            "stopService",
            "(Landroid/content/Context;)V",
            ctx.object());
    });
#endif
}
