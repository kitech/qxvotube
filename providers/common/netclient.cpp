#include "compat.h"
#include "netclient.h"
#include "cookiejar.h"
#include "debuglog.h"
#include "ratelimit.h"
#include "eventpoller.h"        // qlappcom curl multi 引擎

#include <qurl.h>

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// 传输层:EventPoller(curl multi,自带泵线程)。
// 线程契约:
//   泵线程:只把 HttpResponse 打包成事件,post 回 NetClient 所在线程;
//   发起线程:customEvent 里处理 Cookie/重试/单飞扇出/回调。
// 因此下面所有 map/状态只在发起线程读写(flight 表由互斥量保护,跨线程登记)。

namespace qxv {
namespace {

const EventType34 kNetDoneType = toEventType34(QEvent::User + 130);
const EventType34 kNetDeliverType = toEventType34(QEvent::User + 131);

struct Waiter {
    QGuard36<NetClient> self;
    NetClient::Callback cb;
    int gen;
    std::shared_ptr<NetClient::Impl> impl;
};

// 单飞注册表:key -> 等待者(leader 在 index 0)
std::mutex& flightMu() {
    static std::mutex m;
    return m;
}
std::map<QString, std::vector<Waiter> >& flightMap() {
    static std::map<QString, std::vector<Waiter> > m;
    return m;
}

QString hostOf(const QString& url) { return QUrl(url).host(); }

// 单飞 key;singleFlight=false / 非 GET|POST 时返回空(调用方补唯一 key)
QString flightKey(const NetRequest& req, CookieJar* jar) {
    if (!req.singleFlight) return QString();
    if (!qieq(req.method, qFromUtf8("GET"))
        && !qieq(req.method, qFromUtf8("POST")))
        return QString();
    QString key = req.method + qFromUtf8(" ") + req.url;
    if (!req.body.isEmpty()) {          // FNV-1a(Qt3 无 qHash(QByteArray))
        unsigned int h = 2166136261u;
        const char* p = req.body.data();
        for (int i = 0; i < (int)req.body.size(); ++i) {
            h ^= (unsigned char)p[i];
            h *= 16777619u;
        }
        key += qFromUtf8("#") + QString::number((uint)h);
    }
    key += qFromUtf8("@") + QString::number((long long)(uintptr_t)jar);
    return key;
}

// 非单飞请求也要走「等待者表」统一扇出 → 用唯一 key 占位
QString uniqueKey() {
    static std::atomic<long long> seq(0);
    return qFromUtf8("once#%1").arg(++seq);
}

// 请求完成(泵线程) → 投递到发起线程
class NetDoneEvent : public CustomEventBase {
public:
    NetDoneEvent(int g, const NetRequest& r, int a, const QString& k,
                 const HttpResponse& h)
        : CustomEventBase(kNetDoneType), gen(g), req(r), attempt(a),
          key(k), resp(h) {}
    int gen;
    NetRequest req;
    int attempt;
    QString key;
    HttpResponse resp;
};

// 单飞扇出(发起线程 → 各等待者线程)
class NetDeliverEvent : public CustomEventBase {
public:
    NetDeliverEvent(int g, const NetClient::Callback& c, const NetResponse& r)
        : CustomEventBase(kNetDeliverType), gen(g), cb(c), resp(r) {}
    int gen;
    NetClient::Callback cb;
    NetResponse resp;
};

} // namespace

struct NetClient::Impl {
    CookieJar* jar;
    std::atomic<int> gen;
    Impl() : jar(0), gen(0) {}
};

NetClient::NetClient(QObject* parent) : QObject(parent), d(new Impl) {}
NetClient::~NetClient() { d->gen.fetch_add(1); }

void NetClient::setCookieJar(CookieJar* jar) { d->jar = jar; }
void NetClient::cancel() { d->gen.fetch_add(1); }

void NetClient::dispatch(const NetRequest& req, int gen, const QString& key,
                         int attempt) {
    if (!EventPoller::isReady()) EventPoller::start();   // 幂等

    HttpRequest hr;
    hr.url = std::string(qToUtf8(req.url).data());
    hr.method = qieq(req.method, qFromUtf8("POST")) ? "POST" : "GET";
    if (!req.body.isEmpty())
        hr.data = std::string(req.body.data(), (size_t)req.body.size());
    hr.timeoutSec = req.timeoutSec;
    hr.stallSec = req.stallSec;
    hr.followRedirects = true;
    if (!req.proxy.isEmpty()) hr.proxy = std::string(qToUtf8(req.proxy).data());
    for (int i = 0; i < req.headers.size(); ++i)
        hr.extraHeaders[std::string(qToUtf8(req.headers[i].first).data())] =
            std::string(qToUtf8(req.headers[i].second).data());
    if (req.autoCookie && d->jar) {
        const QString c = d->jar->cookieForUrl(req.url);
        if (!c.isEmpty()) hr.extraHeaders["Cookie"] = std::string(qToUtf8(c).data());
    }
    // 限流:非阻塞预约 → 交给引擎的延迟调度,不占线程
    const long long wait = RateLimiter::reserveSlot(hostOf(req.url));
    int delay = (int)(wait > 4000 ? 4000 : wait);
    if (attempt > 1) {   // 重试退避:retryDelayMs * 2^(attempt-2),封顶 4s
        int backoff = req.retryDelayMs * (1 << (attempt - 2));
        if (backoff > 4000) backoff = 4000;
        if (backoff > delay) delay = backoff;
    }
    hr.delayMs = delay;

    struct Ctx {
        QGuard36<NetClient> self;
        NetRequest req;
        int gen;
        int attempt;
        QString key;
    };
    Ctx* ctx = new Ctx;
    ctx->self = this;
    ctx->req = req;
    ctx->gen = gen;
    ctx->attempt = attempt;
    ctx->key = key;

    EventPoller::addRequest(hr, [](const HttpResponse& h, void* ud) {
        Ctx* c = static_cast<Ctx*>(ud);
        if (c->self) {
            qAppPostEvent((NetClient*)c->self,
                          new NetDoneEvent(c->gen, c->req, c->attempt,
                                           c->key, h));
        } else if (!c->key.isEmpty()) {
            // 客户端已销毁:清掉单飞占位,避免后续请求永久挂等
            std::lock_guard<std::mutex> lk(flightMu());
            flightMap().erase(c->key);
        }
        delete c;
    }, ctx);

    DBG_HTTP(Debug, "%s %s (attempt=%d delay=%dms)",
             qToUtf8(req.method).data(), qToUtf8(req.url).data(), attempt, delay);
}

void NetClient::send(const NetRequest& req, Callback cb) {
    // 限流配置只读一次 env(测试可直接调 RateLimiter::configure)
    static const bool cfgOnce = []() {
        const QString q = qenv("QXVOTUBE_QPS");
        if (!q.isEmpty()) RateLimiter::configure(q.toDouble());
        return true;
    }();
    Q_UNUSED(cfgOnce);

    const int gen = d->gen.load();
    QString key = flightKey(req, d->jar);

    Waiter w;
    w.self = this;
    w.cb = cb;
    w.gen = gen;
    w.impl = d;

    if (key.isEmpty()) key = uniqueKey();
    {
        std::lock_guard<std::mutex> lk(flightMu());
        std::vector<Waiter>& list = flightMap()[key];
        list.push_back(w);                 // leader 也是等待者(index 0)
        if (list.size() > 1) {
            DBG_HTTP(Debug, "%s %s joined in-flight (waiters=%d)",
                     qToUtf8(req.method).data(), qToUtf8(req.url).data(),
                     (int)list.size());
            return;                        // 不重复发请求
        }
    }
    dispatch(req, gen, key, 1);
}

void NetClient::customEvent(CustomEventBase* e) {
    if (e->type() == kNetDeliverType) {
        NetDeliverEvent* de = static_cast<NetDeliverEvent*>(e);
        if (de->gen == d->gen.load() && de->cb) de->cb(de->resp);
        return;
    }
    if (e->type() != kNetDoneType) { QObject::customEvent(e); return; }

    NetDoneEvent* ev = static_cast<NetDoneEvent*>(e);

    NetResponse r;
    r.status = ev->resp.httpCode;
    if (!ev->resp.curlErrStr.empty())
        r.error = qFromUtf8(ev->resp.curlErrStr.c_str());
    r.body = qbaFromData(ev->resp.body.data(), (int)ev->resp.body.size());
    for (std::map<std::string, std::string>::const_iterator it =
             ev->resp.headers.begin(); it != ev->resp.headers.end(); ++it)
        r.headers.append(qMakePair(qFromUtf8(it->first.c_str()),
                                   qFromUtf8(it->second.c_str())));
    if (ev->req.autoCookie && d->jar)
        d->jar->storeFromResponse(ev->req.url, r.headers);

    // leader 已取消:自己的回调丢弃,但单飞等待者仍要收到(各自代际校验)
    const bool stale = (ev->gen != d->gen.load());
    if (stale)
        DBG_HTTP(Debug, "leader stale (gen=%d now=%d), fan-out waiters only",
                 ev->gen, d->gen.load());

    const bool retryable = !r.error.isEmpty() || r.status == 429
        || r.status >= 500
        || (ev->req.retryOnEmpty && r.ok() && r.body.isEmpty());
    const int maxAttempts = ev->req.maxAttempts > 1 ? ev->req.maxAttempts : 1;
    if (!stale && retryable && ev->attempt < maxAttempts) {
        DBG_HTTP(Warn, "%s %s retry %d/%d (status=%d err=%s)",
                 qToUtf8(ev->req.method).data(), qToUtf8(ev->req.url).data(),
                 ev->attempt + 1, maxAttempts, r.status, qToUtf8(r.error).data());
        dispatch(ev->req, ev->gen, ev->key, ev->attempt + 1);
        return;
    }

    // 扇出:leader + 单飞等待者(各自代际校验在 NetDeliverEvent 落地时再做一次)
    std::vector<Waiter> waiters;
    if (!ev->key.isEmpty()) {
        std::lock_guard<std::mutex> lk(flightMu());
        std::map<QString, std::vector<Waiter> >::iterator it =
            flightMap().find(ev->key);
        if (it != flightMap().end()) {
            waiters.swap(it->second);
            flightMap().erase(it);
        }
    }
    if (waiters.empty()) return;    // 不会发生:send() 至少登记 leader

    for (size_t i = 0; i < waiters.size(); ++i) {
        const Waiter& w = waiters[i];
        if (!w.self) continue;
        qAppPostEvent((NetClient*)w.self, new NetDeliverEvent(w.gen, w.cb, r));
    }
}

QString NetClient::cookieHeaderFrom(const NetResponse& r) {
    QString out;
    for (int i = 0; i < r.headers.size(); ++i) {
        if (!qieq(r.headers[i].first, qFromUtf8("Set-Cookie"))) continue;
        if (!out.isEmpty()) out += qFromUtf8("; ");
        out += r.headers[i].second;
    }
    return out;
}

} // namespace qxv
