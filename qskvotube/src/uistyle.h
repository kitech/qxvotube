#ifndef UISTYLE_H
#define UISTYLE_H

#include <QFont>

class QskSkin;

// 全局字体/皮肤主题助手(MainPage 与 SettingsPage 共用)
// 桌面:DejaVu(Adwaita Sans 会致 QSkinny 文字异常);
// Android:默认族 + 16pt(anystik 同法,CJK 靠系统字体合并)
QFont cxBaseFont();
// 设置页字号:按 pt 覆盖基准字号并立即应用到 app + 当前皮肤 role
void cxSetBasePointSize(int pt);
int  cxBasePointSize();
// 明暗:缺省 dark=true(与 qskmusic 一致)
bool cxDarkMode();
void cxSetDarkMode(bool on);   // 写 QSettings + 立即应用到当前皮肤

// 配色(按 cxDarkMode) + 三个 font role 统一为当前基准字体
void cxApplySkinTheme(QskSkin* sk);

#endif
