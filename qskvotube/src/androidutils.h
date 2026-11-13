#ifndef ANDROID_UTILS_H
#define ANDROID_UTILS_H

#include <QString>

void showAndroidToast(const QString& message);
void installLogHandler();
// 屏幕常亮（Android: FLAG_KEEP_SCREEN_ON；桌面空操作）——anystik 同源
void setKeepScreenOn(bool on);

#endif
