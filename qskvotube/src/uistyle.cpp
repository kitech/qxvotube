#include "uistyle.h"

#include <QGuiApplication>
#include <QSettings>
#include <QskSkinManager.h>
#include <QskSkin.h>
#include <QskFontRole.h>

namespace {

int g_pointSize = 0;   // 0 = 未显式设置,用平台缺省

int platformDefaultPointSize()
{
#ifdef Q_OS_ANDROID
    return 16;
#else
    return 10;
#endif
}

} // namespace

QFont cxBaseFont()
{
    const int pt = g_pointSize > 0 ? g_pointSize : platformDefaultPointSize();
#ifdef Q_OS_ANDROID
    QFont f;
    f.setPointSizeF(pt);
    return f;
#else
    return QFont(QString::fromLatin1("DejaVu Sans"), pt);
#endif
}

int cxBasePointSize()
{
    return g_pointSize > 0 ? g_pointSize : platformDefaultPointSize();
}

void cxSetBasePointSize(int pt)
{
    if (pt <= 0)
        return;
    g_pointSize = pt;
    const QFont f = cxBaseFont();
    QGuiApplication::setFont(f);
    cxApplySkinTheme(qskSkinManager ? qskSkinManager->skin() : 0);
}

bool cxDarkMode()
{
    // 缺省深色(qskmusic 同款;qsktox 缺省为浅色,本项目取深色)
    return QSettings().value("darkMode", true).toBool();
}

void cxSetDarkMode(bool on)
{
    QSettings().setValue("darkMode", on);
    QskSkin* sk = qskSkinManager ? qskSkinManager->skin() : 0;
    if (sk != 0)
        sk->setColorScheme(on ? QskSkin::DarkScheme : QskSkin::LightScheme);
}

void cxApplySkinTheme(QskSkin* sk)
{
    if (sk == 0)
        return;
    // 配色与字体统一入口:配色按持久化 darkMode(缺省深色)应用
    sk->setColorScheme(cxDarkMode() ? QskSkin::DarkScheme
                                    : QskSkin::LightScheme);
    // anystik 同法:皮肤字体 role 显式设真实存在的字体——皮肤内置字体
    // (如 Segoe UI)本机缺失时经回退可能整段字形为空
    const QFont f = cxBaseFont();
    sk->setFont({ QskFontRole::Body, QskFontRole::Normal }, f);
    sk->setFont({ QskFontRole::Title, QskFontRole::Normal }, f);
    sk->setFont({ QskFontRole::Caption, QskFontRole::Normal }, f);
    QGuiApplication::setFont(f);
}
