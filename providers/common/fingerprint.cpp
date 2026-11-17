#include "compat.h"
#include "fingerprint.h"
#include <qdatetime.h>

namespace qxv {

static QString chromeVer() {
    // 简易滚动:2026 年 Chrome 大版本 ~150+;按年推
    const int year = QDate::currentDate().year();
    return QString::number(110 + (year - 2023) * 12);
}

static NetHeaders webHeaders(const QString& ua,
                                                   const QString& lang,
                                                   const QString& brand,
                                                   const QString& platform) {
    const QString v = chromeVer();
    NetHeaders h;
    h.append(qMakePair(qFromUtf8("User-Agent"), ua));
    h.append(qMakePair(qFromUtf8("Accept"),
        qFromUtf8("text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8")));
    h.append(qMakePair(qFromUtf8("Accept-Language"), lang));
    h.append(qMakePair(qFromUtf8("Accept-Encoding"), qFromUtf8("gzip, deflate")));
    h.append(qMakePair(qFromUtf8("sec-ch-ua"),
        QString::fromUtf8("\"%1\";v=\"%2\", \"Chromium\";v=\"%2\", \"Not=A?Brand\";v=\"24\"")
            .arg(brand, v)));
    h.append(qMakePair(qFromUtf8("sec-ch-ua-mobile"),
        qFromUtf8("?%1").arg(platform == qFromUtf8("mobile") ? 1 : 0)));
    h.append(qMakePair(qFromUtf8("sec-ch-ua-platform"),
        platform == qFromUtf8("mobile") ? qFromUtf8("\"Android\"")
                                        : qFromUtf8("\"Windows\"")));
    h.append(qMakePair(qFromUtf8("Sec-Fetch-Dest"), qFromUtf8("document")));
    h.append(qMakePair(qFromUtf8("Sec-Fetch-Mode"), qFromUtf8("navigate")));
    h.append(qMakePair(qFromUtf8("Sec-Fetch-Site"), qFromUtf8("none")));
    h.append(qMakePair(qFromUtf8("Upgrade-Insecure-Requests"), qFromUtf8("1")));
    return h;
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
    f.headers.append(qMakePair(qFromUtf8("User-Agent"), userAgent));
    f.headers.append(qMakePair(qFromUtf8("Accept-Encoding"),
                               qFromUtf8("gzip, deflate")));
    return f;
}

} // namespace qxv
