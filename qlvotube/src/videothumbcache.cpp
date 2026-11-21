#include "videothumbcache.h"
#include "filecache.h"
#include "config.h"

#include "providers/common/compat.h"
#include <atomic>
#include <qapplication.h>
#include <qmap.h>
#include <qstringlist.h>
#if QT_VERSION >= 0x040000
#include <QDebug>
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if QT_VERSION >= 0x040000
#include <QImageReader>
#endif

/* stb_image：Qt 缺 imageformats 插件时的外部解码兜底。
 * 实现体在 vendor/uc_apng_loader/stb_image.c，此处仅声明。 */
#include "stb_image.h"

namespace {

QMap<QString, QPixmap>& cache() { static QMap<QString, QPixmap> m; return m; }
QMap<QString, int>& state() { static QMap<QString, int> m; return m; }
QStringList& lru() { static QStringList l; return l; }
int g_requested = 0;
int g_failed = 0;
const int kMaxThumbs = 300;
/* 图片磁盘缓存上限(GUI 线程刷新,泵线程只读) */
std::atomic<long long> g_imageLimit(500LL * 1024 * 1024);

void thumbLog(const QString& s)
{
    if (!getenv("QLVOTUBE_DEBUG")) return;
#if QT_VERSION >= 0x060000
    qDebug().noquote() << s;
#else
    qDebug("%s", qToUtf8(s).data());
#endif
}

/* 魔数识别格式（按内容，不看扩展名）——移植 qlmusic extensionForData 口径 */
static const char* sniffFormat(const uchar* p, int n)
{
    if (n >= 3 && p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF) return "JPEG";
    if (n >= 8 && p[0] == 0x89 && ::memcmp(p + 1, "PNG", 3) == 0) return "PNG";
    if (n >= 12 && ::memcmp(p, "RIFF", 4) == 0 && ::memcmp(p + 8, "WEBP", 4) == 0) return "WEBP";
    if (n >= 4 && ::memcmp(p, "GIF8", 4) == 0) return "GIF";
    if (n >= 2 && p[0] == 'B' && p[1] == 'M') return "BMP";
    return "";
}

static QString headHex(const uchar* p, int n)
{
    const int m = n < 8 ? n : 8;
    QString h;
    for (int i = 0; i < m; ++i) {
        QString b = QString::number((int)p[i], 16);
        if (b.length() < 2) b = QChar('0') + b;
        h += b;
    }
    return h;
}

/* 启动期格式能力日志（Qt4+ QImageReader / Qt3 inputFormatList）*/
static void logImageCaps()
{
    static bool done = false;
    if (done || !getenv("QLVOTUBE_DEBUG")) return;
    done = true;
#if QT_VERSION >= 0x040000
    const QList<QByteArray> fmts = QImageReader::supportedImageFormats();
    QStringList names;
    for (int i = 0; i < fmts.size(); ++i)
        names.append(QString::fromLatin1(fmts.at(i).constData()));
    thumbLog(qFromUtf8("[thumb] decodable formats: ") + names.join(qFromUtf8(",")));
#else
    thumbLog(qFromUtf8("[thumb] decodable formats: ")
             + QImage::inputFormatList().join(qFromUtf8(",")));
#endif
}

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

/* 从内存字节解码(原 onThumbDone 内联逻辑抽出,供磁盘命中复用) */
QImage decodeImageBytes(const uchar* p, int n, const char** usedOut)
{
    const char* fmt = sniffFormat(p, n);
    QImage img;
    bool ok = false;
    const char* used = "-";
#ifdef QT3_BUILD
    if (*fmt && img.loadFromData(p, (uint)n, fmt) && !img.isNull()) { ok = true; used = "qt-hint"; }
    if (!ok) { img = QImage(); if (img.loadFromData(p, (uint)n) && !img.isNull()) { ok = true; used = "qt-auto"; } }
#else
    if (*fmt) { img = QImage::fromData(p, n, QByteArray(fmt)); if (!img.isNull()) { ok = true; used = "qt-hint"; } }
    if (!ok) { img = QImage::fromData(p, n); if (!img.isNull()) { ok = true; used = "qt-auto"; } }
#endif
    if (!ok) {
        int w = 0, h = 0, ch = 0;
        stbi_uc* raw = stbi_load_from_memory(p, n, &w, &h, &ch, 4);
        if (raw && w > 0 && h > 0) {
#ifdef QT3_BUILD
            QImage tmp(w, h, 32);
#else
            QImage tmp(w, h, QImage::Format_ARGB32);
#endif
            for (int y = 0; y < h; ++y) {
                uchar* dst = tmp.scanLine(y);
                const stbi_uc* src = raw + (size_t)y * w * 4;
                for (int x = 0; x < w; ++x) {
                    dst[x*4+0] = src[x*4+2];
                    dst[x*4+1] = src[x*4+1];
                    dst[x*4+2] = src[x*4+0];
                    dst[x*4+3] = src[x*4+3];
                }
            }
            stbi_image_free(raw);
            img = tmp;
            ok = !img.isNull();
            used = ok ? "stb" : "-";
        }
        if (!raw && getenv("QLVOTUBE_DEBUG"))
            thumbLog(qFromUtf8("[thumb] stb fail ")
                     + qFromUtf8(stbi_failure_reason() ? stbi_failure_reason() : "?"));
    }
    if (usedOut) *usedOut = used;
    return img;
}

void onThumbDone(const HttpResponse& resp, void* udata)
{
    ThumbCtx* ctx = static_cast<ThumbCtx*>(udata);
    QImage img;
    if (resp.httpCode == 200 && !resp.body.empty()) {
        const uchar* p = reinterpret_cast<const uchar*>(resp.body.data());
        const int n = static_cast<int>(resp.body.size());
        const char* used = "-";
        img = decodeImageBytes(p, n, &used);
        if (!img.isNull()) {
            /* 落盘:原始字节 → covers/<key>;每 50 次异步 trim */
            const QByteArray rawBytes = qbaFromData((const char*)p, n);
            filecache::writeFile(filecache::coversDir(),
                                 filecache::keyForUrl(ctx->url), rawBytes);
            static std::atomic<int> s_writes(0);
            if (++s_writes % 50 == 0)
                filecache::requestTrim(filecache::coversDir(),
                                       thumbcache::imageLimitBytes());
        }
        const char* fmt = sniffFormat(p, n);
        thumbLog(QString(qFromUtf8("thumb: decode http=%1 size=%2 head=%3 fmt=%4 path=%5 %6"))
                 .arg(resp.httpCode).arg(n).arg(headHex(p, n))
                 .arg(qFromUtf8(fmt)).arg(qFromUtf8(used)).arg(ctx ? ctx->url : QString()));
    } else {
        thumbLog(QString(qFromUtf8("thumb: no-body http=%1 curl=%2 %3"))
                 .arg(resp.httpCode).arg(qFromUtf8(resp.curlErrStr.c_str()))
                 .arg(ctx ? ctx->url : QString()));
    }
    if (ctx && ctx->target) {
        QApplication::postEvent(ctx->target, new ThumbReadyEvent(ctx->url, img));
    }
    delete ctx;
}

} // namespace

namespace thumbcache {

void refreshImageLimit()
{
    int mb = Config::value(qFromUtf8("cache.image_max_mb")).toInt();
    if (mb <= 0) mb = 500;
    g_imageLimit.store((long long)mb * 1024 * 1024);
}

long long imageLimitBytes() { return g_imageLimit.load(); }

void request(const QString& urlRaw, QWidget* target)
{
    const QString url = normUrl(urlRaw);
    if (url.isEmpty() || !target) return;
    logImageCaps();
    if (cache().contains(url) || state().contains(url)) return;

    /* 磁盘命中:解码后回投,不发网络;损坏则删掉再走网络 */
    {
        const QString key = filecache::keyForUrl(url);
        QByteArray raw = filecache::readFile(filecache::coversDir(), key);
        if (!raw.isEmpty()) {
            const char* used = "-";
            QImage dimg = decodeImageBytes(
                reinterpret_cast<const uchar*>(raw.data()), raw.size(), &used);
            if (!dimg.isNull()) {
                QApplication::postEvent(target, new ThumbReadyEvent(url, dimg));
                return;
            }
            filecache::removeFile(filecache::coversDir(), key);
        }
    }

    HttpRequest req;
    req.url = qToUtf8(url).data();
    req.timeoutSec = 20;
    req.followRedirects = true;
    req.extraHeaders["User-Agent"] =
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36 Edg/124.0.0.0";
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
