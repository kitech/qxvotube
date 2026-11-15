#include "hgstream.h"

#include "common/debuglog.h"
#include "common/hgcenc/cenc.h"
#include "common/hgcenc/mp4box.h"
#include "common/hgcenc/spade.h"

#include "httplib.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QMap>

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <thread>

// 口径见 hgstream.h 顶部注释 + docs/protocols/bytedance.md §6.1 与 dong/xb
// 流式代理实现(Range 对齐: 每样本独立 IV,跨窗口只需按样本边界拉取)。

namespace qxv {
namespace hgstream {

namespace {

const char* kMod = "hongguo-stream";
const char* kUa = "com.phoenix.read/73532 (Linux; U; Android 16; zh_CN)";

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

quint32 rd32(const QByteArray& b, quint64 o) {
    return (quint32(static_cast<unsigned char>(b[int(o)])) << 24)
         | (quint32(static_cast<unsigned char>(b[int(o) + 1])) << 16)
         | (quint32(static_cast<unsigned char>(b[int(o) + 2])) << 8)
         |  quint32(static_cast<unsigned char>(b[int(o) + 3]));
}

quint64 rd64(const QByteArray& b, quint64 o) {
    quint64 v = 0;
    for (int i = 0; i < 8; ++i)
        v = (v << 8) | quint32(static_cast<unsigned char>(b[int(o) + i]));
    return v;
}

struct Fetched {
    bool ok = false;
    int status = 0;
    QByteArray data;
    quint64 total = 0;
    QString err;
};

QString proxyHostPort(const QString& proxy, QString* scheme) {
    // "http://host:port" → host、port;简单解析(仅自用)
    QString p = proxy;
    *scheme = qFromUtf8("http");
    if (p.startsWith(qFromUtf8("http://"))) p = p.mid(7);
    else if (p.startsWith(qFromUtf8("https://"))) { p = p.mid(8); *scheme = qFromUtf8("https"); }
    return p;
}

// 远端 Range GET(open 与会话填充共用;带 UA/跟随跳转)
Fetched fetchRange(const QString& url, quint64 off, quint64 len,
                   const QString& proxy) {
    Fetched f;
    const int sp = url.indexOf(qFromUtf8("://"));
    if (sp < 0) { f.err = qFromUtf8("bad url"); return f; }
    const QString scheme = url.left(sp);
    const int slash = url.indexOf('/', sp + 3);
    const QString hostPort = slash < 0 ? url.mid(sp + 3)
                                       : url.mid(sp + 3, slash - sp - 3);
    const QString path = slash < 0 ? QString() : url.mid(slash);
    const int colon = hostPort.lastIndexOf(':');
    const QString host = colon < 0 ? hostPort : hostPort.left(colon);
    const int port = colon < 0 ? (scheme == qFromUtf8("https") ? 443 : 80)
                              : hostPort.mid(colon + 1).toInt();

    try {
        // 注意:Client(host, port) 恒为明文;TLS 必须走 scheme 形式(httplib v0.20)
        httplib::Client cli(qFromUtf8("%1://%2:%3").arg(scheme, host).arg(port)
                                .toStdString());
        cli.set_follow_location(true);
        cli.set_connection_timeout(10, 0);
        cli.set_read_timeout(30, 0);
        cli.set_write_timeout(10, 0);
        if (scheme == qFromUtf8("https")) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            cli.enable_server_certificate_verification(true);
#endif
        }
        if (!proxy.isEmpty()) {
            QString ps;
            const QString hp = proxyHostPort(proxy, &ps);
            const int c = hp.lastIndexOf(':');
            cli.set_proxy(hp.left(c).toStdString(), hp.mid(c + 1).toInt());
        }
        httplib::Headers h = {
            { "Range", qFromUtf8("bytes=%1-%2").arg(off).arg(off + len - 1)
                          .toUtf8().constData() },
            { "User-Agent", kUa },
        };
        auto r = cli.Get(path.toStdString(), h);
        if (!r) { f.err = qFromUtf8("connect failed"); return f; }
        f.status = r->status;
        f.data = QByteArray(r->body.data(), int(r->body.size()));
        if (r->status == 206) {
            const QString cr = QString::fromUtf8(
                r->get_header_value("Content-Range").c_str());
            const int sl = cr.lastIndexOf('/');
            if (sl >= 0) f.total = cr.mid(sl + 1).toULongLong();
            f.ok = true;
        } else if (r->status == 200) {
            // 服务端忽略 Range:只有从 0 取才有意义
            f.ok = off == 0;
            f.total = quint64(f.data.size());
        } else {
            f.err = qFromUtf8("http %1").arg(r->status);
        }
    } catch (...) {
        f.err = qFromUtf8("exception");
    }
    return f;
}

// 同长度四个字符标签替换(仅当所在位置像 box 头:前置 4 字节是合理尺寸)。
// 不动长度 → stco 全部偏移有效(容器无需重写)。
bool renameTag(QByteArray& moov, const char* from, const char* to) {
    const QByteArray pat(from, 4);
    int hits = 0;
    int i = int(moov.indexOf(pat, 0));
    while (i >= 8) {
        const quint32 sz = rd32(moov, quint64(i) - 4);
        if (sz >= 8 && quint64(sz) <= quint64(moov.size())) {
            moov.replace(i, 4, QByteArray(to, 4));
            ++hits;
            i = int(moov.indexOf(pat, i + 4));
        } else {
            i = int(moov.indexOf(pat, i + 1));
        }
    }
    return hits > 0;
}

// moov 内同长度标签改写(让播放器当明文处理):
//   encv→hvc1/avc1(看 hvcC/avcC 子盒)、enca→mp4a、sinf/schi/tenc/senc→free
void patchMoov(QByteArray& moov) {
    const bool hevc = moov.contains(QByteArray("hvcC", 4));
    renameTag(moov, "encv", hevc ? "hvc1" : "avc1");
    renameTag(moov, "enca", "mp4a");
    renameTag(moov, "sinf", "free");
    renameTag(moov, "schi", "free");
    renameTag(moov, "tenc", "free");
    renameTag(moov, "senc", "free");
}

struct Session {
    QString token;
    QString vid;
    QString quality;
    QString remoteUrl;
    QByteArray key;                 // 16B
    quint64 total = 0;
    quint64 moovOff = 0;
    QByteArray moovPatched;         // 含 box 头(同长度改写后)
    QList<hgcenc::TrackInfo> tracks;
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
    std::mutex mu;                  // 保护 byToken/port/state
    std::mutex cvMu;
    std::condition_variable cv;
    std::map<QString, std::shared_ptr<Session>> byToken;
    std::atomic<bool> running{ false };
    std::atomic<qint64> lastRequest{ 0 };
    int port = 0;
    qint64 startedAt = 0;
    QString lastStopReason;
    QString proxy;

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

    // 拉取 [off,off+len) 的**明文**(样本解密 + moov 覆盖)
    bool fill(const std::shared_ptr<Session>& s, quint64 off, quint64 len,
              QByteArray& out) {
        Fetched f = fetchRange(s->remoteUrl, off, len, proxy);
        if (!f.ok) {
            DBG_P(kMod, Warn, "fill fetch failed off=%llu len=%llu: %s",
                  (unsigned long long)off, (unsigned long long)len,
                  qPrintable(f.err));
            return false;
        }
        out = f.data;
        const quint64 outEnd = off + quint64(out.size());

        // moov 区：改写后的字节直接覆盖(尺寸相同)
        const quint64 moovEnd = s->moovOff + quint64(s->moovPatched.size());
        if (s->moovOff < outEnd && moovEnd > off) {
            const quint64 a = qMax(off, s->moovOff);
            const quint64 b = qMin(outEnd, moovEnd);
            memcpy(out.data() + int(a - off),
                   s->moovPatched.constData() + int(a - s->moovOff),
                   size_t(b - a));
        }

        // 样本区：整体落在窗口内的就地解；跨窗口拉整样本后切片
        for (int ti = 0; ti < s->tracks.size(); ++ti) {
            const hgcenc::TrackInfo& t = s->tracks[ti];
            for (int i = 0; i < t.sampleOffsets.size(); ++i) {
                const quint64 so = t.sampleOffsets[i];
                const int sz = int(t.sampleSizes[i]);
                if (so >= outEnd) break;
                if (so + quint64(sz) <= off) continue;
                if (so >= off && so + quint64(sz) <= outEnd) {
                    hgcenc::TrackInfo one = t;
                    one.sampleOffsets = QList<quint64>() << (so - off);
                    one.sampleSizes = QList<quint32>() << quint32(sz);
                    one.baseIv = t.baseIv + quint64(i);
                    if (hgcenc::Cenc::decryptTrack(out, one, s->key) != 1)
                        return false;
                } else {
                    Fetched sf = fetchRange(s->remoteUrl, so, quint64(sz), proxy);
                    if (!sf.ok || sf.data.size() != sz) return false;
                    QByteArray one = sf.data;
                    hgcenc::TrackInfo tt = t;
                    tt.sampleOffsets = QList<quint64>() << 0;
                    tt.sampleSizes = QList<quint32>() << quint32(sz);
                    tt.baseIv = t.baseIv + quint64(i);
                    if (hgcenc::Cenc::decryptTrack(one, tt, s->key) != 1)
                        return false;
                    const quint64 a = qMax(off, so);
                    const quint64 b = qMin(outEnd, so + quint64(sz));
                    memcpy(out.data() + int(a - off),
                           one.constData() + int(a - so), size_t(b - a));
                }
            }
        }
        return true;
    }

    void joinThreads() {
        if (listenTh.joinable()) listenTh.join();
        if (watchTh.joinable()) watchTh.join();
    }

    void reap(const QString& reason, bool fromWatchdog) {
        // 先停标志再 join:watchdog 的循环条件是 running,否则 join 永远等不到退出
        {
            std::lock_guard<std::mutex> lk(mu);
            running = false;
        }
        cv.notify_all();
        if (srv) srv->stop();
        // 注意:不得持有 mu 时 join(handler 可能正等 mu)
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
            // 会话回收：refs==0 && 无在途 && 静默超时
            {
                std::lock_guard<std::mutex> lk(mu);
                for (auto it = byToken.begin(); it != byToken.end();) {
                    if (it->second->refs == 0 && it->second->active == 0
                        && now - it->second->lastAccess
                               > qint64(opt.sessionIdleSec) * 1000) {
                        DBG_P(kMod, Info, "session evict vid=%s q=%s (idle)",
                              qPrintable(it->second->vid),
                              qPrintable(it->second->quality));
                        it = byToken.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
            // 空闲自动关
            if (opt.idleSec > 0
                && now - lastRequest.load() > qint64(opt.idleSec) * 1000
                && activeTotal() == 0) {
                DBG_P(kMod, Info, "idle %ds reached -> stopping",
                      opt.idleSec);
                reap(qFromUtf8("idle"), true);
                return;
            }
        }
    }

    bool startLocked(Result& err) {
        joinThreads();   // 清上一轮(空闲关后残留)
        srv.reset(new httplib::Server());
        srv->set_read_timeout(30, 0);
        srv->set_write_timeout(30, 0);
        srv->set_idle_interval(0, 200000);
        srv->Get("/hg/([0-9a-f]{32})\\.mp4",
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
                if (s->refs == 0) {
                    // 播放器直接续播(provider 已 release):临时计数,防回收
                    s->refs = 1;
                }
                ++s->active;
            }
            DBG_P(kMod, Debug, "GET %s offset=%s", qPrintable(token.left(8)),
                  req.has_header("Range")
                      ? qPrintable(QString::fromUtf8(
                            req.get_header_value("Range").c_str()))
                      : "0");
            const quint64 total = s->total;
            res.set_content_provider(
                size_t(total), "video/mp4",
                [this, s](size_t offset, size_t length,
                          httplib::DataSink& sink) {
                QByteArray buf;
                if (!fill(s, quint64(offset), quint64(length), buf))
                    return false;
                if (!buf.isEmpty()
                    && !sink.write(buf.constData(), size_t(buf.size())))
                    return false;
                return true;   // 不要调 sink.done():由 httplib 负责(未设置时抛 bad_function_call)
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
                               qFromUtf8("hgstream: bind 127.0.0.1 失败"));
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

Server::Server() : d(new Impl) {
    // env 覆盖(测试/用户调参)
    if (!QXV_ENV_EMPTY("QXVOTUBE_HG_IDLE_SEC"))
        d->opt.idleSec = QXV_ENV("QXVOTUBE_HG_IDLE_SEC").toInt();
    if (!QXV_ENV_EMPTY("QXVOTUBE_HG_SESS_TTL_SEC"))
        d->opt.sessionIdleSec = QXV_ENV("QXVOTUBE_HG_SESS_TTL_SEC").toInt();
    if (!QXV_ENV_EMPTY("QXVOTUBE_HG_MAX_SESSIONS"))
        d->opt.maxSessions = QXV_ENV("QXVOTUBE_HG_MAX_SESSIONS").toInt();
    if (!QXV_ENV_EMPTY("QXVOTUBE_HG_STREAM_PORT"))
        d->opt.fixedPort = QXV_ENV("QXVOTUBE_HG_STREAM_PORT").toInt();
}

Server::~Server() {
    stop(qFromUtf8("shutdown"));
    delete d;
}

Server& Server::instance() {
    static Server s;
    return s;
}

void Server::configure(const Options& o) {
    std::lock_guard<std::mutex> lk(d->mu);
    if (d->running) return;   // 运行中不改(避免竞态)
    d->opt = o;
}

Options Server::options() const {
    std::lock_guard<std::mutex> lk(const_cast<Impl*>(d)->mu);
    return d->opt;
}

Result Server::start() {
    if (QXV_ENV("QXVOTUBE_HG_STREAM") == qFromUtf8("off"))
        return Result::fail(Err::Unsupported,
                            qFromUtf8("hgstream disabled by env"));
    Result err = Result::ok();
    std::lock_guard<std::mutex> lk(d->mu);
    if (d->running) return Result::ok();
    if (!d->startLocked(err)) return err;
    return Result::ok();
}

void Server::stop(const QString& reason) {
    // 不持锁调用 reap(内部会抢锁 + join)
    if (!d->running && !d->listenTh.joinable() && !d->watchTh.joinable()
        && !d->srv)
        return;
    d->reap(reason, false);
}

int Server::port() const { return d->port; }

Stats Server::stats() const {
    Stats s;
    std::lock_guard<std::mutex> lk(const_cast<Impl*>(d)->mu);
    s.running = d->running;
    s.port = d->port;
    s.sessions = int(d->byToken.size());
    s.activeStreams = 0;
    for (auto& kv : d->byToken) s.activeStreams += kv.second->active;
    s.lastStopReason = d->lastStopReason;
    s.uptimeMs = d->startedAt ? (nowMs() - d->startedAt) : 0;
    return s;
}

void Server::setProxy(const QString& proxy) {
    std::lock_guard<std::mutex> lk(d->mu);
    d->proxy = proxy;
}

Result Server::open(const QString& vid, const QString& quality,
                    const QString& remoteUrl, const QString& spadeA,
                    QString* outUrl) {
    if (QXV_ENV("QXVOTUBE_HG_STREAM") == qFromUtf8("off"))
        return Result::fail(Err::Unsupported,
                            qFromUtf8("hgstream disabled by env"));
    const Result st = start();
    if (st.code != Err::Ok) return st;

    const QByteArray key = QByteArray::fromHex(
        hgcenc::Spade::unwrapV1(QByteArray::fromBase64(spadeA.toUtf8()), 0)
            .toUtf8());
    if (key.size() != 16)
        return Result::fail(Err::Parse, qFromUtf8("hgstream: spade unwrap 失败"));

    // 已存在 → refs+1 复用
    {
        std::lock_guard<std::mutex> lk(d->mu);
        for (auto& kv : d->byToken) {
            if (kv.second->vid == vid && kv.second->quality == quality
                && kv.second->remoteUrl == remoteUrl) {
                ++kv.second->refs;
                kv.second->lastAccess = nowMs();
                *outUrl = qFromUtf8("http://127.0.0.1:%1/hg/%2.mp4")
                              .arg(d->port).arg(kv.second->token);
                DBG_P(kMod, Debug, "session reuse vid=%s q=%s (refs=%d)",
                      qPrintable(vid), qPrintable(quality), kv.second->refs);
                return Result::ok();
            }
        }
    }

    Fetched head = fetchRange(remoteUrl, 0, quint64(d->opt.prefetchBytes),
                             d->proxy);
    if (!head.ok)
        return Result::fail(Err::Network,
                            qFromUtf8("hgstream: 头部拉取失败 %1").arg(head.err));
    quint64 total = head.total ? head.total : quint64(head.data.size());

    // 顶层找 moov(需完整;不足则按精确 Range 补拉)
    quint64 moovOff = 0, moovSize = 0;
    {
        quint64 o = 0;
        while (o + 8 <= quint64(head.data.size())) {
            const quint64 sz = rd32(head.data, o);
            const quint32 ty = rd32(head.data, o + 4);
            const bool isMoov = (ty == 0x6d6f6f76);   // 'moov'
            if (isMoov) {
                moovOff = o;
                moovSize = (sz == 1) ? rd64(head.data, o + 8) : sz;
                break;
            }
            if (sz < 8) break;
            o += sz;
        }
    }
    QByteArray moov;
    if (moovSize == 0)
        return Result::fail(Err::Parse, qFromUtf8("hgstream: 无 moov"));
    if (moovOff + moovSize <= quint64(head.data.size())) {
        moov = head.data.mid(int(moovOff), int(moovSize));
    } else {
        Fetched mf = fetchRange(remoteUrl, moovOff, moovSize, d->proxy);
        if (!mf.ok || quint64(mf.data.size()) < moovSize)
            return Result::fail(Err::Network,
                                qFromUtf8("hgstream: moov 拉取失败"));
        moov = mf.data.left(int(moovSize));
    }

    QList<hgcenc::TrackInfo> tracks;
    if (hgcenc::findTracks(moov, tracks) != 0)
        return Result::fail(Err::Parse,
                            qFromUtf8("hgstream: 加密轨解析失败"));

    patchMoov(moov);

    auto s = std::make_shared<Session>();
    s->token = randHex(32);
    s->vid = vid;
    s->quality = quality;
    s->remoteUrl = remoteUrl;
    s->key = key;
    s->total = total;
    s->moovOff = moovOff;
    s->moovPatched = moov;
    s->tracks = tracks;
    s->refs = 1;
    s->lastAccess = nowMs();

    int nsess = 0;
    {
        std::lock_guard<std::mutex> lk(d->mu);
        // LRU 上限:超限淘汰 refs==0 且最久未访问
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
            DBG_P(kMod, Info, "session evict vid=%s (lru)",
                  qPrintable(victim->second->vid));
            d->byToken.erase(victim);
        }
        d->byToken[s->token] = s;
        nsess = int(d->byToken.size());
        *outUrl = qFromUtf8("http://127.0.0.1:%1/hg/%2.mp4")
                      .arg(d->port).arg(s->token);
    }
    DBG_P(kMod, Info, "session open vid=%s q=%s (refs=1, cache=%d, total=%llu)",
          qPrintable(vid), qPrintable(quality), nsess,
          (unsigned long long)total);
    return Result::ok();
}

void Server::release(const QString& vid, const QString& quality) {
    bool evicted = false;
    {
        std::lock_guard<std::mutex> lk(d->mu);
        for (auto it = d->byToken.begin(); it != d->byToken.end(); ++it) {
            Session& s = *it->second;
            if (s.vid != vid) continue;
            if (!quality.isEmpty() && s.quality != quality) continue;
            if (s.refs > 0) --s.refs;
            s.lastAccess = nowMs();
            DBG_P(kMod, Debug, "session release vid=%s q=%s (refs=%d)",
                  qPrintable(s.vid), qPrintable(s.quality), s.refs);
            if (s.refs == 0 && s.active == 0) evicted = true;  // 已无引用,交由清理
            break;
        }
    }
    Q_UNUSED(evicted);
}

QList<Server::SampleRef> Server::samplesOf(const QString& token,
                                          int n) const {
    QList<SampleRef> out;
    auto s = d->findByToken(token);
    if (!s) return out;
    for (int ti = 0; ti < s->tracks.size(); ++ti) {
        const hgcenc::TrackInfo& t = s->tracks[ti];
        for (int i = 0; i < t.sampleOffsets.size() && out.size() < n; ++i)
            out.append({ t.sampleOffsets[i], int(t.sampleSizes[i]),
                         t.isVideo });
    }
    return out;
}

QString Server::tokenOf(const QString& vid, const QString& quality) const {
    std::lock_guard<std::mutex> lk(const_cast<Impl*>(d)->mu);
    for (auto& kv : d->byToken)
        if (kv.second->vid == vid
            && (quality.isEmpty() || kv.second->quality == quality))
            return kv.second->token;
    return QString();
}

} // namespace hgstream
} // namespace qxv
