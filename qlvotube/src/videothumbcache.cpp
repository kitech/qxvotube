#include "videothumbcache.h"

#include <qapplication.h>
#include <qmap.h>
#if QT_VERSION >= 0x040000
#include <QDebug>
#endif
#include <cstdio>
#include <cstdlib>

#ifdef QT3_BUILD
/* Qt3:视频源 UI 不启用(providers=0 源),封面缓存退化为空实现;
 * ContentView 自动回落到 identicon 占位。 */
namespace thumbcache {
void request(const QString&, QWidget*) {}
QPixmap pixmap(const QString&) { return QPixmap(); }
int requested() { return 0; }
int failed() { return 0; }
void accept(const QString&, const QImage&) {}
} // namespace thumbcache
#else

namespace {

QMap<QString, QPixmap> g_cache;     // GUI 线程
QMap<QString, int> g_state;         // 1=在途 2=失败(其余=未请求)
int g_requested = 0;
int g_failed = 0;

/* 图床防盗链:按封面域名给 Referer(取不到 → 空,不发无谓头) */
QString refererFor(const QString& url)
{
    const int p = url.indexOf(QString::fromLatin1("://"));
    const int slash = url.indexOf(QLatin1Char('/'), p + 3);
    const QString host = (p >= 0 && slash > p) ? url.mid(p + 3, slash - p - 3)
                                               : QString();
    if (host.endsWith(QString::fromLatin1("hdslb.com")))
        return QString::fromLatin1("https://www.bilibili.com/");
    if (host.contains(QString::fromLatin1("xiaohongshu"))
        || host.contains(QString::fromLatin1("xhscdn")))
        return QString::fromLatin1("https://www.xiaohongshu.com/");
    if (host.endsWith(QString::fromLatin1("ytimg.com")))
        return QString::fromLatin1("https://www.youtube.com/");
    if (host.contains(QString::fromLatin1("byteimg"))
        || host.contains(QString::fromLatin1("douyinpic"))
        || host.contains(QString::fromLatin1("bytedance")))
        return QString::fromLatin1("https://hongguoduanju.com/");
    return QString();
}

QString proxyFromEnv()
{
    const char* px = getenv("QXVOTUBE_PROXY");   // http://host:port
    if (!px || !*px) return QString();
    const QString s = QString::fromLatin1(px);
    if (s.startsWith(QString::fromLatin1("http://"))
        || s.startsWith(QString::fromLatin1("socks5://")))
        return s;
    return QString::fromLatin1("http://") + s;
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
        img = QImage::fromData(reinterpret_cast<const uchar*>(resp.body.data()),
                              static_cast<int>(resp.body.size()));
    }
    if (ctx && ctx->target) {
        QApplication::postEvent(ctx->target, new ThumbReadyEvent(ctx->url, img));
    } else if (ctx) {
        delete ctx;
        return;
    }
    delete ctx;
}

} // namespace

namespace thumbcache {

void request(const QString& url, QWidget* target)
{
    if (url.isEmpty() || !target) return;
    if (g_cache.contains(url) || g_state.value(url) != 0) return;

    HttpRequest req;
    req.url = url.toUtf8().constData();
    req.timeoutSec = 20;
    req.followRedirects = true;
    req.extraHeaders["User-Agent"] =
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";
    const QString ref = refererFor(url);
    if (!ref.isEmpty()) req.extraHeaders["Referer"] = ref.toUtf8().constData();
    const QString px = proxyFromEnv();
    if (!px.isEmpty()) req.proxy = px.toUtf8().constData();

    ThumbCtx* ctx = new ThumbCtx;
    ctx->url = url;
    ctx->target = target;
    g_state[url] = 1;
    ++g_requested;
    if (getenv("QLVOTUBE_DEBUG") != 0)
#if QT_VERSION >= 0x060000
        qDebug() << "thumb: GET" << url << "referer" << ref;
#else
        qDebug("thumb: GET %s referer %s", qToUtf8(url).data(),
               qToUtf8(ref).data());
#endif
    EventPoller::addRequest(req, onThumbDone, ctx);
}

QPixmap pixmap(const QString& url)
{
    return g_cache.value(url);
}

int requested() { return g_requested; }
int failed() { return g_failed; }

/* 事件处理入口(ContentView::customEvent 调用) */
void accept(const QString& url, const QImage& image)
{
    if (image.isNull()) {
        g_state[url] = 2;
        ++g_failed;
        if (getenv("QLVOTUBE_DEBUG") != 0)
#if QT_VERSION >= 0x060000
            qWarning() << "thumb: FAILED (decode)" << url;
#else
            qWarning("thumb: FAILED (decode) %s", qToUtf8(url).data());
#endif
        return;
    }
#ifdef QT3_BUILD
    QPixmap pm;
    pm.convertFromImage(image);
    g_cache.insert(url, pm);
#else
    g_cache.insert(url, QPixmap::fromImage(image));
#endif
    g_state[url] = 3;
}

} // namespace thumbcache

#endif  // QT3_BUILD(videothumbcache)
