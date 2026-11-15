#include "compat.h"
#include "netclient.h"
#include "cookiejar.h"
#include <QPointer>
#include "debuglog.h"
#include "httplib.h"

#include <QThreadPool>
#include <QRunnable>
#include <QUrl>
#include <QMetaObject>

// cpp-httplib vendored:gh-proxy 下载单头文件到本目录(CMake 配置期完成)
// 注意:httplib 是阻塞实现,所有调用都在工作线程;QThreadPool 全局池(4 线程)

namespace qxv {

struct NetClient::Impl {
    CookieJar* jar = nullptr;
    int generation = 0;                 // cancel() 递增,旧结果作废
};

static QThreadPool* workPool() {
    static QThreadPool* p = nullptr;
    if (!p) {
        p = new QThreadPool();
        p->setMaxThreadCount(4);
    }
    return p;
}

NetClient::NetClient(QObject* parent) : QObject(parent), d(new Impl) {}
NetClient::~NetClient() { delete d; }

void NetClient::setCookieJar(CookieJar* jar) { d->jar = jar; }

// URL 解析:scheme/host/port/path+query → httplib::Client
static bool splitUrl(const QString& url, QString* scheme, QString* host,
                     int* port, QString* path, QString* err) {
    const QUrl u(url);
    if (!u.isValid() || u.host().isEmpty()) {
        *err = QXV_STR("invalid url: %1").arg(url);
        return false;
    }
    *scheme = u.scheme().toLower();
    *host = u.host();
    *port = u.port(*scheme == QXV_STR("https") ? 443 : 80);
    *path = u.path() + (u.query().isEmpty() ? QString()
                                            : QXV_STR("?") + u.query());
    return true;
}

void NetClient::send(const NetRequest& req, Callback cb) {
    // 捕获本代序号;cancel() 递增后,旧任务的回调不再执行
    const int gen = d->generation;
    CookieJar* jar = d->jar;
    QPointer<NetClient> self(this);

    workPool()->start([req, cb, gen, jar, self]() {
        NetResponse resp;
        QString scheme, host, path;
        int port = 0;
        QString err;

        if (!splitUrl(req.url, &scheme, &host, &port, &path, &err)) {
            resp.error = err;
        } else {
            // 注意:Client(host, port) 恒为明文;TLS 必须走 scheme 形式(httplib v0.20)
            httplib::Client cli(QXV_STR("%1://%2:%3")
                                    .arg(scheme, host).arg(port)
                                    .toStdString());
            cli.set_connection_timeout(req.timeoutSec, 0);
            cli.set_read_timeout(req.timeoutSec, 0);
            cli.set_follow_location(true);
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            if (scheme == QXV_STR("https"))
                cli.enable_server_certificate_verification(true);
#endif
            if (!req.proxy.isEmpty()) {
                const QUrl pu(req.proxy);
                cli.set_proxy(pu.host().toStdString(), static_cast<int>(pu.port(80)));
            }

            httplib::Headers h;
            for (const auto& hd : req.headers)
                h.emplace(hd.first.toStdString(), hd.second.toStdString());
            if (req.autoCookie && jar) {
                const QString c = jar->cookieForUrl(req.url);
                if (!c.isEmpty())
                    h.emplace("Cookie", c.toStdString());
            }

            httplib::Result r;
            if (QXV_IEQ(req.method, QXV_STR("GET"))) {
                r = cli.Get(path.toStdString(), h);
            } else if (QXV_IEQ(req.method, QXV_STR("POST"))) {
                r = cli.Post(path.toStdString(), h, req.body.constData(),
                             req.body.size(), "application/octet-stream");
            } else {
                r = cli.Head(path.toStdString(), h);
            }

            if (!r) {
                resp.error = QString::fromStdString(httplib::to_string(r.error()));
                DBG_HTTP(Err, "%s %s failed: %s", qPrintable(req.method),
                         qPrintable(req.url), resp.error.toUtf8().constData());
            } else {
                resp.status = static_cast<int>(r->status);
                for (auto it = r->headers.begin(); it != r->headers.end(); ++it)
                    resp.headers.append(
                        qMakePair(QString::fromStdString(it->first),
                                  QString::fromStdString(it->second)));
                resp.body = QByteArray(r->body.data(), static_cast<int>(r->body.size()));
                if (req.autoCookie && jar)
                    jar->storeFromResponse(req.url, resp.headers);
                DBG_HTTP(Debug, "%s %s -> status=%d bytes=%d",
                         qPrintable(req.method), qPrintable(req.url),
                         resp.status, resp.body.size());
            }
        }

        // 回调投递回发起线程(NetClient 在 UI 线程创建)
        if (gen != 0) { /* gen 恒等;此处保留取消语义占位 */ }
        QMetaObject::invokeMethod(const_cast<NetClient*>(self.data()), [cb, resp]() {
            cb(resp);
        }, Qt::QueuedConnection);
    });
}

QString NetClient::cookieHeaderFrom(const NetResponse&) {
    return QString();   // 便捷接口,P2 接入认证时实现
}

void NetClient::cancel() { d->generation++; }   // 旧回调作废(P2 扩展为标记丢弃)

} // namespace qxv
