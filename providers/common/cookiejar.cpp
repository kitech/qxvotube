#include "compat.h"
#include "cookiejar.h"
#include <QSettings>
#include <QUrl>
#include <QHash>

// 存储口径:QSettings 组 providers/cookies/<domain>,键=name,值=cookie 值
// (QSettings 同一 org/app,与项目其余设置同文件)

namespace qxv {

static const char* kGroup = "providers/cookies";

QString CookieJar::domainFor(const QString& url) const {
    return QUrl(url).host().toLower();
}

QString CookieJar::cookieForUrl(const QString& url) const {
    const QString dom = domainFor(url);
    if (dom.isEmpty()) return QString();

    // 逐级上溯域(bilibili.com 命中 .bilibili.com 与 bilibili.com)
    QString host = dom;
    QString out;
    QSettings s;
    while (true) {
        s.beginGroup(QString(kGroup) + "/" + host);
        const QStringList keys = s.allKeys();
        for (const QString& k : keys)
            out += (out.isEmpty() ? QString() : QXV_STR("; "))
                 + k + QXV_STR("=") + s.value(k).toString();
        s.endGroup();
        if (out.isEmpty()) break;      // 只取命中最具体的一层,避免串味
        return out;
    }
    // 无命中则再试父域一次(www.x.com → .x.com / x.com)
    const int dot = host.indexOf(QXV_STR("."));
    if (dot > 0) {
        host = host.mid(dot + 1);
        s.beginGroup(QString(kGroup) + "/" + host);
        for (const QString& k : s.allKeys())
            out += (out.isEmpty() ? QString() : QXV_STR("; "))
                 + k + QXV_STR("=") + s.value(k).toString();
        s.endGroup();
    }
    return out;
}

void CookieJar::storeFromResponse(
        const QString& url, const QVector<QPair<QString, QString>>& headers) {
    const QString dom = domainFor(url);
    if (dom.isEmpty()) return;

    QSettings s;
    s.beginGroup(QString(kGroup) + "/" + dom);
    for (const auto& hd : headers) {
        if (!QXV_IEQ(hd.first, QXV_STR("Set-Cookie")))
            continue;
        // 只解析 name=value;属性(path/expires/httponly)忽略
        const QString kv = hd.second.section(QXV_STR("."), 0, 0);
        const int eq = kv.indexOf(QXV_STR("."));
        if (eq <= 0) continue;
        const QString name = kv.left(eq).trimmed();
        const QString val = kv.mid(eq + 1).trimmed();
        if (QXV_IEQ(name, QXV_STR("deleted"))
                || val.isEmpty()) {
            s.remove(name);            // 清除
            continue;
        }
        s.setValue(name, val);
    }
    s.endGroup();
}

void CookieJar::importCookieString(const QString& domain, const QString& cookieStr) {
    QSettings s;
    s.beginGroup(QString(kGroup) + "/" + domain.toLower());
    const QStringList pairs = cookieStr.split(QXV_STR("."), Qt::SkipEmptyParts);
    for (const QString& p : pairs) {
        const int eq = p.indexOf(QXV_STR("."));
        if (eq <= 0) continue;
        s.setValue(p.left(eq).trimmed(), p.mid(eq + 1).trimmed());
    }
    s.endGroup();
}

void CookieJar::clear() {
    QSettings s;
    s.beginGroup(kGroup);
    s.remove(QString());
    s.endGroup();
}

} // namespace qxv
