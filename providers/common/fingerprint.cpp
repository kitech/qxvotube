#include "compat.h"
#include "fingerprint.h"
#include <QDate>

namespace qxv {

static QString chromeVer() {
    // 简易滚动:2026 年 Chrome 大版本 ~150+;按年推
    const int year = QDate::currentDate().year();
    return QString::number(110 + (year - 2023) * 12);
}

static QVector<QPair<QString, QString>> webHeaders(const QString& ua,
                                                   const QString& lang,
                                                   const QString& brand,
                                                   const QString& platform) {
    const QString v = chromeVer();
    return {
        { "User-Agent", ua },
        { "Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8" },
        { "Accept-Language", lang },
        { "Accept-Encoding", "gzip, deflate" },   // 主动不设 br(httplib 不支持)
        { "sec-ch-ua", QString("\"%1\";v=\"%2\", \"Chromium\";v=\"%2\", \"Not=A?Brand\";v=\"24\"").arg(brand, v) },
        { "sec-ch-ua-mobile", qFromUtf8("?%1").arg(platform == qFromUtf8("mobile") ? 1 : 0) },
        { "sec-ch-ua-platform", platform == qFromUtf8("mobile")
                                   ? qFromUtf8("\"Android\"") : qFromUtf8("\"Windows\"") },
        { "Sec-Fetch-Dest", "document" },
        { "Sec-Fetch-Mode", "navigate" },
        { "Sec-Fetch-Site", "none" },
        { "Upgrade-Insecure-Requests", "1" },
    };
}

Fingerprint Fingerprint::chromeWeb(const QString& langTag) {
    const QString v = chromeVer();
    const QString ua = qFromUtf8(
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/%1.0.0.0 Safari/537.36").arg(v);
    Fingerprint f;
    f.userAgent = ua;
    f.deviceName = qFromUtf8("chrome-web-%1").arg(v);
    f.headers = webHeaders(ua, langTag, "Google Chrome", "desktop");
    return f;
}

Fingerprint Fingerprint::chromeWebMobile(const QString& langTag) {
    const QString v = chromeVer();
    const QString ua = qFromUtf8(
        "Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/%1.0.0.0 Mobile Safari/537.36").arg(v);
    Fingerprint f;
    f.userAgent = ua;
    f.deviceName = qFromUtf8("chrome-web-mobile-%1").arg(v);
    f.headers = webHeaders(ua, langTag, "Google Chrome", "mobile");
    return f;
}

Fingerprint Fingerprint::appClient(const QString& userAgent) {
    Fingerprint f;
    f.userAgent = userAgent;
    f.deviceName = qFromUtf8("app-%1").arg(userAgent.left(24));
    f.headers = {
        { "User-Agent", userAgent },
        { "Accept-Encoding", "gzip, deflate" },
    };
    return f;
}

} // namespace qxv
