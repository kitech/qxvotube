#include "compat.h"
#include "netclient.h"
#include "cookiejar.h"
#include <QPointer>
#include "debuglog.h"
#include "httplib.h"

#include <QThreadPool>
#include <QRunnable>
#include <QUrl>
#include "ratelimit.h"

#include <QHash>
#include <QMetaObject>
#include <QPointer>
#include <atomic>
#include <chrono>
#include <thread>

// cpp-httplib vendored:gh-proxy 下载单头文件到本目录(CMake 配置期完成)
// 注意:httplib 是阻塞实现,所有调用都在工作线程;QThreadPool 全局池(4 线程)

namespace qxv {

struct NetClient::Impl {
    CookieJar* jar = nullptr;
    std::atomic<int> generation{ 0 };   // cancel() 递增,旧结果作废(跨线程读)
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
NetClient::~NetClient() = default;

void NetClient::setCookieJar(CookieJar* jar) { d->jar = jar; }

// URL 解析:scheme/host/port/path+query → httplib::Client
static bool splitUrl(const QString& url, QString* scheme, QString* host,
                     int* port, QString* path, QString* err) {
    const QUrl u(url);
    if (!u.isValid() || u.host().isEmpty()) {
        *err = qFromUtf8("invalid url: %1").arg(url);
        return false;
    }
    *scheme = u.scheme().toLower();
    *host = u.host();
    *port = u.port(*scheme == qFromUtf8("https") ? 443 : 80);
    *path = u.path() + (u.query().isEmpty() ? QString()
                                            : qFromUtf8("?") + u.query());
    return true;
}

// 单次请求(不含重试);失败/成功都填 resp
static void doOnce(const NetRequest& req, CookieJar* jar, NetResponse* resp) {
    QString scheme, host, path;
    int port = 0;
    QString err;
    if (!splitUrl(req.url, &scheme, &host, &port, &path, &err)) {
        resp->error = err;
        return;
    }
    // 全局限流(按 host 最小间隔;QXVOTUBE_QPS,默认 12)
    RateLimiter::acquire(host);
    // 注意:Client(host, port) 恒为明文;TLS 必须走 scheme 形式(httplib v0.20)
    httplib::Client cli(qFromUtf8("%1://%2:%3").arg(scheme, host).arg(port)
                            .toStdString());
    cli.set_connection_timeout(req.timeoutSec, 0);
    cli.set_read_timeout(req.timeoutSec, 0);
    cli.set_follow_location(true);
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    if (scheme == qFromUtf8("https"))
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
        if (!c.isEmpty()) h.emplace("Cookie", c.toStdString());
    }

    httplib::Result r;
    // httplib 的 content_type 参数是 set_header 替换语义,headers
    // 里已带 Content-Type 时必须传空串,否则会被盖成默认值
    // (红果 fallback-play-info 400 public_fallback_unavailable 实测根因)
    bool hasCt = false;
    for (const auto& hd : req.headers)
        if (QXV_IEQ(hd.first, qFromUtf8("Content-Type"))) hasCt = true;
    const char* ctype = hasCt ? "" : "application/octet-stream";
    if (QXV_IEQ(req.method, qFromUtf8("GET"))) {
        r = cli.Get(path.toStdString(), h);
    } else if (QXV_IEQ(req.method, qFromUtf8("POST"))) {
        r = cli.Post(path.toStdString(), h, req.body.constData(),
                     req.body.size(), ctype);
    } else {
        r = cli.Head(path.toStdString(), h);
    }

    if (!r) {
        resp->error = QString::fromStdString(httplib::to_string(r.error()));
        DBG_HTTP(Err, "%s %s failed: %s", qPrintable(req.method),
                 qPrintable(req.url), resp->error.toUtf8().constData());
        return;
    }
    resp->status = static_cast<int>(r->status);
    for (auto it = r->headers.begin(); it != r->headers.end(); ++it)
        resp->headers.append(qMakePair(QString::fromStdString(it->first),
                                       QString::fromStdString(it->second)));
    resp->body = QByteArray(r->body.data(), static_cast<int>(r->body.size()));
    if (req.autoCookie && jar) jar->storeFromResponse(req.url, resp->headers);
    DBG_HTTP(Debug, "%s %s -> status=%d bytes=%d", qPrintable(req.method),
             qPrintable(req.url), resp->status, resp->body.size());
}

// 可重试判定:传输错误 / 429 / 5xx / (可选)200 空包
static bool retryableResp(const NetRequest& req, const NetResponse& r) {
    if (!r.error.isEmpty()) return true;
    if (r.status == 429) return true;
    if (r.status >= 500) return true;
    if (req.retryOnEmpty && r.status >= 200 && r.status < 300 && r.body.isEmpty())
        return true;
    return false;
}

// ── 单飞(请求去重)注册表 ─────────────────────────────────────────────
// 同 method+url+body+jar 的在途请求只发一次;等待者各自带 generation,
// cancel 只丢自己的回调,不影响其它等待者。
namespace {

struct Waiter {
    QPointer<NetClient> self;
    NetClient::Callback cb;
    int gen = 0;
    std::shared_ptr<NetClient::Impl> impl;
};

std::mutex& flightMu() {
    static std::mutex m;
    return m;
}
QHash<QString, QList<Waiter>>& flightMap() {
    static QHash<QString, QList<Waiter>> m;
    return m;
}

QString flightKey(const NetRequest& req, CookieJar* jar) {
    if (!req.singleFlight) return QString();
    if (!QXV_IEQ(req.method, qFromUtf8("GET"))
        && !QXV_IEQ(req.method, qFromUtf8("POST")))
        return QString();
    QString key = req.method + qFromUtf8(" ") + req.url;
    if (!req.body.isEmpty())
        key += qFromUtf8("#") + QString::number(qHash(req.body));
    key += qFromUtf8("@") + QString::number(reinterpret_cast<quintptr>(jar));
    return key;
}

// 投递(先校验该等待者的 generation;过期即丢弃)
void deliver(const Waiter& w, const NetResponse& resp) {
    if (!w.self) return;
    if (w.impl->generation.load() != w.gen) return;
    QMetaObject::invokeMethod(w.self.data(), [w, resp]() {
        if (w.impl->generation.load() != w.gen) return;
        w.cb(resp);
    }, Qt::QueuedConnection);
}

} // namespace

void NetClient::send(const NetRequest& req, Callback cb) {
    // 限流配置只读一次 env(测试可直接调 RateLimiter::configure)
    static const bool cfgOnce = []() {
        const QString q = QXV_ENV("QXVOTUBE_QPS");
        if (!q.isEmpty()) RateLimiter::configure(q.toDouble());
        return true;
    }();
    Q_UNUSED(cfgOnce);

    // 捕获本代序号;cancel() 递增后,旧任务的回调不再执行
    const int gen = d->generation.load();
    std::shared_ptr<Impl> impl = d;
    CookieJar* jar = impl->jar;
    QPointer<NetClient> self(this);
    const QString key = flightKey(req, jar);

    if (!key.isEmpty()) {
        std::lock_guard<std::mutex> lk(flightMu());
        QHash<QString, QList<Waiter>>::iterator it = flightMap().find(key);
        if (it != flightMap().end()) {
            // 已有在途:挂到等待表,不发请求
            it->append(Waiter{ self, cb, gen, impl });
            DBG_HTTP(Debug, "%s %s joined in-flight (waiters=%d)",
                     qPrintable(req.method), qPrintable(req.url),
                     it->size());
            return;
        }
        flightMap().insert(key, QList<Waiter>());   // 占位:本请求成为 leader
    }

    workPool()->start([req, cb, gen, jar, impl, self, key]() {
        NetResponse resp;
        const int maxAttempts = qMax(1, req.maxAttempts);
        for (int attempt = 1;; ++attempt) {
            if (impl->generation.load() != gen) break;   // 取消:仍然要继续扇出
            resp = NetResponse();
            doOnce(req, jar, &resp);
            if (attempt >= maxAttempts || !retryableResp(req, resp)) break;
            // 指数退避,分段 sleep 以便 cancel 及时生效
            int delay = qMin(req.retryDelayMs * (1 << (attempt - 1)), 4000);
            if (delay < 0) delay = 4000;
            for (int slept = 0; slept < delay; slept += 50) {
                if (impl->generation.load() != gen) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            if (impl->generation.load() != gen) break;
            DBG_HTTP(Warn, "%s %s retry %d/%d (status=%d err=%s)",
                     qPrintable(req.method), qPrintable(req.url), attempt + 1,
                     maxAttempts, resp.status, resp.error.toUtf8().constData());
        }

        // 收集等待者(含 leader 自己),然后统一扇出
        QList<Waiter> waiters;
        waiters.append(Waiter{ self, cb, gen, impl });
        if (!key.isEmpty()) {
            std::lock_guard<std::mutex> lk(flightMu());
            QList<Waiter> joined = flightMap().take(key);
            waiters += joined;
        }
        for (int i = 0; i < waiters.size(); ++i) deliver(waiters[i], resp);
    });
}

QString NetClient::cookieHeaderFrom(const NetResponse&) {
    return QString();   // 便捷接口,P2 接入认证时实现
}

void NetClient::cancel() { d->generation.fetch_add(1); }   // 在途/重试/待投递全部作废

} // namespace qxv
