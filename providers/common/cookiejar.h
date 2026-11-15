#pragma once
#include <QString>
#include <QVector>
#include <QPair>

// 自管 CookieJar:Set-Cookie 解析 + 按域持久化(QSettings)
// 不依赖 QNetworkCookieJar(低版 Qt 无/行为不一)
namespace qxv {

class CookieJar {
public:
    // 取该 URL 应携带的 Cookie 头值(无则空串)
    QString cookieForUrl(const QString& url) const;
    // 从响应头回存 Set-Cookie
    void storeFromResponse(const QString& url,
                           const QVector<QPair<QString, QString>>& headers);
    // 手动导入(认证:用户粘贴整段 cookie 串)
    void importCookieString(const QString& domain, const QString& cookieStr);
    void clear();

private:
    QString domainFor(const QString& url) const;
};

} // namespace qxv
