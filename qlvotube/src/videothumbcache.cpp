#include "videothumbcache.h"

#include "providers/common/compat.h"
#include <qapplication.h>
#include <qmap.h>
#include <qstringlist.h>
#if QT_VERSION >= 0x040000
#include <QDebug>
#endif
#include <cstdio>
#include <cstdlib>

namespace {

QMap<QString, QPixmap>& cache() { static QMap<QString, QPixmap> m; return m; }
QMap<QString, int>& state() { static QMap<QString, int> m; return m; }
QStringList& lru() { static QStringList l; return l; }
int g_requested = 0;
int g_failed = 0;
const int kMaxThumbs = 300;

/* 协议相对封面(//i0.hdslb.com/...)补 https:curl 要求绝对 URL */
QString normUrl(const QString& url)
{
    if (url.startsWith(qFromUtf8("//"))) return qFromUtf8("https:") + url;
    return url;
}

/* 图床防盗链:按封面域名给 Referer(取不到 → 空,不发无谓头) */
QString refererFor(const QString& url)
{
    const int p = qIndexOf(url, qFromUtf8("://"));
    const int slash = qIndexOf(url, qFromUtf8("/"), p + 3);
    const QString host = (p >= 0 && slash > p) ? url.mid(p + 3, slash - p - 3)
                                               : QString();
    if (host.endsWith(qFromUtf8("hdslb.com")))
        return qFromUtf8("https://www.bilibili.com/");
    if (host.contains(qFromUtf8("xiaohongshu"))
        || host.contains(qFromUtf8("xhscdn")))
        return qFromUtf8("https://www.xiaohongshu.com/");
    if (host.endsWith(qFromUtf8("ytimg.com")))
        return qFromUtf8("https://www.youtube.com/");
    if (host.contains(qFromUtf8("byteimg"))
        || host.contains(qFromUtf8("douyinpic"))
        || host.contains(qFromUtf8("bytedance")))
        return qFromUtf8("https://hongguoduanju.com/");
    return QString();
}

QString proxyFromEnv()
{
    const char* px = getenv("QXVOTUBE_PROXY");
    if (!px || !*px) return QString();
    const QString s = qFromUtf8(px);
    if (s.startsWith(qFromUtf8("http://"))
        || s.startsWith(qFromUtf8("socks5://")))
        return s;
    return qFromUtf8("http://") + s;
}

struct ThumbCtx {
    QString url;
    QWidget* target;
};

void onThumbDone(const HttpResponse& resp, void* udata)
{
    ThumbCtx* ctx = static_cast<ThumbCtx*>(udata);
    QImage img;
    if (resp.httpCode == 200 && !resp.body.empty()) {
        const uchar* p = reinterpret_cast<const uchar*>(resp.body.data());
        const int n = static_cast<int>(resp.body.size());
#ifdef QT3_BUILD
        img.loadFromData(p, (uint)n);
#else
        img = QImage::fromData(p, n);
#endif
    }
    if (ctx && ctx->target) {
        QApplication::postEvent(ctx->target, new ThumbReadyEvent(ctx->url, img));
    }
    delete ctx;
}

} // namespace

namespace thumbcache {

void request(const QString& urlRaw, QWidget* target)
{
    const QString url = normUrl(urlRaw);
    if (url.isEmpty() || !target) return;
    if (cache().contains(url) || state().contains(url)) return;

    HttpRequest req;
    req.url = qToUtf8(url).data();
    req.timeoutSec = 20;
    req.followRedirects = true;
    req.extraHeaders["User-Agent"] =
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";
    const QString ref = refererFor(url);
    if (!ref.isEmpty()) req.extraHeaders["Referer"] = qToUtf8(ref).data();
    const QString px = proxyFromEnv();
    if (!px.isEmpty()) req.proxy = qToUtf8(px).data();

    ThumbCtx* ctx = new ThumbCtx;
    ctx->url = url;
    ctx->target = target;
    state()[url] = 1;
    ++g_requested;
    if (getenv("QLVOTUBE_DEBUG") != 0) {
#if QT_VERSION >= 0x060000
        qDebug() << "thumb: GET" << url << "referer" << ref;
#else
        qDebug("thumb: GET %s referer %s", qToUtf8(url).data(),
               qToUtf8(ref).data());
#endif
    }
    EventPoller::addRequest(req, onThumbDone, ctx);
}

QPixmap pixmap(const QString& urlRaw)
{
    const QString url = normUrl(urlRaw);
    if (cache().contains(url)) {
        qstrListRemoveOne(lru(), url);
        lru().prepend(url);
        return cache()[url];
    }
    return QPixmap();
}

int requested() { return g_requested; }
int failed() { return g_failed; }

void accept(const QString& url, const QImage& image)
{
    if (image.isNull()) {
        state()[url] = 2;
        ++g_failed;
        if (getenv("QLVOTUBE_DEBUG") != 0) {
#if QT_VERSION >= 0x060000
            qWarning() << "thumb: FAILED (decode)" << url;
#else
            qWarning("thumb: FAILED (decode) %s", qToUtf8(url).data());
#endif
        }
        return;
    }
#ifdef QT3_BUILD
    QPixmap pm;
    pm.convertFromImage(image);
    cache().insert(url, pm);
#else
    cache().insert(url, QPixmap::fromImage(image));
#endif
    state()[url] = 3;
    qstrListRemoveOne(lru(), url);
    lru().prepend(url);
    while (lru().size() > (uint)kMaxThumbs) {
        const QString victim = lru().last();
        qstrListRemoveOne(lru(), victim);
        cache().remove(victim);
        state().remove(victim);
        if (getenv("QLVOTUBE_DEBUG") != 0) {
#if QT_VERSION >= 0x060000
            qDebug() << "thumb: evicted" << victim;
#else
            qDebug("thumb: evicted %s", qToUtf8(victim).data());
#endif
        }
    }
}

} // namespace thumbcache
