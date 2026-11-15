#include "xiaohongshuprovider.h"
#include "common/autherror.h"
#include "common/qcjson.h"
#include "common/debuglog.h"
#include "common/fingerprint.h"
#include "parse.h"
#include "ssr.h"

#include <QDateTime>
#include <QTimer>
#include <QUrlQuery>

#include <cmath>
#include <random>

// 口径:docs/providers/xiaohongshu-research.md
// 关键实测:
//   ① 游客 SSR 可用(explore/detail+token/user);
//   ② 详情必须带 xsec_token(按笔记签发,缺失则为空);
//   ③ 搜索/API 一律 -101 无登录信息 → NeedAuth;
//   ④ 风控阈值官方未公开:沿用 XHS-Downloader 默认(均值 6s 对数正态,
//      clamp 1.2~30s),并加每分钟/每日上限与指数冷却。

namespace qxv {

static const char* kMod = "xiaohongshu";
static const char* kSite = "https://www.xiaohongshu.com";

namespace {

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

// 首页频道(翻页替代):page1 用默认,page>=2 依次换频道
const char* kChannels[] = {
    "homefeed_recommend",
    "homefeed.fashion_v3",
    "homefeed.food_v3",
    "homefeed.travel_v3",
    "homefeed.career_v3",
    "homefeed.movie_and_tv_v3",
};
const int kChannelCount = int(sizeof(kChannels) / sizeof(kChannels[0]));

int envInt(const char* name, int def) {
    if (QXV_ENV_EMPTY(name)) return def;
    bool ok = false;
    const int v = QXV_ENV(name).toInt(&ok);
    return ok ? v : def;
}

// 对数正态延迟:均值 6s(与 XHS-Downloader 默认一致),clamp 1.2~30s
long long jitterDelayMs(int meanMs) {
    static std::mt19937 g(static_cast<unsigned>(std::random_device{}()));
    std::normal_distribution<double> nd(0.0, 0.6);
    const double mean = meanMs / 1000.0;
    const double mu = std::log(mean) - 0.5 * 0.36;
    double sec = std::exp(mu + 0.6 * nd(g));
    const double lo = mean * 0.2, hi = mean * 5.0;
    if (sec < lo) sec = lo;
    if (sec > hi) sec = hi;
    return static_cast<long long>(sec * 1000);
}

} // namespace

XiaohongshuProvider::XiaohongshuProvider() = default;

void XiaohongshuProvider::cancel() {
    m_net.cancel();
    m_cache.clear();
}

// ── 节流 gate:所有请求排队(异步,不阻塞 UI)────────────────────────
void XiaohongshuProvider::gate(std::function<void()> fn) {
    const qint64 now = nowMs();
    if (now < m_cooldownUntil) {
        const qint64 left = m_cooldownUntil - now;
        DBG_P(kMod, Info, "cooldown active, drop request (%llds left)",
              (long long)(left / 1000));
        // 冷却期直接失败(调用方 cb 走 RateLimit,这里由各 API 检查后返回)
        QTimer::singleShot(0, [fn]() { fn(); });
        return;
    }
    // 每分钟/每日上限
    if (m_minuteStart == 0 || now - m_minuteStart >= 60000) {
        m_minuteStart = now;
        m_minuteCount = 0;
    }
    if (m_dayStart == 0 || now - m_dayStart >= 86400000) {
        m_dayStart = now;
        m_dayCount = 0;
    }
    const int maxPerMin = envInt("QXVOTUBE_XHS_MAX_PER_MIN", 10);
    const int maxPerDay = envInt("QXVOTUBE_XHS_MAX_PER_DAY", 100);
    if (m_minuteCount >= maxPerMin) {
        const qint64 wait = 60000 - (now - m_minuteStart);
        DBG_P(kMod, Info, "per-minute cap reached, delay %lldms", (long long)wait);
        m_nextAllowed = qMax(m_nextAllowed, now + wait);
    }
    if (m_dayCount >= maxPerDay) {
        DBG_P(kMod, Warn, "per-day cap reached (%d)", m_dayCount);
        return;   // 不再发起(调用方拿到空响应会走错误分支)
    }

    const long long delay = m_nextAllowed > now ? (m_nextAllowed - now) : 0;
    auto run = [this, fn]() {
        m_nextAllowed = nowMs() + jitterDelayMs(
            envInt("QXVOTUBE_XHS_DELAY_MS", 6000));
        ++m_minuteCount;
        ++m_dayCount;
        fn();
    };
    if (delay > 0)
        QTimer::singleShot(int(delay), run);
    else
        run();
}

void XiaohongshuProvider::onSoftBlock() {
    const long long ladder[] = { 5 * 60 * 1000LL, 30 * 60 * 1000LL,
                                 120 * 60 * 1000LL };
    const int idx = qMin(m_blockLevel, 2);
    const qint64 ms = envInt("QXVOTUBE_XHS_COOLDOWN_MS",
                             int(ladder[idx]));
    m_cooldownUntil = nowMs() + ms;
    if (m_blockLevel < 2) ++m_blockLevel;
    DBG_P(kMod, Warn, "soft block detected -> cooldown %llds (level %d)",
          (long long)(ms / 1000), m_blockLevel);
}

QString XiaohongshuProvider::pickToken(const QString& noteId) const {
    auto it = m_tokens.find(noteId);
    if (it == m_tokens.end()) return QString();
    if (nowMs() >= it->expiresAt) return QString();
    return it->token;
}

void XiaohongshuProvider::rememberToken(const QString& noteId,
                                        const QString& token) {
    if (noteId.isEmpty() || token.isEmpty()) return;
    if (m_tokens.size() > 256) m_tokens.clear();
    TokenEntry e;
    e.token = token;
    e.expiresAt = nowMs() + 5 * 60 * 1000;   // 5 分钟(旧链接会被风控)
    m_tokens[noteId] = e;
}

// ── 通用 SSR GET ────────────────────────────────────────────────────
void XiaohongshuProvider::fetchPage(
    const QString& pathWithQuery,
    std::function<void(const Result&, const QByteArray&)> cb) {
    if (!m_jarInit) { m_net.setCookieJar(&m_jar); m_jarInit = true; }
    if (nowMs() < m_cooldownUntil) {
        const qint64 left = m_cooldownUntil - nowMs();
        Result r = authFail(Err::RateLimit, credContext(m_cred),
                            qFromUtf8("xiaohongshu: 风控冷却中"));
        r.detail.insert(qFromUtf8("retryAfterMs"), left);
        cb(r, QByteArray());
        return;
    }
    gate([this, pathWithQuery, cb]() {
        NetRequest req;
        req.url = qFromUtf8("%1%2").arg(qFromUtf8(kSite), pathWithQuery);
        for (const auto& h : Fingerprint::chromeWeb(qFromUtf8("zh-CN,zh;q=0.9"))
                                 .headers)
            req.headers.append(h);
        req.headers.append(qMakePair(qFromUtf8("Accept"),
            qFromUtf8("text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8")));
        req.headers.append(qMakePair(qFromUtf8("Accept-Language"),
                                     qFromUtf8("zh-CN,zh;q=0.9,en;q=0.8")));
        req.headers.append(qMakePair(qFromUtf8("Referer"),
                                     qFromUtf8("https://www.xiaohongshu.com/")));
        req.autoCookie = true;
        req.timeoutSec = 25;
        req.maxAttempts = 2;      // 风控环境下少重试,避免加重
        req.retryDelayMs = 2000;
        if (m_proxy.enabled)
            req.proxy = qFromUtf8("http://%1:%2").arg(m_proxy.host).arg(m_proxy.port);
        DBG_P(kMod, Debug, "GET %s", qPrintable(req.url));
        m_net.send(req, [this, cb](const NetResponse& r) {
            if (r.status == 302) {   // 跳登录
                onSoftBlock();
                QVariantMap extra;
                extra.insert(qFromUtf8("captchaUrl"),
                             qFromUtf8("https://www.xiaohongshu.com/explore"));
                cb(authFail(Err::CaptchaRequired, credContext(m_cred),
                            qFromUtf8("xiaohongshu: 被重定向到登录/验证"),
                            r.status, QString(), QString(), extra),
                   QByteArray());
                return;
            }
            if (r.status == 461 || r.status == 406) {
                onSoftBlock();
                QVariantMap extra;
                extra.insert(qFromUtf8("retryAfterMs"),
                             m_cooldownUntil - nowMs());
                cb(authFail(Err::RateLimit, credContext(m_cred),
                            qFromUtf8("xiaohongshu: 风控(%1)").arg(r.status),
                            r.status, QString::number(r.status),
                            QString::fromUtf8(r.body.left(200)), extra),
                   QByteArray());
                return;
            }
            if (!r.ok()) {
                cb(authFail(Err::Network, credContext(m_cred),
                            r.error.isEmpty()
                                ? qFromUtf8("http %1").arg(r.status) : r.error,
                            r.status, QString(),
                            QString::fromUtf8(r.body.left(200))), QByteArray());
                return;
            }
            cb(Result::ok(), r.body);
        });
    });
}

// ── search:必须登录(无免登录通道)─────────────────────────────────
void XiaohongshuProvider::search(const QString& kw, int page, SearchCb cb) {
    Q_UNUSED(kw);
    Q_UNUSED(page);
    const AuthContext ctx = credContext(m_cred);
    cb(authFail(Err::NeedAuth, ctx,
       ctx.provided
           ? qFromUtf8("xiaohongshu: 已设置凭证但搜索仍被拒(需登录态 API)")
           : qFromUtf8("xiaohongshu: 搜索需登录(官方 web API -101);"
                       "游客可用首页/用户页/笔记详情"),
       200, qFromUtf8("-101"), qFromUtf8("无登录信息")), {}, false);
}

// ── homeFeed:explore + 频道轮转 ────────────────────────────────────
void XiaohongshuProvider::homeFeed(int page, SearchCb cb) {
    if (page < 1) page = 1;
    QString path = qFromUtf8("/explore");
    if (page > 1) {
        if (page - 2 >= kChannelCount) {
            cb(Result::ok(), {}, false);
            return;
        }
        path += qFromUtf8("?channel_id=%1")
                    .arg(QString::fromUtf8(kChannels[page - 2]));
    }
    fetchPage(path, [this, cb, page](const Result& r, const QByteArray& body) {
        if (r.code != Err::Ok) { cb(r, {}, false); return; }
        JRoot root;
        if (!xiaohongshu::ssr::extractInitialState(body, root)) {
            onSoftBlock();   // 无 __INITIAL_STATE__ = 反爬降级
            cb(authFail(Err::RateLimit, credContext(m_cred),
                        qFromUtf8("xiaohongshu: 首页无 __INITIAL_STATE__(风控)"),
                        200, QString(),
                        QString::fromUtf8(body.left(200))), {}, false);
            return;
        }
        const QList<VideoItem> items = xiaohongshu::parse::feedNotes(root.p);
        if (items.isEmpty()) {
            onSoftBlock();
            cb(authFail(Err::RateLimit, credContext(m_cred),
                        qFromUtf8("xiaohongshu: 首页 feed 为空(风控)"),
                        200, QString(),
                        QString::fromUtf8(body.left(200))), {}, false);
            return;
        }
        for (const VideoItem& it : items)
            rememberToken(it.id,
                          it.extra.value(qFromUtf8("xsecToken")).toString());
        const bool hasMore = page < kChannelCount + 1;
        DBG_P(kMod, Info, "homeFeed p%d -> %d items (hasMore=%d)", page,
              items.size(), int(hasMore));
        cb(Result::ok(), items, hasMore);
    });
}

// ── detail:必须 token ──────────────────────────────────────────────
void XiaohongshuProvider::detail(const QString& id, DetailCb cb) {
    QString noteId = id, token;
    if (id.contains(QLatin1Char('@'))) {
        const int at = id.indexOf(QLatin1Char('@'));
        noteId = id.left(at);
        token = id.mid(at + 1);
    } else if (id.startsWith(qFromUtf8("http"))) {
        QString t;
        xiaohongshu::parse::parseShareUrl(id, &noteId, &t);
        token = t;
    }
    if (token.isEmpty()) token = pickToken(noteId);
    if (noteId.isEmpty() || token.isEmpty()) {
        cb(authFail(Err::NeedAuth, credContext(m_cred),
           qFromUtf8("xiaohongshu: 缺 xsec_token(从首页/用户页打开,"
                     "或粘贴分享链接;id 可写 noteId@token)")), {}, {});
        return;
    }
    QUrlQuery q;
    q.addQueryItem(qFromUtf8("xsec_token"), token);
    q.addQueryItem(qFromUtf8("xsec_source"), qFromUtf8("pc_feed"));
    const QString path = qFromUtf8("/explore/%1?%2")
                             .arg(noteId, q.toString(QUrl::FullyEncoded));
    fetchPage(path, [this, cb, noteId](const Result& r, const QByteArray& body) {
        if (r.code != Err::Ok) { cb(r, {}, {}); return; }
        JRoot root;
        if (!xiaohongshu::ssr::extractInitialState(body, root)) {
            onSoftBlock();
            cb(authFail(Err::RateLimit, credContext(m_cred),
                        qFromUtf8("xiaohongshu: 详情无 SSR(风控)"), 200,
                        QString(), QString::fromUtf8(body.left(200))), {}, {});
            return;
        }
        VideoItem vi;
        QList<Stream> streams;
        const Result res =
            xiaohongshu::parse::noteDetail(root.p, noteId, &vi, &streams);
        if (res.code == Err::Parse) {
            onSoftBlock();   // detailMap 空 = 软风控/token 过期
            cb(authFail(Err::RateLimit, credContext(m_cred),
                        qFromUtf8("xiaohongshu: 详情为空(token 过期或风控)"),
                        200, QString(),
                        QString::fromUtf8(body.left(200))), {}, {});
            return;
        }
        QList<VideoItem> parts;   // 小红书无分集
        if (res.code == Err::Ok) parts.append(vi);
        cb(res, vi, parts);
    });
}

// ── resolve ────────────────────────────────────────────────────────
void XiaohongshuProvider::resolve(const QString& id, ResolveCb cb) {
    const QString ckey = qFromUtf8("xiaohongshu|%1").arg(id);
    PlayResult cached;
    if (m_cache.get(ckey, &cached)) { cb(Result::ok(), cached); return; }

    // 解析 id/token(与 detail 同口径),只发一次请求
    QString noteId = id, token;
    if (id.contains(QLatin1Char('@'))) {
        const int at = id.indexOf(QLatin1Char('@'));
        noteId = id.left(at);
        token = id.mid(at + 1);
    } else if (id.startsWith(qFromUtf8("http"))) {
        QString t;
        xiaohongshu::parse::parseShareUrl(id, &noteId, &t);
        token = t;
    }
    if (token.isEmpty()) token = pickToken(noteId);
    if (noteId.isEmpty() || token.isEmpty()) {
        cb(authFail(Err::NeedAuth, credContext(m_cred),
           qFromUtf8("xiaohongshu: 缺 xsec_token(从首页点开或粘贴分享链接)")), {});
        return;
    }
    QUrlQuery q;
    q.addQueryItem(qFromUtf8("xsec_token"), token);
    q.addQueryItem(qFromUtf8("xsec_source"), qFromUtf8("pc_feed"));
    const QString path = qFromUtf8("/explore/%1?%2")
                             .arg(noteId, q.toString(QUrl::FullyEncoded));
    fetchPage(path, [this, cb, noteId, ckey](const Result& r,
                                             const QByteArray& body) {
        if (r.code != Err::Ok) { cb(r, {}); return; }
        JRoot root;
        if (!xiaohongshu::ssr::extractInitialState(body, root)) {
            onSoftBlock();
            cb(Result::fail(Err::RateLimit,
                            qFromUtf8("xiaohongshu: 详情无 SSR(风控)")), {});
            return;
        }
        VideoItem vi;
        QList<Stream> streams;
        const Result res =
            xiaohongshu::parse::noteDetail(root.p, noteId, &vi, &streams);
        if (res.code == Err::Parse) {
            onSoftBlock();
            cb(Result::fail(Err::RateLimit,
                            qFromUtf8("xiaohongshu: 详情为空(token 过期或风控)")),
               {});
            return;
        }
        if (res.code != Err::Ok) { cb(res, {}); return; }
        PlayResult pr;
        pr.item = vi;
        pr.streams = streams;
        m_cache.put(ckey, pr, 10 * 60 * 1000);
        DBG_P(kMod, Info, "resolve %s -> %d streams", qPrintable(noteId),
              pr.streams.size());
        cb(Result::ok(), pr);
    });
}

void XiaohongshuProvider::resolveQuality(const QString& id,
                                         const QString& quality,
                                         ResolveCb cb) {
    // 小红书每篇笔记视频档位固定(通常 1080x720/单档)→ 忽略 quality 请求
    Q_UNUSED(quality);
    resolve(id, std::move(cb));
}

// ── userVideos:用户页 SSR(可能无 id,见调研)───────────────────────
void XiaohongshuProvider::userVideos(const QString& uid, int page, SearchCb cb) {
    if (page > 1) {   // SSR 首屏 30 条,无分页参数
        cb(Result::ok(), {}, false);
        return;
    }
    fetchPage(qFromUtf8("/user/profile/%1").arg(uid),
              [this, cb, uid](const Result& r, const QByteArray& body) {
        if (r.code != Err::Ok) { cb(r, {}, false); return; }
        JRoot root;
        if (!xiaohongshu::ssr::extractInitialState(body, root)) {
            onSoftBlock();
            cb(authFail(Err::RateLimit, credContext(m_cred),
                        qFromUtf8("xiaohongshu: 用户页无 SSR(风控)"), 200,
                        QString(), QString::fromUtf8(body.left(200))), {}, false);
            return;
        }
        QList<VideoItem> items = xiaohongshu::parse::userNotes(root.p);
        if (items.isEmpty()) {
            onSoftBlock();
            cb(authFail(Err::RateLimit, credContext(m_cred),
                        qFromUtf8("xiaohongshu: 用户页无笔记(风控/隐私)"), 200,
                        QString(), QString::fromUtf8(body.left(200))), {}, false);
            return;
        }
        for (const VideoItem& it : items)
            rememberToken(it.id,
                          it.extra.value(qFromUtf8("xsecToken")).toString());
        DBG_P(kMod, Info, "userVideos %s -> %d items", qPrintable(uid),
              items.size());
        cb(Result::ok(), items, false);
    });
}

// ── comments:SSR 首屏(若有)───────────────────────────────────────
void XiaohongshuProvider::comments(const QString& id, const QString& cursor,
                                   CommentsCb cb) {
    Q_UNUSED(id);
    Q_UNUSED(cursor);
    cb(Result::fail(Err::Unsupported,
       qFromUtf8("xiaohongshu: 评论仅详情 SSR 首屏,未单独提供(需登录 API)")),
       {}, QString());
}

} // namespace qxv
