#pragma once
#include "compat.h"
#include <qpair.h>
#include <qstring.h>

// 浏览器/客户端指纹档位:
//  - Web 档:Chrome UA + sec-ch-ua 全套 + Accept-Language(与 locale 一致)
//  - App 档:设备档案(品牌/型号/系统/CDID),档案持久化复用,禁止每请求换设备
namespace qxv {

struct Fingerprint {
    NetHeaders headers;  // 含 UA;不含 Cookie
    QString userAgent;
    QString deviceName;                        // 档案名(日志/持久化键)

    // Chrome Web 档(当前 Chrome 15x 系);lang 如 "zh-CN,en-US"
    static Fingerprint chromeWeb(const QString& langTag);
    // 客户端 App 档(纯 UA,如 bilibili gRPC / YouTube andriod client)
    static Fingerprint appClient(const QString& userAgent);
    // 移动端 web 档(手机 UA)
    static Fingerprint chromeWebMobile(const QString& langTag);
};

} // namespace qxv
