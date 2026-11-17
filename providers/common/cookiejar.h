#pragma once
#include "compat.h"

#include <qstring.h>

// 自管 CookieJar:Set-Cookie 解析 + 按域持久化(QSettings)
// 不依赖 QNetworkCookieJar(低版 Qt 无/行为不一)
namespace qxv {

class CookieJar {
public:
    // 取该 URL 应携带的 Cookie 头值(无则空串)
    QString cookieForUrl(const QString& url) const;
    // 从响应头回存 Set-Cookie
    // 注:curl 引擎把同名多值用 '\n' 拼在一条里,这里按行拆分。
    void storeFromResponse(const QString& url, const NetHeaders& headers);
    // 手动导入(认证:用户粘贴整段 cookie 串,形如 "a=1; b=2")
    void importCookieString(const QString& domain, const QString& cookieStr);
    void clear();

private:
    QString domainFor(const QString& url) const;
};

} // namespace qxv
