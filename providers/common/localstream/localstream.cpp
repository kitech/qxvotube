#include "localstream.h"

#include "common/debuglog.h"

#include "httplib.h"

#include <QDateTime>

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <random>
#include <thread>

// 口径见 localstream.h。服务骨架(线程/会话/Range)与具体源无关;
// 源只负责 read(off,len) 产出明文。

namespace qxv {
namespace localstream {

namespace {

const char* kMod = "localstream";

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

std::mt19937& randGen() {
    static std::mt19937 g(static_cast<unsigned>(
        std::random_device{}() ^ static_cast<unsigned>(nowMs())));
    return g;
}

QString randHex(int n) {
    static const char* kHex = "0123456789abcdef";
    QByteArray s;
    s.reserve(n);
    for (int i = 0; i < n; ++i) s.append(kHex[randGen()() % 16]);
    return QString::fromLatin1(s);
}

// 新变量名优先,兼容旧 QXVOTUBE_HG_* 口径
QString envFirst(const char* a, const char* b) {
    QString v = QXV_ENV(a);
    if (v.isEmpty() && b) v = QXV_ENV(b);
    return v;
}

struct Session {
    QString token;
    QString key;
    std::shared_ptr<Source> src;
    int refs = 0;
    int active = 0;
    qint64 lastAccess = 0;
};

} // namespace

struct Server::Impl {
    Options opt;
    std::unique_ptr<httplib::Server> srv;
    std::thread listenTh;
    std::thread watchTh;
    std::mutex mu;
    std::mutex cvMu;
    std::condition_variable cv;
    std::map<QString, std::shared_ptr<Session>> byToken;   // token → session
    std::atomic<bool> running{ false };
    std::atomic<qint64> lastRequest{ 0 };
    int port = 0;
    qint64 startedAt = 0;
    QString lastStopReason;

    qint64 activeTotal() {
        std::lock_guard<std::mutex> lk(mu);
        qint64 n = 0;
        for (auto& kv : byToken) n += kv.second->active;
        return n;
    }

    std::shared_ptr<Session> findByToken(const QString& t) {
        std::lock_guard<std::mutex> lk(mu);
        auto it = byToken.find(t);
        return it == byToken.end() ? std::shared_ptr<Session>() : it->second;
    }

    void joinThreads() {
        if (listenTh.joinable()) listenTh.join();
        if (watchTh.joinable()) watchTh.join();
    }

    void reap(const QString& reason, bool fromWatchdog) {
        // 先停标志再 join:watchdog 的循环条件是 running
        {
            std::lock_guard<std::mutex> lk(mu);
            running = false;
        }
        cv.notify_all();
        if (srv) srv->stop();
        // 不持锁 join(handler 可能正等锁)
        if (listenTh.joinable()) listenTh.join();
        if (!fromWatchdog && watchTh.joinable()) watchTh.join();
        int nsess = 0;
        {
            std::lock_guard<std::mutex> lk(mu);
            nsess = int(byToken.size());
            byToken.clear();
            if (srv) srv.reset();
            lastStopReason = reason;
        }
        const qint64 up = startedAt ? (nowMs() - startedAt) : 0;
        DBG_P(kMod, Info,
              "stream server stop (reason=%s, sessions=%d, uptime=%llds)",
              qPrintable(reason), nsess, (long long)(up / 1000));
        DBG_P(kMod, Debug, "listen/watchdog threads joined");
    }

    void watchLoop() {
        while (running) {
            {
                std::unique_lock<std::mutex> lk(cvMu);
                cv.wait_for(lk, std::chrono::seconds(1),
                            [this] { return !running.load(); });
            }
            if (!running) break;
            const qint64 now = nowMs();
            {
                std::lock_guard<std::mutex> lk(mu);
                for (auto it = byToken.begin(); it != byToken.end();) {
                    if (it->second->refs == 0 && it->second->active == 0
                        && now - it->second->lastAccess
                               > qint64(opt.sessionIdleSec) * 1000) {
                        DBG_P(kMod, Info, "session evict key=%s (idle)",
                              qPrintable(it->second->key));
                        it = byToken.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
            if (opt.idleSec > 0
                && now - lastRequest.load() > qint64(opt.idleSec) * 1000
                && activeTotal() == 0) {
                DBG_P(kMod, Info, "idle %ds reached -> stopping", opt.idleSec);
                reap(qFromUtf8("idle"), true);
                return;
            }
        }
    }

    bool startLocked(Result& err) {
        joinThreads();
        srv.reset(new httplib::Server());
        srv->set_read_timeout(30, 0);
        srv->set_write_timeout(30, 0);
        srv->set_idle_interval(0, 200000);
        srv->Get("/s/([0-9a-f]{32})\\.mp4",
                 [this](const httplib::Request& req, httplib::Response& res) {
            const QString token = QString::fromUtf8(req.matches[1].str().c_str());
            auto s = findByToken(token);
            if (!s) {
                res.status = 404;
                res.set_content("session not found", "text/plain");
                return;
            }
            s->lastAccess = nowMs();
            lastRequest.store(s->lastAccess);
            {
                std::lock_guard<std::mutex> lk(mu);
                if (s->refs == 0) s->refs = 1;   // 播放器续播:临时计数防回收
                ++s->active;
            }
            DBG_P(kMod, Debug, "GET %s offset=%s", qPrintable(token.left(8)),
                  req.has_header("Range")
                      ? qPrintable(QString::fromUtf8(
                            req.get_header_value("Range").c_str()))
                      : "0");
            res.set_content_provider(
                size_t(s->src->total()), s->src->contentType().toUtf8().constData(),
                [this, s](size_t offset, size_t length, httplib::DataSink& sink) {
                QByteArray buf;
                if (!s->src->read(quint64(offset), quint64(length), &buf))
                    return false;
                if (!buf.isEmpty()
                    && !sink.write(buf.constData(), size_t(buf.size())))
                    return false;
                return true;   // 不要调 sink.done():由 httplib 负责
            },
                [this, s](bool) {
                std::lock_guard<std::mutex> lk(mu);
                if (s->active > 0) --s->active;
                s->lastAccess = nowMs();
                lastRequest.store(s->lastAccess);
            });
        });
        srv->set_exception_handler([](const httplib::Request&,
                                      httplib::Response& res, std::exception_ptr) {
            res.status = 500;
            res.set_content("internal error", "text/plain");
        });

        int p = 0;
        if (opt.fixedPort > 0)
            p = srv->bind_to_port("127.0.0.1", opt.fixedPort) ? opt.fixedPort : -1;
        else
            p = srv->bind_to_any_port("127.0.0.1");
        if (p <= 0) {
            srv.reset();
            err = Result::fail(Err::Network,
                               qFromUtf8("localstream: bind 127.0.0.1 失败"));
            DBG_P(kMod, Warn, "stream server start failed (bind)");
            return false;
        }
        port = p;
        startedAt = nowMs();
        lastRequest.store(startedAt);
        running = true;
        listenTh = std::thread([this] { srv->listen_after_bind(); });
        watchTh = std::thread([this] { watchLoop(); });
        DBG_P(kMod, Info, "stream server start (port=%d, bind=ok)", port);
        DBG_P(kMod, Debug, "stream server listening on 127.0.0.1:%d", port);
        return true;
    }
};

Server::Server() : d(std::make_shared<Impl>()) {
    if (!QXV_ENV_EMPTY("QXVOTUBE_STREAM_IDLE_SEC"))
        d->opt.idleSec = QXV_ENV("QXVOTUBE_STREAM_IDLE_SEC").toInt();
    else if (!QXV_ENV_EMPTY("QXVOTUBE_HG_IDLE_SEC"))
        d->opt.idleSec = QXV_ENV("QXVOTUBE_HG_IDLE_SEC").toInt();
    if (!QXV_ENV_EMPTY("QXVOTUBE_STREAM_SESS_TTL_SEC"))
        d->opt.sessionIdleSec = QXV_ENV("QXVOTUBE_STREAM_SESS_TTL_SEC").toInt();
    else if (!QXV_ENV_EMPTY("QXVOTUBE_HG_SESS_TTL_SEC"))
        d->opt.sessionIdleSec = QXV_ENV("QXVOTUBE_HG_SESS_TTL_SEC").toInt();
    if (!QXV_ENV_EMPTY("QXVOTUBE_STREAM_MAX_SESSIONS"))
        d->opt.maxSessions = QXV_ENV("QXVOTUBE_STREAM_MAX_SESSIONS").toInt();
    else if (!QXV_ENV_EMPTY("QXVOTUBE_HG_MAX_SESSIONS"))
        d->opt.maxSessions = QXV_ENV("QXVOTUBE_HG_MAX_SESSIONS").toInt();
    if (!QXV_ENV_EMPTY("QXVOTUBE_STREAM_PORT"))
        d->opt.fixedPort = QXV_ENV("QXVOTUBE_STREAM_PORT").toInt();
    else if (!QXV_ENV_EMPTY("QXVOTUBE_HG_STREAM_PORT"))
        d->opt.fixedPort = QXV_ENV("QXVOTUBE_HG_STREAM_PORT").toInt();
}

Server::~Server() { stop(qFromUtf8("shutdown")); }

Server& Server::instance() {
    static Server s;
    return s;
}

void Server::configure(const Options& o) {
    std::lock_guard<std::mutex> lk(d->mu);
    if (d->running) return;
    d->opt = o;
}

Options Server::options() const {
    std::lock_guard<std::mutex> lk(const_cast<Impl*>(d.get())->mu);
    return d->opt;
}

Result Server::start() {
    if (envFirst("QXVOTUBE_STREAM", "QXVOTUBE_HG_STREAM") == qFromUtf8("off"))
        return Result::fail(Err::Unsupported,
                            qFromUtf8("localstream disabled by env"));
    Result err = Result::ok();
    std::lock_guard<std::mutex> lk(d->mu);
    if (d->running) return Result::ok();
    if (!d->startLocked(err)) return err;
    return Result::ok();
}

void Server::stop(const QString& reason) {
    if (!d->running && !d->listenTh.joinable() && !d->watchTh.joinable() && !d->srv)
        return;
    d->reap(reason, false);
}

int Server::port() const { return d->port; }

Stats Server::stats() const {
    Stats s;
    std::lock_guard<std::mutex> lk(const_cast<Impl*>(d.get())->mu);
    s.running = d->running;
    s.port = d->port;
    s.sessions = int(d->byToken.size());
    s.activeStreams = 0;
    for (auto& kv : d->byToken) s.activeStreams += kv.second->active;
    s.lastStopReason = d->lastStopReason;
    s.uptimeMs = d->startedAt ? (nowMs() - d->startedAt) : 0;
    return s;
}

Result Server::open(const QString& key, std::shared_ptr<Source> src,
                    QString* outUrl) {
    if (!src)
        return Result::fail(Err::Parse, qFromUtf8("localstream: null source"));
    if (envFirst("QXVOTUBE_STREAM", "QXVOTUBE_HG_STREAM") == qFromUtf8("off"))
        return Result::fail(Err::Unsupported,
                            qFromUtf8("localstream disabled by env"));
    const Result st = start();
    if (st.code != Err::Ok) return st;

    // 已存在同 key → refs+1 复用
    {
        std::lock_guard<std::mutex> lk(d->mu);
        for (auto& kv : d->byToken) {
            if (kv.second->key == key) {
                ++kv.second->refs;
                kv.second->lastAccess = nowMs();
                *outUrl = qFromUtf8("http://127.0.0.1:%1/s/%2.mp4")
                              .arg(d->port).arg(kv.second->token);
                DBG_P(kMod, Debug, "session reuse key=%s (refs=%d)",
                      qPrintable(key), kv.second->refs);
                return Result::ok();
            }
        }
    }

    auto s = std::make_shared<Session>();
    s->token = randHex(32);
    s->key = key;
    s->src = src;
    s->refs = 1;
    s->lastAccess = nowMs();

    int nsess = 0;
    {
        std::lock_guard<std::mutex> lk(d->mu);
        while (int(d->byToken.size()) >= d->opt.maxSessions) {
            auto victim = d->byToken.end();
            qint64 oldest = 0;
            for (auto it = d->byToken.begin(); it != d->byToken.end(); ++it) {
                if (it->second->refs > 0 || it->second->active > 0) continue;
                if (victim == d->byToken.end() || it->second->lastAccess < oldest) {
                    victim = it;
                    oldest = it->second->lastAccess;
                }
            }
            if (victim == d->byToken.end()) break;   // 全在用
            DBG_P(kMod, Info, "session evict key=%s (lru)",
                  qPrintable(victim->second->key));
            d->byToken.erase(victim);
        }
        d->byToken[s->token] = s;
        nsess = int(d->byToken.size());
        *outUrl = qFromUtf8("http://127.0.0.1:%1/s/%2.mp4")
                      .arg(d->port).arg(s->token);
    }
    DBG_P(kMod, Info, "session open key=%s (refs=1, cache=%d, total=%llu)",
          qPrintable(key), nsess,
          (unsigned long long)s->src->total());
    return Result::ok();
}

void Server::release(const QString& key) {
    std::lock_guard<std::mutex> lk(d->mu);
    for (auto it = d->byToken.begin(); it != d->byToken.end(); ++it) {
        Session& s = *it->second;
        if (s.key != key) continue;
        if (s.refs > 0) --s.refs;
        s.lastAccess = nowMs();
        DBG_P(kMod, Debug, "session release key=%s (refs=%d)", qPrintable(key),
              s.refs);
        break;
    }
}

QString Server::tokenOf(const QString& key) const {
    std::lock_guard<std::mutex> lk(const_cast<Impl*>(d.get())->mu);
    for (auto& kv : d->byToken)
        if (kv.second->key == key) return kv.second->token;
    return QString();
}

std::shared_ptr<Source> Server::sourceOf(const QString& token) const {
    std::lock_guard<std::mutex> lk(const_cast<Impl*>(d.get())->mu);
    auto it = d->byToken.find(token);
    return it == d->byToken.end() ? std::shared_ptr<Source>() : it->second->src;
}

} // namespace localstream
} // namespace qxv
